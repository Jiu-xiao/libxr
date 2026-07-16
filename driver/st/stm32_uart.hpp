#pragma once

#include <atomic>
#include <cstdint>

#include "main.h"

#ifdef HAL_UART_MODULE_ENABLED

#if defined(DMA_IT_SUSP) && defined(DMA_FLAG_SUSP)
#error \
    "LibXR STM32UART does not support H5/U5 GPDMA; circular RX requires a dedicated linked-list backend"
#endif

#ifdef UART
#undef UART
#endif

#include "latest_snapshot.hpp"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "model/uart_circular_dma_rx_model.hpp"
#include "model/uart_dma_abort_join.hpp"
#include "model/uart_dma_tx_model.hpp"
#include "model/uart_hardware_gate.hpp"
#include "stm32_dcache.hpp"
#include "uart.hpp"

typedef enum : uint8_t
{
#ifdef USART1
  STM32_USART1,
#endif
#ifdef USART2
  STM32_USART2,
#endif
#ifdef USART3
  STM32_USART3,
#endif
#ifdef USART4
  STM32_USART4,
#endif
#ifdef USART5
  STM32_USART5,
#endif
#ifdef USART6
  STM32_USART6,
#endif
#ifdef USART7
  STM32_USART7,
#endif
#ifdef USART8
  STM32_USART8,
#endif
#ifdef USART9
  STM32_USART9,
#endif
#ifdef USART10
  STM32_USART10,
#endif
#ifdef USART11
  STM32_USART11,
#endif
#ifdef USART12
  STM32_USART12,
#endif
#ifdef USART13
  STM32_USART13,
#endif
#ifdef UART1
  STM32_UART1,
#endif
#ifdef UART2
  STM32_UART2,
#endif
#ifdef UART3
  STM32_UART3,
#endif
#ifdef UART4
  STM32_UART4,
#endif
#ifdef UART5
  STM32_UART5,
#endif
#ifdef UART6
  STM32_UART6,
#endif
#ifdef UART7
  STM32_UART7,
#endif
#ifdef UART8
  STM32_UART8,
#endif
#ifdef UART9
  STM32_UART9,
#endif
#ifdef UART10
  STM32_UART10,
#endif
#ifdef UART11
  STM32_UART11,
#endif
#ifdef UART12
  STM32_UART12,
#endif
#ifdef UART13
  STM32_UART13,
#endif
#ifdef LPUART1
  STM32_LPUART1,
#endif
#ifdef LPUART2
  STM32_LPUART2,
#endif
#ifdef LPUART3
  STM32_LPUART3,
#endif
  STM32_UART_NUMBER,
  STM32_UART_ID_ERROR
} stm32_uart_id_t;

stm32_uart_id_t stm32_uart_get_id(USART_TypeDef* addr);

namespace LibXR
{
/**
 * @brief BSP-owned control of one STM32 UART normal IRQ domain.
 *
 * The driver stores this descriptor by value but does not own `context`. The descriptor
 * and the pointed-to context must remain valid for the `STM32UART` lifetime. All hooks
 * are non-blocking and may be called with interrupts active. This backend covers STM32
 * DMA Stream, DMA Channel, and H7 BDMA. H5/U5 GPDMA is rejected at compile time because
 * its circular receive path requires a dedicated linked-list backend.
 *
 * `mask_normal` and `restore_normal` run only on the designated IRQ core. They control
 * the complete normal UART, RX-DMA, and TX-DMA NVIC domain for this instance. Masking
 * must be complete before returning. Restoring reinstates the BSP's authoritative enable
 * set; during CONFIG abort, only explicitly armed Stream terminal sources can produce
 * fresh work. An exclusively owned vector may clear stale NVIC pending state before
 * restore. Shared or overlapping vectors require BSP-owned claim/distributor arbitration:
 * one UART must not blindly clear, disable, or restore another user's vector.
 *
 * `kick_target` may run on any core. If already on the designated IRQ core and no handler
 * is active, it may call `STM32UART::IrqDomainHandler()` inline. Cross-core and nested
 * calls must instead non-blockingly schedule it through a software IRQ or IPI. Handler
 * calls for one UART must never recursively reenter or overlap. A stale coalesced kick is
 * harmless; a request-word transition from idle to SCHEDULED must always start an inline
 * call or leave one later invocation guaranteed.
 * The kick source itself must remain usable while the normal UART/DMA domain is masked.
 * Repeated kicks may be coalesced, but at least one later call is required after every
 * transition from no scheduled call to scheduled work.
 */
struct STM32UartIrqDomainOps
{
  using Hook = void (*)(void* context) noexcept;

