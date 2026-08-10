#pragma once

#include <ti/driverlib/dl_dma.h>
#include <ti/driverlib/dl_uart_main.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <type_traits>
#include <utility>

#include "driver/common/serialized_service.hpp"
#include "mspm0_dma_dispatcher.hpp"
#include "ti_msp_dl_config.h"
#include "uart.hpp"
#include "uart/uart_dma_model.hpp"

namespace LibXR
{

/**
 * @brief UART-IRQ-only execution policy for the MSPM0 UART backend.
 *
 * Ordinary publishers only store durable facts and pend the UART IRQ. The supplied
 * stack-local handler is neither called nor retained. The UART ISR claims the service
 * before its first protected hardware access.
 */
class MSPM0UartIrqPolicy
{
 public:
  explicit MSPM0UartIrqPolicy(IRQn_Type irqn) : irqn_(irqn) {}

  template <typename Handler>
  bool Invoke(uint32_t events, Handler&& handler) noexcept
  {
    static_cast<void>(handler);
    service_.Publish(events);
    __DMB();
    NVIC_SetPendingIRQ(irqn_);
    return false;
  }

  template <typename Source, typename Handler>
  bool InvokeIrq(Source&& source, Handler&& handler) noexcept
  {
    return service_.ClaimAndInvoke(std::forward<Source>(source),
                                   [this, &handler](uint32_t snapshot) noexcept
                                   {
                                     const uint32_t continuation = handler(snapshot);
                                     service_.Publish(continuation);
                                   });
  }

  MSPM0UartIrqPolicy(const MSPM0UartIrqPolicy&) = delete;
  MSPM0UartIrqPolicy& operator=(const MSPM0UartIrqPolicy&) = delete;

 private:
  IRQn_Type irqn_;
  SerializedService service_{};
};

/**
 * @brief Aligned contiguous storage for the common UART TX double buffer.
 *
 * The total extent is split into two equal DMA blocks. Keeping the alignment in the
 * storage type prevents a plain byte array from passing the public construction helper
 * and then failing `DoubleBufferStorage` validation at runtime.
 *
 * @tparam TotalSize Total storage extent in bytes
 */
template <size_t TotalSize>
class alignas(size_t) MSPM0UARTTxBuffer
{
  static_assert(TotalSize > 0U, "MSPM0 UART TX storage must not be empty");
  static_assert((TotalSize % 2U) == 0U,
                "MSPM0 UART TX storage must contain two equal blocks");
  static_assert((TotalSize % (2U * alignof(size_t))) == 0U,
                "MSPM0 UART TX storage must satisfy common double-buffer alignment");
  static_assert((TotalSize / 2U) <= 0xFFFFU, "MSPM0 UART TX block exceeds DMA size");

 public:
  [[nodiscard]] uint8_t* Data() { return storage_.data(); }
  [[nodiscard]] const uint8_t* Data() const { return storage_.data(); }
  [[nodiscard]] static constexpr size_t Size() { return TotalSize; }

 private:
  alignas(size_t) std::array<uint8_t, TotalSize> storage_{};
};

/**
 * @brief MSPM0 UART backend with common TX DMA ownership and explicit RX capability.
 *
 * All TX and control semantics come from `UartDmaModel`. Main RX uses threshold-one
 * byte interrupts. Every Extend RX uses one full-channel repeated `2N` DMA ring. FULL
 * completion is mandatory; DMA HALF Pre-IRQ is enabled only when the binding selects it
 * on a channel that provides it.
 *
 * Extend RX is conditional on the BSP proving
 * `T_broker + T_uart_irq + T_copy < N * frame_bits / baud` with hardware margin when
 * HALF is available. A FULL-only binding has a tighter wrap-service bound and must treat
 * any observed overwrite as loss. The driver derives LIN compare from the current clock
 * and UART framing; a match only flushes a stable partial tail into the byte stream. It
 * is not UART IDLE and does not publish a frame boundary. G3507 fixed UART TX DMA also
 * requires the BSP clock contract `MCLK == ULPCLK` because of `DMA_ERR_01`. The driver
 * always enables both TX and RX; this also keeps RXE set while G3507 waits for EOT as
 * required by `UART_ERR_02`.
 */
class MSPM0UART : public UART
{
  friend class UartDmaModel<MSPM0UART, MSPM0UartIrqPolicy>;

