#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "driver/gpio.h"
#include "esp_def.hpp"
#include "esp_idf_version.h"
#include "esp_intr_alloc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "hal/uart_hal.h"
#include "hal/uart_types.h"
#include "model/uart_dma_tx_model.hpp"
#include "model/uart_execution_policy.hpp"
#include "model/uart_rx_config_gate.hpp"
#include "soc/periph_defs.h"
#include "soc/soc_caps.h"
#include "uart.hpp"

#if defined(SOC_AHB_GDMA_SUPPORTED) && SOC_AHB_GDMA_SUPPORTED && \
    defined(SOC_UHCI_SUPPORTED) && SOC_UHCI_SUPPORTED
#define LIBXR_ESP_UART_HAS_AHB_GDMA 1
#include "esp_private/gdma.h"
#include "esp_private/gdma_link.h"
#include "hal/dma_types.h"
#include "hal/gdma_hal.h"
#include "hal/uhci_hal.h"
#else
#define LIBXR_ESP_UART_HAS_AHB_GDMA 0
#endif

namespace LibXR
{

class ESP32UART;

#if (SOC_CPU_CORES_NUM > 1) && \
    (!defined(CONFIG_FREERTOS_UNICORE) || !CONFIG_FREERTOS_UNICORE)
inline constexpr bool ESP_UART_USES_IRQ_SERIALIZATION = true;
#else
inline constexpr bool ESP_UART_USES_IRQ_SERIALIZATION = false;
#endif

/** CONFIG-only gate used when the SMP raw-IRQ owner already serializes RX. */
class ESP32UARTConfigOnlyGate
{
 public:
  [[nodiscard]] bool TryReserveConfig() { return config_gate_.TryReserveConfig(); }
  void PublishConfig() { config_gate_.PublishConfig(); }
  [[nodiscard]] bool TryEnterRx() { return true; }
  [[nodiscard]] bool LeaveRx() { return false; }
  [[nodiscard]] bool TryEnterConfig() { return config_gate_.TryEnterConfig(); }
  void LeaveConfig() { config_gate_.LeaveConfig(); }
  [[nodiscard]] bool TryEnterRecovery() { return true; }
  void LeaveRecovery() {}

 private:
  UartRxConfigGate config_gate_;
};

using ESP32UARTRxConfigGate =
    std::conditional_t<ESP_UART_USES_IRQ_SERIALIZATION, ESP32UARTConfigOnlyGate,
                       UartRxConfigGate>;

#if defined(CONFIG_APPTRACE_SV_ENABLE) && CONFIG_APPTRACE_SV_ENABLE
static_assert(!ESP_UART_USES_IRQ_SERIALIZATION,
              "ESP UART SMP IRQ serialization is incompatible with the ESP-IDF "
              "SystemView interrupt wrapper");
#endif

#if defined(CONFIG_ESP_TRACE_ENABLE) && CONFIG_ESP_TRACE_ENABLE
static_assert(!ESP_UART_USES_IRQ_SERIALIZATION,
              "ESP UART SMP IRQ serialization requires direct non-shared ISR handlers");
#endif

class ESP32UARTIrqAdapter
{
 public:
  explicit ESP32UARTIrqAdapter(ESP32UART& owner) : owner_(owner) {}

  void LockAndMaskIrqDomain() noexcept;
  void UnlockIrqDomain() noexcept;
  void LockIrqDomain() noexcept;
  void RestoreAndUnlockIrqDomain() noexcept;

 private:
  ESP32UART& owner_;
};

template <bool UseIrqSerialization>
class ESP32UARTExecutionPolicyStorage;

template <>
class ESP32UARTExecutionPolicyStorage<false>
{
 public:
  explicit ESP32UARTExecutionPolicyStorage(ESP32UART&) {}

  template <typename Handler>
  bool Invoke(uint32_t events, Handler&& handler) noexcept
  {
    return policy_.Invoke(events, std::forward<Handler>(handler));
  }

  template <typename Handler>
  bool InvokeConfig(uint32_t events, bool in_isr, Handler&& handler) noexcept
  {
    return policy_.InvokeConfig(events, in_isr, std::forward<Handler>(handler));
  }

  template <typename Source, typename Handler>
  bool InvokeIrq(Source&& source, Handler&& handler) noexcept
  {
    return policy_.InvokeIrq(std::forward<Source>(source),
                             std::forward<Handler>(handler));
  }

 private:
  UartDirectPolicy policy_{};
};

template <>
class ESP32UARTExecutionPolicyStorage<true>
{
 public:
  explicit ESP32UARTExecutionPolicyStorage(ESP32UART& owner)
      : adapter_(owner), policy_(adapter_)
  {
  }

  template <typename Handler>
  bool Invoke(uint32_t events, Handler&& handler) noexcept
  {
    return policy_.Invoke(events, std::forward<Handler>(handler));
  }