  void* context = nullptr;
  Hook mask_normal = nullptr;
  Hook restore_normal = nullptr;
  Hook kick_target = nullptr;

  [[nodiscard]] bool IsValid() const noexcept
  {
    return (mask_normal != nullptr) && (restore_normal != nullptr) &&
           (kick_target != nullptr);
  }
};

/**
 * @brief STM32 UART driver implementation.
 *
 * The UART global IRQ and associated RX/TX DMA IRQs must be dispatched through
 * `STM32_UART_IRQHandler()` and `STM32_UART_DMA_IRQHandler()` rather than calling HAL
 * handlers directly. Sources for one UART must share the target core and preemption
 * priority. The `UartHardwareGate` serializes HAL access and keeps CONFIG ownership
 * across the asynchronous DMA-abort join. Construction requires BSP IRQ-domain hooks;
 * there is no silent single-core or no-op fallback. During CONFIG, DMA `EN == 0` is the
 * only quiescence proof. The armed stop IRQ path does not dispatch HAL callbacks, so a
 * stale circular-RX terminal flag cannot complete the join while DMA is still enabled.
 * DMA Channel and BDMA `HAL_DMA_Abort_IT()` must return with `EN == 0`; F4/H7 DMA Stream
 * asynchronously clears EN and must eventually assert a fresh TC/TE/DME/FE carrier after
 * stale flags are cleared. `ASYNC_STOP_ARMED` is published only after all carrier-enable
 * register writes are complete.
 *
 * On UART IPs with `TEACK`/`REACK`, CONFIG deliberately does not poll those flags. The
 * STM32 reference sequence permits `DMAT`/`DMAR` to be armed before the corresponding
 * enable acknowledgement, so the DMA setup remains non-blocking. The BSP must keep the
 * UART kernel clock running and guarantee that an enabled transmitter/receiver is
 * eventually acknowledged. Bytes arriving during this destructive CONFIG transition
 * may be discarded; `SetConfig()` success does not mean that software synchronously
 * observed the acknowledgement.
 *
 * H745/H747-class M7/M4 firmware must assign one owner core to the complete C++ UART
 * object. The owner core executes every direct `Read()`, `Write()`, `SetConfig()`, UART
 * IRQ, RX-DMA IRQ, and TX-DMA IRQ call. The other core must use a BSP IPC facade rather
 * than dereference this polymorphic object or its queue/operation pointers. This avoids
 * relying on cache coherence, identical vtable/function addresses, or one shared C++
 * allocator across two independently linked images. All three IRQ wrappers execute on
 * the owner core at the same preemption priority; the other core does not enable them.
 *
 * The BSP keeps the complete normal IRQ domain masked from before construction until
 * `SetRxDMA()` has returned, `map` is published, and any hook-context back-pointer has
 * been bound. It may restore the domain only after the constructor has returned.
 */
class STM32UART : public UART
{
  friend class UartCircularDmaRxModel;
  friend class UartDmaTxModel<STM32UART>;
  friend void STM32_UART_DMA_IRQHandler(DMA_HandleTypeDef* dma_handle);
  friend void STM32_UART_IRQHandler(UART_HandleTypeDef* uart_handle);
  friend void ::HAL_UARTEx_RxEventCallback(UART_HandleTypeDef* huart, uint16_t size);
  friend void ::HAL_UART_TxCpltCallback(UART_HandleTypeDef* huart);
  friend void ::HAL_UART_ErrorCallback(UART_HandleTypeDef* huart);

 public:
  static ErrorCode WriteFun(WritePort& port, bool in_isr);

  static ErrorCode ReadFun(ReadPort& port, bool in_isr);