 public:
  enum class RxMode : uint8_t
  {
    MAIN_BYTE_IRQ,
    EXTEND_DMA,
  };

  /** @brief Low-level descriptor populated by the public SysConfig macros. */
  struct Resources
  {
    UART_Regs* instance;
    IRQn_Type irqn;
    uint32_t clock_freq;
    uint8_t index;
    RxMode rx_mode;
    bool rx_half_interrupt;
    uint8_t dma_tx_channel;
    uint8_t dma_tx_trigger;
    uint8_t dma_rx_channel;
    uint8_t dma_rx_trigger;
  };

  static constexpr uint8_t INVALID_DMA_CHANNEL = 0xFFU;

  /**
   * @brief Construct and take ownership of one UART and its named DMA resources.
   * @param res Low-level resources produced by a construction macro
   * @param tx_dma_storage Writable contiguous `2N` common TX storage
   * @param rx_dma_storage Empty for Main RX; writable contiguous `2N` ring for Extend RX
   * @param tx_queue_size Pending TX record queue depth
   * @param rx_queue_capacity Software byte queue capacity
   * @param config Initial framing and baud rate
   * @pre SysConfig initialization has powered and clocked the UART and DMA register
   *      blocks. Do not construct this object during global initialization before
   *      `SYSCFG_DL_init()`.
   * @pre DMA storage remains writable, DMA-accessible, and alive until destruction.
   */
  MSPM0UART(Resources res, RawData tx_dma_storage, RawData rx_dma_storage,
            uint32_t tx_queue_size, uint32_t rx_queue_capacity,
            UART::Configuration config = {115200, UART::Parity::NO_PARITY, 8, 1});

  /**
   * @brief Release a quiescent application-lifetime UART instance.
   * @pre No operation, callback, DMA transfer, IRQ, or incoming character overlaps.
   */
  ~MSPM0UART();

  /**
   * @brief 异步接纳完整 UART 配置 / Asynchronously admit a complete UART configuration
   * @param config Requested baud rate and framing
   * @return `OK` once admitted, `BUSY` while another CONFIG is outstanding, or a
   *         validation error; `OK` does not mean the hardware change is already complete
   * @note May be called from task context or an ordinary maskable ISR. The call only
   *       publishes work; UART IRQ remains the hardware owner.
   * @pre Single-core execution outside NMI and HardFault. SMP callers require a separate
   *      platform contract.
   */
  ErrorCode SetConfig(UART::Configuration config) override;

  static ErrorCode WriteFun(WritePort& port, bool in_isr);
  static void OnInterrupt(uint8_t index);
  static bool InIsr();
  static UART::Configuration BuildConfigFromSysCfg(UART_Regs* instance,
                                                   uint32_t baudrate);

  [[nodiscard]] RxMode GetRxMode() const { return res_.rx_mode; }
  [[nodiscard]] bool RxHalfInterruptEnabled() const { return res_.rx_half_interrupt; }
  [[nodiscard]] uint32_t GetRxDropCount() const;
  [[nodiscard]] uint32_t GetRxLossGeneration() const;
  [[nodiscard]] uint32_t GetRxDeadlineViolationCount() const;
  [[nodiscard]] uint32_t GetRxStaleEventCount() const;
  [[nodiscard]] uint32_t GetRxOverrunCount() const;
  [[nodiscard]] uint32_t GetRxFramingErrorCount() const;
  [[nodiscard]] uint32_t GetRxParityErrorCount() const;
  [[nodiscard]] uint32_t GetRxBreakErrorCount() const;
  [[nodiscard]] uint32_t GetRxNoiseErrorCount() const;
  [[nodiscard]] uint32_t GetDmaErrorCount() const;
  [[nodiscard]] uint32_t GetRecoveryCount() const;