  template <typename Handler>
  bool InvokeConfig(uint32_t events, bool in_isr, Handler&& handler) noexcept
  {
    return policy_.InvokeConfig(events, in_isr, std::forward<Handler>(handler));
  }

  template <typename Source, typename Handler>
  bool InvokeIrq(Source&& source, Handler&& handler) noexcept
  {
    return policy_.InvokeIrq(std::forward<Source>(source),
                             std::forward<Handler>(handler));
  }

 private:
  ESP32UARTIrqAdapter adapter_;
  UartIrqSerializedPolicy<ESP32UARTIrqAdapter> policy_;
};

using ESP32UARTExecutionPolicy =
    ESP32UARTExecutionPolicyStorage<ESP_UART_USES_IRQ_SERIALIZATION>;

/**
 * @brief ESP32 UART 读端口 / ESP32 UART read port
 *
 * 当上层从软件 RX 队列成功出队后，该读端口会回调所属 UART 后端继续尝试排空
 * 硬件 FIFO。
 * After software dequeues bytes from the RX queue, this read port calls back
 * into the owning UART backend so the hardware FIFO can be drained again.
 */
class ESP32UARTReadPort : public ReadPort
{
 public:
  /**
   * @brief 构造读端口 / Construct the read port
   *
   * @param size RX 队列容量（字节） / RX queue capacity in bytes
   * @param owner 所属 UART 后端 / Owning UART backend
   */
  explicit ESP32UARTReadPort(size_t size, ESP32UART& owner)
      : ReadPort(size), owner_(owner)
  {
  }

  /**
   * @brief 软件队列出队后的回调 / Callback after software RX dequeue
   */
  void OnRxDequeue(bool in_isr) override;

  ESP32UARTReadPort& operator=(ReadFun fun)
  {
    ReadPort::operator=(fun);
    return *this;
  }

 private:
  ESP32UART& owner_;  ///< 所属 UART 后端 / Owning UART backend
};

/**
 * @brief ESP32 UART backend with FIFO and optional GDMA fast paths.
 * @brief ESP32 UART 后端，支持 FIFO 和可选 GDMA 快路径。
 *
 * The object owns the UART hardware state, software queue plumbing, and the
 * optional DMA resources used by the ESP-specific transmit and receive paths.
 * 该对象持有 UART 硬件状态、软件队列连接，以及 ESP 专用收发路径使用的可选
 * DMA 资源。
 *
 * GDMA interrupts are deliberately registered as non-IRAM interrupts. The complete
 * LibXR service and user-callback chain is safe in an ordinary ISR, but is not required
 * to reside in IRAM for cache-disabled execution. ESP-IDF therefore defers these
 * interrupts while flash cache is disabled and services their latched level status
 * after cache access resumes.
 */
class ESP32UART : public UART
{
  friend class ESP32UARTReadPort;
  friend class ESP32UARTIrqAdapter;
  friend class UartDmaTxModel<ESP32UART>;

 public:
  static constexpr int PIN_NO_CHANGE = -1;  ///< Sentinel for an unmapped GPIO.

  /**
   * @brief Create and initialize one ESP32 UART instance.
   * @brief 创建并初始化一个 ESP32 UART 实例。
   */
  ESP32UART(uart_port_t uart_num, int tx_pin, int rx_pin, int rts_pin = PIN_NO_CHANGE,
            int cts_pin = PIN_NO_CHANGE, size_t rx_buffer_size = 1024,
            size_t tx_buffer_size = 512, uint32_t tx_queue_size = 5,
            UART::Configuration config = {115200, UART::Parity::NO_PARITY, 8, 1},
            bool enable_dma = true);

  /**
   * @brief Apply a new UART framing and baud configuration.
   * @brief 应用新的 UART 帧格式和波特率配置。
   * @return `BUSY` while an earlier request is outstanding, or `NOT_SUPPORT` when the
   * FIFO backend is active.
   */
  ErrorCode SetConfig(UART::Configuration config) override;

  /**
   * @brief Toggle UART peripheral internal loopback mode.
   * @brief 切换 UART 外设内部环回模式。
   * @warning Setup/self-test API. The caller must quiesce concurrent UART traffic and
   *          configuration before changing this direct peripheral bit.
   */
  ErrorCode SetLoopback(bool enable);

  /**
   * @brief Queue-driven TX entry used by `WritePort`.
   * @brief `WritePort` 使用的队列驱动 TX 入口。
   */
  static ErrorCode WriteFun(WritePort& port, bool in_isr);

  /**
   * @brief Queue-driven RX entry used by `ReadPort`.
   * @brief `ReadPort` 使用的队列驱动 RX 入口。
   */
  static ErrorCode ReadFun(ReadPort& port, bool in_isr);