  /**
   * @brief 构造 UART 对象 / Construct UART object
   */
  STM32UART(UART_HandleTypeDef* uart_handle, RawData dma_buff_rx, RawData dma_buff_tx,
            STM32UartIrqDomainOps irq_domain_ops, uint32_t tx_queue_size = 5);

  ErrorCode SetConfig(UART::Configuration config);

  void SetRxDMA();
  void OnRxDataAvailable(bool in_isr);
  void DmaIRQHandler(DMA_HandleTypeDef* dma_handle);
  void UartIRQHandler();

  /**
   * @brief Service coalesced deferred-scan and NVIC-restore work on the IRQ target core.
   *
   * The BSP calls this only as the continuation scheduled by `kick_target`. It may run
   * in an ISR or an existing target-core execution point; the driver does not create or
   * require a worker thread. Calls for one UART must not overlap or recursively reenter.
   */
  void IrqDomainHandler();

  ReadPort _read_port;
  WritePort _write_port;

  LatestSnapshot<UART::Configuration> requested_config_;
  UartHardwareGate hardware_gate_;
  UartCircularDmaRxModel rx_dma_model_;
  UartDmaTxModel<STM32UART> tx_dma_model_;
  UartDmaAbortJoin abort_join_;
  std::atomic<uint32_t> rx_admission_{1U};

  UART_HandleTypeDef* uart_handle_;

  stm32_uart_id_t id_ = STM32_UART_ID_ERROR;

  static STM32UART* map[STM32_UART_NUMBER];  // NOLINT

 private:
  enum class CallbackDispatch : uint32_t
  {
    NONE = 0U,
    NORMAL = 1U,
  };

  enum class IrqDomainRequest : uint32_t
  {
    NONE = 0U,
    DEFERRED_SCAN = 1U << 0U,
    RESTORE_NORMAL = 1U << 1U,
  };

  static constexpr uint32_t IRQ_DOMAIN_SCHEDULED = 1U << 31U;
  static constexpr uint32_t IRQ_DOMAIN_REQUEST_MASK =
      static_cast<uint32_t>(IrqDomainRequest::DEFERRED_SCAN) |
      static_cast<uint32_t>(IrqDomainRequest::RESTORE_NORMAL);

  class CallbackScope
  {
   public:
    CallbackScope(STM32UART& uart, UartHardwareGate::OwnerContext* context,
                  CallbackDispatch dispatch, DMA_HandleTypeDef* dma);
    ~CallbackScope();

    CallbackScope(const CallbackScope&) = delete;
    CallbackScope& operator=(const CallbackScope&) = delete;

   private:
    STM32UART& uart_;
  };

  STM32UartIrqDomainOps irq_domain_ops_;
  // Request bits and the scheduled marker share one atomic word. Publishers set both
  // before deciding whether to kick; the handler keeps SCHEDULED set while it drains a
  // snapshot and clears it with a release CAS only after processing that snapshot.
  std::atomic<uint32_t> irq_domain_state_{0U};
  CallbackDispatch callback_dispatch_{CallbackDispatch::NONE};
  UartHardwareGate::OwnerContext* callback_context_ = nullptr;
  DMA_HandleTypeDef* callback_dma_ = nullptr;

  void OnConfigRequested() { hardware_gate_.RequestConfig(); }

  [[nodiscard]] bool ConfigRequested() const { return hardware_gate_.ConfigRequested(); }

  bool ApplyPendingConfig(bool in_isr);

  bool OnConfigApplied(bool in_isr);

  [[nodiscard]] UartDmaAbortJoin::Direction DmaDirection(
      DMA_HandleTypeDef* dma_handle) const;

  [[nodiscard]] bool DmaIsStopped(DMA_HandleTypeDef* dma_handle) const;

  void DisableUartInterrupts();

  static void DisableDmaInterrupts(DMA_HandleTypeDef* dma_handle);

  static void EnableDmaAbortInterrupt(DMA_HandleTypeDef* dma_handle);

  static void ClearDmaFlags(DMA_HandleTypeDef* dma_handle);

  void FinalizeStoppedDma(DMA_HandleTypeDef* dma_handle, bool in_isr);

  static void ReadBackDmaControl(DMA_HandleTypeDef* dma_handle);