  ReadPort _read_port;    // NOLINT
  WritePort _write_port;  // NOLINT

  static constexpr uint8_t ResolveIndex(IRQn_Type irqn)
  {
    switch (irqn)
    {
#if defined(UART0_BASE)
      case UART0_INT_IRQn:
        return 0U;
#endif
#if defined(UART1_BASE)
      case UART1_INT_IRQn:
        return 1U;
#endif
#if defined(UART2_BASE)
      case UART2_INT_IRQn:
        return 2U;
#endif
#if defined(UART3_BASE)
      case UART3_INT_IRQn:
        return 3U;
#endif
#if defined(UART4_BASE)
      case UART4_INT_IRQn:
        return 4U;
#endif
#if defined(UART5_BASE)
      case UART5_INT_IRQn:
        return 5U;
#endif
#if defined(UART6_BASE)
      case UART6_INT_IRQn:
        return 6U;
#endif
#if defined(UART7_BASE)
      case UART7_INT_IRQn:
        return 7U;
#endif
      default:
        return INVALID_INSTANCE_INDEX;
    }
  }

 private:
  static constexpr uint8_t MAX_UART_INSTANCES = 8U;
  static constexpr uint8_t INVALID_INSTANCE_INDEX = 0xFFU;
  static constexpr uint32_t RX_DMA_FACT_BITS = 3U;
  static constexpr uint32_t RX_DMA_FACT_MASK = (1U << RX_DMA_FACT_BITS) - 1U;
  static constexpr uint32_t RX_DMA_EPOCH_SHIFT = RX_DMA_FACT_BITS;

  enum class RxDmaFact : uint32_t
  {
    HALF = 1U << 0U,
    FULL = 1U << 1U,
    CONFLICT = 1U << 2U,
  };

  enum class RxDmaPhase : uint8_t
  {
    HALF,
    FULL,
  };

  enum class RxDmaSample : uint8_t
  {
    STABLE,
    WAIT_BOUNDARY,
    RETRY,
    INVALID,
  };

  enum class ControlPhase : uint8_t
  {
    IDLE,
    WAIT_EOT,
    WAIT_UART_IDLE,
    QUIESCENT,
  };

  struct Counters
  {
    std::atomic<uint32_t> rx_drop{0U};
    std::atomic<uint32_t> rx_loss_generation{0U};
    std::atomic<uint32_t> rx_deadline_violation{0U};
    std::atomic<uint32_t> rx_stale_event{0U};
    std::atomic<uint32_t> rx_overrun{0U};
    std::atomic<uint32_t> rx_framing{0U};
    std::atomic<uint32_t> rx_parity{0U};
    std::atomic<uint32_t> rx_break{0U};
    std::atomic<uint32_t> rx_noise{0U};
    std::atomic<uint32_t> dma_error{0U};
    std::atomic<uint32_t> recovery{0U};
  };

  static constexpr uint32_t FactMask(RxDmaFact fact)
  {
    return static_cast<uint32_t>(fact);
  }

  void HandleInterrupt();
  uint32_t CaptureIrqEvents();
  void CaptureMainRx(uint32_t pending, uint32_t& events);
  void CaptureExtendRx(uint32_t& events);
  void DrainMainRx();
  bool ConsumeRxDmaFacts(uint32_t facts);
  bool FlushPartialRx();
  RxDmaSample SampleRxDmaPosition(size_t& position);
  void PublishRxRange(size_t begin, size_t end);
  void DiscardRxFifo();
  void CountUartInterruptErrors(uint32_t pending);
  static uint32_t RxWordInterruptErrors(uint32_t word);
  void InvalidateRxEpoch();
  void StartRxEpoch();