 private:
  /**
   * @brief Allocate DMA-capable backing storage for TX buffers.
   * @brief 为 TX buffer 分配可 DMA 访问的 backing storage。
   */
  static uint8_t* AllocateTxStorage(size_t size);

  /**
   * @brief Map one UART index to its peripheral module.
   * @brief 将一个 UART 序号映射到对应的外设模块。
   */
  static ErrorCode ResolveUartPeriph(uart_port_t uart_num, periph_module_t& out);

  /**
   * @brief Convert configured data bits into the HAL enum.
   * @brief 将配置的数据位转换为 HAL 枚举。
   */
  static bool ResolveWordLength(uint8_t data_bits, uart_word_length_t& out);

  /**
   * @brief Convert configured stop bits into the HAL enum.
   * @brief 将配置的停止位转换为 HAL 枚举。
   */
  static bool ResolveStopBits(uint8_t stop_bits, uart_stop_bits_t& out);

  /**
   * @brief Convert configured parity into the HAL enum.
   * @brief 将配置的校验位转换为 HAL 枚举。
   */
  static uart_parity_t ResolveParity(UART::Parity parity);

  UartDmaControlResult ApplyPendingConfig(bool in_isr);
  void ReleaseConfigAdmission(bool in_isr);

  bool ApplyConfigPayload(UART::Configuration config);

  static bool IsCurrentTaskPinned();

  void SetIrqDomainEnabled(bool enabled) noexcept;
  void SetIrqDomainEnabledLocked(bool enabled) noexcept;

  /**
   * @brief UART ISR trampoline.
   * @brief UART 中断跳板函数。
   */
  static void UartIsrEntry(void* arg);

#if LIBXR_ESP_UART_HAS_AHB_GDMA
  static constexpr uint32_t DMA_UART_ERROR_INTR_MASK =
      UART_INTR_PARITY_ERR | UART_INTR_FRAM_ERR | UART_INTR_RXFIFO_OVF;

  /**
   * @brief GDMA TX completion callback.
   * @brief GDMA TX 完成回调。
   */
  static void DmaTxIsrEntry(void* arg);

  /**
   * @brief GDMA RX interrupt entry.
   * @brief GDMA RX 中断入口。
   */
  static void DmaRxIsrEntry(void* arg);

#endif

  /**
   * @brief Initialize UART hardware and base HAL state.
   * @brief 初始化 UART 硬件和基础 HAL 状态。
   */
  ErrorCode InitUartHardware();

  /**
   * @brief Configure the selected GPIO pins.
   * @brief 配置选定的 GPIO 引脚。
   */
  ErrorCode ConfigurePins();

  /**
   * @brief Install the UART interrupt handler.
   * @brief 安装 UART 中断处理函数。
   */
  ErrorCode InstallUartIsr();

  /**
   * @brief Program RX interrupt thresholds and masks.
   * @brief 配置 RX 中断阈值和掩码。
   */
  void ConfigureRxInterruptPath();

  /**
   * @brief Start one TX request through GDMA.
   * @param data DMA-readable payload buffer.
   * @param size Payload size in bytes.
   * @param block Double-buffer block and descriptor-list index.
   * @return Whether the transfer started or failed terminally.
   */
  UartDmaTxStartResult StartDmaTx(uint8_t* data, size_t size, int block);

#if LIBXR_ESP_UART_HAS_AHB_GDMA
  /**
   * @brief Bring up the UHCI/GDMA backend.
   * @brief 拉起 UHCI/GDMA 后端。
   */
  ErrorCode InitDmaBackend();

  void ConfigureDmaErrorInterruptPath();

  /**
   * @brief Drain all completed RX descriptors visible from the software cursor.
   * @brief 从软件游标开始排空所有已完成的 RX 描述符。
   */
  bool DrainCompletedDmaRxDescriptors(bool& pushed_any);

  /**
   * @brief Recover both DMA data paths after a unified error.
   * @brief 统一错误后恢复 TX/RX DMA 数据路径。
   */
  UartDmaControlResult RecoverDataPath(bool in_isr);

  uint32_t ServiceDmaTxStatus(bool in_isr);
  uint32_t ServiceDmaRxStatus(bool& pushed_any);
  uint32_t ServiceDmaUartStatus(bool in_isr);
  bool ResetAndRestartRxDma();
#endif

  /**
   * @brief Submit queued work to the FIFO backend's serialized service.
   */
  ErrorCode SubmitFifoTx(bool in_isr);

  struct FifoSubmitContext
  {
    ErrorCode result = ErrorCode::PENDING;
    bool resolved = false;
  };

  static constexpr uint32_t FIFO_EVENT_WRITE = 1U << 0U;
  static constexpr uint32_t FIFO_EVENT_IRQ = 1U << 1U;
  static constexpr uint32_t FIFO_EVENT_RX_DRAIN = 1U << 2U;