  void SynchronizeDisabledInterrupts();

  [[nodiscard]] bool LaunchDmaAbort(DMA_HandleTypeDef* dma_handle,
                                    UartDmaAbortJoin::Direction direction, bool in_isr);

  [[nodiscard]] bool ArmAsyncStop(DMA_HandleTypeDef* dma_handle,
                                  UartDmaAbortJoin::Direction direction, bool in_isr);

  [[nodiscard]] bool AnyAsyncStopArmed() const;

  void PublishIrqDomainRequest(IrqDomainRequest request);

  void DeferNormalIrq();

  void DispatchNormalDmaIrq(DMA_HandleTypeDef* dma_handle,
                            UartHardwareGate::OwnerContext& context);

  void DispatchNormalUartIrq(UartHardwareGate::OwnerContext& context);

  void ScanDeferredIrqDomain(UartHardwareGate::OwnerContext& context);

  /**
   * @brief 通过 STM32 HAL 配置并启动 UART 循环 RX DMA / Configure
   * and start circular UART RX DMA through STM32 HAL
   * @param data DMA 可写的接收缓冲区 / DMA-writable receive buffer
   * @param size 接收缓冲区字节数 / Receive buffer capacity in bytes
   */
  void StartCircularDmaRx(uint8_t* data, size_t size)
  {
    uart_handle_->hdmarx->Init.Mode = DMA_CIRCULAR;
    DEV_ASSERT(HAL_DMA_Init(uart_handle_->hdmarx) == HAL_OK);
    DEV_ASSERT(HAL_UARTEx_ReceiveToIdle_DMA(uart_handle_, data, size) == HAL_OK);
  }

  /**
   * @brief 获取 STM32 RX DMA 剩余传输计数 / Get the STM32 RX DMA remaining count
   * @return DMA 尚未写入的字节数 / Number of bytes not yet written by DMA
   */
  [[nodiscard]] size_t GetCircularDmaRxRemaining() const
  {
    return __HAL_DMA_GET_COUNTER(uart_handle_->hdmarx);
  }

  /**
   * @brief 使循环 DMA RX 数据对 CPU 可见 / Make circular DMA RX data
   * visible to the CPU
   * @param data DMA 接收缓冲区起始地址 / DMA receive buffer start address
   * @param size 接收缓冲区字节数 / Receive buffer capacity in bytes
   */
  void PrepareCircularDmaRxForCpu(uint8_t* data, size_t size)
  {
    STM32_InvalidateDCacheByAddr(data, size);
  }

  /**
   * @brief 通过 STM32 HAL 启动一个 active UART TX DMA 载荷 / Start
   * one active UART TX DMA payload through STM32 HAL
   * @param data DMA 可读的载荷缓冲区 / DMA-readable payload buffer
   * @param size 载荷字节数 / Payload size in bytes
   * @param block active 双缓冲块索引，STM32 HAL 不使用 / Active double-buffer
   * block index, unused by STM32 HAL
   * @return HAL 接受 DMA 传输时返回 true / True when HAL accepts the DMA transfer
   */
  UartDmaTxStartResult StartDmaTx(uint8_t* data, size_t size, int block,
                                  UartHardwareGate::OwnerContext* hardware_context);

  void DispatchHardwareActions(UartHardwareGate::PendingAction actions, bool in_isr);
  bool ApplyConfigPayload(bool in_isr);
};

/**
 * @brief Dispatch one STM32 UART DMA IRQ through the libxr TX/config gate.
 *
 * BSP DMA IRQ handlers associated with a libxr UART must call this function instead of
 * calling `HAL_DMA_IRQHandler()` directly. The gate is entered before HAL reads or
 * clears DMA status.
 */
void STM32_UART_DMA_IRQHandler(DMA_HandleTypeDef* dma_handle);

/**
 * @brief Dispatch one STM32 UART global IRQ through the libxr TX/config gate.
 *
 * BSP UART IRQ handlers associated with a libxr UART must call this function instead of
 * calling `HAL_UART_IRQHandler()` directly.
 */
void STM32_UART_IRQHandler(UART_HandleTypeDef* uart_handle);

}  // namespace LibXR

#endif