  [[nodiscard]] ErrorCode ValidateConfig(UART::Configuration config) const;
  void ValidateResources() const;
  void ApplyInitialConfig(UART::Configuration config);
  void ApplyDisabledConfig(UART::Configuration config);
  void ConfigureTxDma();
  void ConfigureRxDma();
  [[nodiscard]] uint32_t RxDmaEventMask() const;
  [[nodiscard]] uint32_t RxDmaBoundaryRawMask() const;
  void StartDataPath();
  void StopDataPathInterrupts();

  UartDmaControlResult AdvanceConfig(UART::Configuration config, bool active_tx,
                                     bool in_isr);
  UartDmaControlProgress CompleteConfig(bool in_isr);
  UartDmaControlResult AdvanceRecovery(bool active_tx, bool in_isr);
  UartDmaControlProgress CompleteRecovery(bool in_isr);
  UartDmaControlResult AdvanceControlStop(bool active_tx, bool error_stop, bool in_isr);
  void BeginControlStop(bool active_tx, bool error_stop);
  bool AdvanceToQuiescence();
  void FinishControl();
  UartDmaTxStartResult StartDmaTx(uint8_t* data, size_t size, int block, bool in_isr);

  static void TxDmaCallback(void* context, uint32_t events);
  static void RxDmaCallback(void* context, uint32_t events);
  void PublishRxDmaFacts(uint32_t facts);
  static uint32_t NextEpoch(uint32_t epoch);

  Resources res_;
  RawData rx_dma_storage_;
  size_t rx_dma_half_size_ = 0U;
  MSPM0UartIrqPolicy execution_policy_;
  UartDmaModel<MSPM0UART, MSPM0UartIrqPolicy> dma_model_;
  MSPM0DmaDispatcher::Registration tx_dma_registration_{};
  MSPM0DmaDispatcher::Registration rx_dma_registration_{};
  std::atomic<uint32_t> rx_dma_facts_{0U};
  std::atomic<uint32_t> rx_epoch_{1U};
  std::atomic<uint32_t> dma_error_pending_{0U};
  Counters counters_{};
  UART::Configuration active_config_{};
  ControlPhase control_phase_ = ControlPhase::IDLE;
  UartOldTxTerminal stopped_tx_terminal_ = UartOldTxTerminal::NONE;
  RxDmaPhase rx_dma_phase_ = RxDmaPhase::HALF;
  size_t rx_dma_cursor_ = 0U;
  bool rx_pushed_in_owner_ = false;
  bool rx_epoch_invalid_ = false;
  bool rx_partial_flush_pending_ = false;
  bool control_config_applied_ = false;
  bool uart_disabled_for_control_ = false;
  bool tx_line_active_ = false;
  bool control_active_tx_ = false;
  bool control_error_stop_ = false;
  bool tx_complete_observed_ = false;
  bool registered_tx_dma_ = false;
  bool registered_rx_dma_ = false;