  uint32_t ServiceFifo(uint32_t events, bool in_isr, FifoSubmitContext* submit,
                       bool& pushed_any) noexcept;

  uint32_t ServiceFifoIrqSource(bool in_isr) noexcept;

  bool StartNextFifoTx(bool in_isr, FifoSubmitContext* submit);

  /**
   * @brief Clear active TX state.
   * @brief 清除 active TX 状态。
   */
  void ClearActiveTx();

  /**
   * @brief Drain active TX bytes into the UART FIFO backend.
   * @brief 将 active TX 字节排入 UART FIFO 后端。
   */
  void FillTxFifo(bool in_isr);

  /**
   * @brief Push RX bytes into the software queue.
   * @brief 将 RX 字节推入软件队列。
   */
  bool PushRxBytes(const uint8_t* data, size_t size);

  /**
   * @brief Drain pending bytes from the hardware RX FIFO.
   * @brief 从硬件 RX FIFO 中取出待处理字节。
   */
  [[nodiscard]] bool DrainRxFifo(bool in_isr);

  uart_port_t uart_num_;  ///< Selected UART peripheral index.
  int tx_pin_;            ///< TX GPIO pin or `PIN_NO_CHANGE`.
  int rx_pin_;            ///< RX GPIO pin or `PIN_NO_CHANGE`.
  int rts_pin_;           ///< RTS GPIO pin or `PIN_NO_CHANGE`.
  int cts_pin_;           ///< CTS GPIO pin or `PIN_NO_CHANGE`.

  UART::Configuration config_;  ///< Current UART framing configuration.
  UART::Configuration requested_config_;
  uint32_t uart_sclk_hz_ = 0U;
  portMUX_TYPE irq_domain_lock_ = portMUX_INITIALIZER_UNLOCKED;
  ESP32UARTExecutionPolicy execution_policy_;
  [[no_unique_address]] ESP32UARTRxConfigGate rx_config_gate_;

  uint8_t* tx_storage_ = nullptr;  ///< Backing storage for the TX half-buffers.
  size_t tx_active_length_ = 0U;   ///< Active TX payload length in bytes.
  size_t tx_active_offset_ = 0U;   ///< Bytes already emitted for the active request.
  bool tx_active_valid_ = false;   ///< Whether the active TX metadata is valid.

  bool uart_hw_enabled_ = false;              ///< UART hardware block was initialized.
  uart_hal_context_t uart_hal_ = {};          ///< ESP-IDF UART HAL context.
  intr_handle_t uart_intr_handle_ = nullptr;  ///< Registered UART interrupt handle.
  bool uart_isr_installed_ = false;           ///< UART ISR installation state.
  bool dma_requested_ = true;                 ///< Constructor preference for DMA mode.

  ESP32UARTReadPort _read_port;  ///< Read-side queue bridge exposed to `UART`.
  WritePort _write_port;         ///< Write-side queue bridge exposed to `UART`.
  UartDmaTxModel<ESP32UART> tx_dma_model_;  ///< One-shot DMA TX execution model.

#if LIBXR_ESP_UART_HAS_AHB_GDMA
  bool dma_backend_enabled_ = false;  ///< UHCI/GDMA backend is active.
  uhci_hal_context_t uhci_hal_ = {};  ///< UHCI HAL context bound to this UART.
  gdma_channel_handle_t tx_dma_channel_ = nullptr;  ///< TX GDMA channel handle.
  gdma_channel_handle_t rx_dma_channel_ = nullptr;  ///< RX GDMA channel handle.
  uintptr_t tx_dma_head_addr_[2] = {0U, 0U};        ///< TX list head addresses.
  gdma_link_list_handle_t rx_dma_link_ = nullptr;   ///< RX GDMA ring list.
  uintptr_t rx_dma_head_addr_ = 0U;                 ///< RX list head address.
  dma_descriptor_t* rx_dma_descriptors_ = nullptr;  ///< Non-cache RX descriptor view.
  uint8_t* rx_dma_storage_ = nullptr;               ///< RX DMA ring backing storage.
  size_t rx_dma_chunk_size_ = 0;                    ///< Size of one RX DMA ring node.
  size_t rx_dma_buffer_alignment_ = 1;              ///< Alignment used for RX buffers.
  uint32_t rx_dma_node_index_ = 0;  ///< Software consumer index in the RX ring.
  gdma_hal_context_t tx_gdma_hal_ = {};
  int tx_gdma_group_id_ = -1;
  int tx_gdma_channel_id_ = -1;
  intr_handle_t tx_gdma_intr_handle_ = nullptr;
  gdma_hal_context_t rx_gdma_hal_ = {};
  int rx_gdma_group_id_ = -1;
  int rx_gdma_channel_id_ = -1;
  intr_handle_t rx_gdma_intr_handle_ = nullptr;
#endif
};

}  // namespace LibXR