  static MSPM0UART* instance_map_[MAX_UART_INSTANCES];
};

namespace Detail
{

template <typename T>
concept MSPM0ByteStorageElement =
    !std::is_const_v<T> && !std::is_volatile_v<T> &&
    (std::is_same_v<T, char> || std::is_same_v<T, signed char> ||
     std::is_same_v<T, unsigned char> || std::is_same_v<T, std::byte>);

template <size_t N>
RawData MSPM0UARTTxStorage(MSPM0UARTTxBuffer<N>& storage)
{
  static_assert(alignof(MSPM0UARTTxBuffer<N>) >= alignof(size_t));
  return RawData{static_cast<void*>(storage.Data()), storage.Size()};
}

template <MSPM0ByteStorageElement T, size_t N>
RawData MSPM0UARTRxDmaStorage(T (&storage)[N])
{
  static_assert(N > 0U, "MSPM0 UART RX DMA storage must not be empty");
  static_assert((N % 2U) == 0U, "MSPM0 UART RX DMA storage must contain two halves");
  static_assert(N <= 0xFFFFU, "MSPM0 UART RX DMA ring exceeds DMA size");
  return RawData{static_cast<void*>(std::data(storage)), N};
}

template <MSPM0ByteStorageElement T, size_t N>
RawData MSPM0UARTRxDmaStorage(std::array<T, N>& storage)
{
  static_assert(N > 0U, "MSPM0 UART RX DMA storage must not be empty");
  static_assert((N % 2U) == 0U, "MSPM0 UART RX DMA storage must contain two halves");
  static_assert(N <= 0xFFFFU, "MSPM0 UART RX DMA ring exceeds DMA size");
  return RawData{static_cast<void*>(std::data(storage)), N};
}

template <IRQn_Type UartIrqn, uint32_t TxDmaChannel, IRQn_Type TxDmaOwnerIrqn,
          bool TxBinding, bool ExtendCapable, bool DispatcherAvailable>
MSPM0UART::Resources MakeMSPM0MainUartResources(UART_Regs* instance, uint32_t clock_freq,
                                                uint8_t tx_dma_trigger)
{
  static_assert(TxBinding, "The named DMA resource is not a UART TX binding");
  static_assert(DispatcherAvailable,
                "MSPM0 UART TX requires the shared DMA IRQ dispatcher binding");
  static_assert(!ExtendCapable,
                "A physical Extend UART must use the MSPM0 Extend DMA path");
  static_assert(TxDmaOwnerIrqn == UartIrqn,
                "The named TX DMA resource belongs to another UART");
  static_assert(TxDmaChannel < DMA_SYS_N_DMA_CHANNEL,
                "MSPM0 UART TX DMA channel is outside the device range");
  return {instance,
          UartIrqn,
          clock_freq,
          MSPM0UART::ResolveIndex(UartIrqn),
          MSPM0UART::RxMode::MAIN_BYTE_IRQ,
          false,
          static_cast<uint8_t>(TxDmaChannel),
          tx_dma_trigger,
          MSPM0UART::INVALID_DMA_CHANNEL,
          0U};
}

template <IRQn_Type UartIrqn, uint32_t TxDmaChannel, IRQn_Type TxDmaOwnerIrqn,
          bool TxBinding, uint32_t RxDmaChannel, IRQn_Type RxDmaOwnerIrqn, bool RxBinding,
          bool ExtendCapable, bool FullRxDmaChannel, bool HalfRxInterrupt,
          bool DispatcherAvailable>
MSPM0UART::Resources MakeMSPM0ExtendUartResources(UART_Regs* instance,
                                                  uint32_t clock_freq,
                                                  uint8_t tx_dma_trigger,
                                                  uint8_t rx_dma_trigger)
{
  static_assert(TxBinding, "The named DMA resource is not a UART TX binding");
  static_assert(RxBinding, "The named DMA resource is not a UART RX binding");
  static_assert(TxDmaOwnerIrqn == UartIrqn,
                "The named TX DMA resource belongs to another UART");
  static_assert(RxDmaOwnerIrqn == UartIrqn,
                "The named RX DMA resource belongs to another UART");
  static_assert(TxDmaChannel < DMA_SYS_N_DMA_CHANNEL,
                "MSPM0 UART TX DMA channel is outside the device range");
  static_assert(TxDmaChannel != RxDmaChannel,
                "MSPM0 UART TX and RX require distinct DMA channels");
  static_assert(ExtendCapable,
                "The selected SysConfig/BSP UART binding is not Extend-capable");
  static_assert(FullRxDmaChannel, "MSPM0 Extend RX requires a full-channel DMA binding");
  static_assert(DispatcherAvailable,
                "MSPM0 Extend RX requires the shared DMA IRQ dispatcher binding");
  static_assert(RxDmaChannel < DMA_SYS_N_DMA_FULL_CHANNEL,
                "MSPM0 Extend RX requires a full DMA channel");
  static_assert(
      !HalfRxInterrupt || MSPM0DmaDispatcher::EarlyInterruptSupported(RxDmaChannel),
      "The selected RX DMA binding does not support a half-transfer interrupt");
  return {instance,
          UartIrqn,
          clock_freq,
          MSPM0UART::ResolveIndex(UartIrqn),
          MSPM0UART::RxMode::EXTEND_DMA,
          HalfRxInterrupt,
          static_cast<uint8_t>(TxDmaChannel),
          tx_dma_trigger,
          static_cast<uint8_t>(RxDmaChannel),
          rx_dma_trigger};
}

}  // namespace Detail

/**
 * @brief BSP binding markers consumed by the MSPM0 UART construction macros.
 *
 * Keep the ownership markers in a BSP-owned binding header rather than editing the
 * regenerated `ti_msp_dl_config.h`. SysConfig remains authoritative for
 * `<dma>_CHAN_ID`; the binding header adds `<dma>_LIBXR_UART_IRQN`, exactly one of
 * `<dma>_LIBXR_UART_TX` or `<dma>_LIBXR_UART_RX`, and the whole-BSP acknowledgement
 * `LIBXR_MSPM0_DMA_DISPATCHER_AVAILABLE`. An Extend RX binding additionally defines
 * `<dma>_LIBXR_FULL_CHANNEL`, `<dma>_LIBXR_HALF_INTERRUPT`, and
 * `<uart>_LIBXR_EXTEND_CAPABLE`.
 *
 * These markers assert whole-BSP ownership and direction. The construction macros
 * derive the physical trigger from the expanded UART instance token, so ordinary
 * callers never provide a numeric channel, IRQ, trigger, or capability flag.
 */
#define LIBXR_MSPM0_UART_CAT_IMPL(left, right) left##right
#define LIBXR_MSPM0_UART_CAT(left, right) LIBXR_MSPM0_UART_CAT_IMPL(left, right)
#define LIBXR_MSPM0_UART_PROPERTY(name, suffix) LIBXR_MSPM0_UART_CAT(name, suffix)
#define LIBXR_MSPM0_UART_DMA_CHANNEL(dma_name) LIBXR_MSPM0_UART_CAT(dma_name, _CHAN_ID)
#define LIBXR_MSPM0_UART_DMA_PROPERTY(dma_name, suffix) \
  LIBXR_MSPM0_UART_CAT(dma_name, suffix)
#define LIBXR_MSPM0_UART_DMA_TRIGGER_IMPL(instance, direction) \
  DMA_##instance##_##direction##_TRIG
#define LIBXR_MSPM0_UART_DMA_TRIGGER(instance, direction) \
  LIBXR_MSPM0_UART_DMA_TRIGGER_IMPL(instance, direction)

/**
 * @brief Construct Main RX resources from SysConfig UART and named TX DMA tokens.
 * @pre Call only after `SYSCFG_DL_init()` has initialized the selected UART and DMA.
 */
#define MSPM0_UART_MAIN_INIT(name, tx_dma, tx_storage, tx_queue_size, rx_queue_capacity) \
  ::LibXR::Detail::MakeMSPM0MainUartResources<                                           \
      name##_INST_INT_IRQN, static_cast<uint32_t>(LIBXR_MSPM0_UART_DMA_CHANNEL(tx_dma)), \
      LIBXR_MSPM0_UART_DMA_PROPERTY(tx_dma, _LIBXR_UART_IRQN),                           \
      static_cast<bool>(LIBXR_MSPM0_UART_DMA_PROPERTY(tx_dma, _LIBXR_UART_TX)),          \
      static_cast<bool>(LIBXR_MSPM0_UART_PROPERTY(name, _LIBXR_EXTEND_CAPABLE)),         \
      static_cast<bool>(LIBXR_MSPM0_DMA_DISPATCHER_AVAILABLE)>(                          \
      name##_INST, name##_INST_FREQUENCY,                                                \
      static_cast<uint8_t>(LIBXR_MSPM0_UART_DMA_TRIGGER(name##_INST, TX))),              \
      ::LibXR::Detail::MSPM0UARTTxStorage((tx_storage)), ::LibXR::RawData{},             \
      (tx_queue_size), (rx_queue_capacity),                                              \
      ::LibXR::MSPM0UART::BuildConfigFromSysCfg(name##_INST,                             \
                                                static_cast<uint32_t>(name##_BAUD_RATE))

/**
 * @brief Construct Extend RX resources using repeated full-channel DMA.
 *
 * The BSP/generated binding must define `<uart>_LIBXR_EXTEND_CAPABLE` to one for an
 * audited Extend instance. The named RX DMA channel is compile-time checked as full;
 * HALF Pre-IRQ is an explicit binding choice and is checked against channel capability.
 * Repeat DMA does not pause at a boundary. The UART IRQ must publish a boundary before
 * DMA overwrites any still-unpublished byte. On a FULL-only channel this can be the first
 * byte after FULL when no partial range was flushed; one completely unseen wrap also
 * aliases the prior position and sticky COMPLETE state and therefore cannot be detected.
 * LIN compare is derived from the current functional clock and UART framing, so the
 * SysConfig UART token does not need a counter-compare macro.
 * @pre Call only after `SYSCFG_DL_init()` has initialized the selected UART and DMA.
 */
#define MSPM0_UART_EXTEND_INIT(name, tx_dma, rx_dma, tx_storage, tx_queue_size,          \
                               rx_dma_storage, rx_queue_capacity)                        \
  ::LibXR::Detail::MakeMSPM0ExtendUartResources<                                         \
      name##_INST_INT_IRQN, static_cast<uint32_t>(LIBXR_MSPM0_UART_DMA_CHANNEL(tx_dma)), \
      LIBXR_MSPM0_UART_DMA_PROPERTY(tx_dma, _LIBXR_UART_IRQN),                           \
      static_cast<bool>(LIBXR_MSPM0_UART_DMA_PROPERTY(tx_dma, _LIBXR_UART_TX)),          \
      static_cast<uint32_t>(LIBXR_MSPM0_UART_DMA_CHANNEL(rx_dma)),                       \
      LIBXR_MSPM0_UART_DMA_PROPERTY(rx_dma, _LIBXR_UART_IRQN),                           \
      static_cast<bool>(LIBXR_MSPM0_UART_DMA_PROPERTY(rx_dma, _LIBXR_UART_RX)),          \
      static_cast<bool>(LIBXR_MSPM0_UART_PROPERTY(name, _LIBXR_EXTEND_CAPABLE)),         \
      static_cast<bool>(LIBXR_MSPM0_UART_DMA_PROPERTY(rx_dma, _LIBXR_FULL_CHANNEL)),     \
      static_cast<bool>(LIBXR_MSPM0_UART_DMA_PROPERTY(rx_dma, _LIBXR_HALF_INTERRUPT)),   \
      static_cast<bool>(LIBXR_MSPM0_DMA_DISPATCHER_AVAILABLE)>(                          \
      name##_INST, name##_INST_FREQUENCY,                                                \
      static_cast<uint8_t>(LIBXR_MSPM0_UART_DMA_TRIGGER(name##_INST, TX)),               \
      static_cast<uint8_t>(LIBXR_MSPM0_UART_DMA_TRIGGER(name##_INST, RX))),              \
      ::LibXR::Detail::MSPM0UARTTxStorage((tx_storage)),                                 \
      ::LibXR::Detail::MSPM0UARTRxDmaStorage((rx_dma_storage)), (tx_queue_size),         \
      (rx_queue_capacity),                                                               \
      ::LibXR::MSPM0UART::BuildConfigFromSysCfg(name##_INST,                             \
                                                static_cast<uint32_t>(name##_BAUD_RATE))

}  // namespace LibXR
