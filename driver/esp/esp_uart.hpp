#pragma once

#include "esp_def.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "driver/gpio.h"
#include "double_buffer.hpp"
#include "esp_intr_alloc.h"
#include "flag.hpp"
#include "hal/uart_hal.h"
#include "hal/uart_types.h"
#include "soc/periph_defs.h"
#include "soc/soc_caps.h"
#include "uart.hpp"

#if SOC_GDMA_SUPPORTED && SOC_UHCI_SUPPORTED
#include "esp_private/gdma.h"
#include "esp_private/gdma_link.h"
#include "hal/uhci_hal.h"
#endif

namespace LibXR
{

/**
 * @brief ESP32 UART backend with FIFO and optional GDMA fast paths.
 * @brief ESP32 UART 后端，支持 FIFO 和可选 GDMA 快路径。
 *
 * The object owns the UART hardware state, software queue plumbing, and the
 * optional DMA resources used by the ESP-specific transmit and receive paths.
 * 该对象持有 UART 硬件状态、软件队列连接，以及 ESP 专用收发路径使用的可选
 * DMA 资源。
 */
class ESP32UART : public UART
{
 public:
  static constexpr int PIN_NO_CHANGE = -1;  ///< Sentinel for an unmapped GPIO.

  /**
   * @brief Create and initialize one ESP32 UART instance.
   * @brief 创建并初始化一个 ESP32 UART 实例。
   */
  ESP32UART(uart_port_t uart_num, int tx_pin, int rx_pin,
            int rts_pin = PIN_NO_CHANGE, int cts_pin = PIN_NO_CHANGE,
            size_t rx_buffer_size = 1024, size_t tx_buffer_size = 512,
            uint32_t tx_queue_size = 5,
            UART::Configuration config = {115200, UART::Parity::NO_PARITY, 8, 1},
            bool enable_dma = true);

  /**
   * @brief Apply a new UART framing and baud configuration.
   * @brief 应用新的 UART 帧格式和波特率配置。
   */
  ErrorCode SetConfig(UART::Configuration config) override;

  /**
   * @brief Toggle hardware loopback mode.
   * @brief 切换硬件环回模式。
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

  /**
   * @brief UART ISR trampoline.
   * @brief UART 中断跳板函数。
   */
  static void UartIsrEntry(void* arg);

#if SOC_GDMA_SUPPORTED && SOC_UHCI_SUPPORTED
  /**
   * @brief GDMA TX completion callback.
   * @brief GDMA TX 完成回调。
   */
  static bool DmaTxEofCallback(gdma_channel_handle_t dma_chan,
                               gdma_event_data_t* event_data,
                               void* user_data);

  /**
   * @brief GDMA TX descriptor error callback.
   * @brief GDMA TX 描述符错误回调。
   */
  static bool DmaTxDescrErrCallback(gdma_channel_handle_t dma_chan,
                                    gdma_event_data_t* event_data,
                                    void* user_data);

  /**
   * @brief GDMA RX completion callback.
   * @brief GDMA RX 完成回调。
   */
  static bool DmaRxDoneCallback(gdma_channel_handle_t dma_chan,
                                gdma_event_data_t* event_data,
                                void* user_data);

  /**
   * @brief GDMA RX descriptor error callback.
   * @brief GDMA RX 描述符错误回调。
   */
  static bool DmaRxDescrErrCallback(gdma_channel_handle_t dma_chan,
                                    gdma_event_data_t* event_data,
                                    void* user_data);
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

#if SOC_GDMA_SUPPORTED && SOC_UHCI_SUPPORTED
  /**
   * @brief Bring up the UHCI/GDMA backend.
   * @brief 拉起 UHCI/GDMA 后端。
   */
  ErrorCode InitDmaBackend();

  /**
   * @brief Start the active TX request over GDMA.
   * @brief 通过 GDMA 启动当前 active TX 请求。
   */
  bool StartDmaTx();

  /**
   * @brief Handle one RX DMA completion.
   * @brief 处理一次 RX DMA 完成事件。
   */
  void HandleDmaRxDone(gdma_event_data_t* event_data);

  /**
   * @brief Recover the RX DMA ring after an error.
   * @brief RX DMA 出错后恢复环形队列。
   */
  void HandleDmaRxError();

  /**
   * @brief Abort the TX DMA path and report failure.
   * @brief 中止 TX DMA 路径并上报失败。
   */
  void HandleDmaTxError();

  /**
   * @brief Push one RX DMA chunk into the software queue.
   * @brief 将一个 RX DMA chunk 推入软件队列。
   */
  void PushDmaRxData(size_t recv_size, bool in_isr);
#endif

  /**
   * @brief Try to start queued transmit work.
   * @brief 尝试启动排队的发送工作。
   */
  ErrorCode TryStartTx(bool in_isr);

  /**
   * @brief Load one active TX request from the queue.
   * @brief 从队列装载一个 active TX 请求。
   */
  bool LoadActiveTxFromQueue(bool in_isr);

  /**
   * @brief Preload one pending TX request for DMA mode.
   * @brief 为 DMA 模式预装一个 pending TX 请求。
   */
  bool LoadPendingTxFromQueue(bool in_isr);

  /**
   * @brief Promote pending DMA work when hardware becomes idle.
   * @brief 在硬件空闲时提升 pending DMA 工作。
   */
  bool StartPendingTxIfIdle(bool in_isr);

  /**
   * @brief Dequeue one TX payload into the selected buffer.
   * @brief 将一个 TX payload 出队到选定缓冲区。
   */
  bool DequeueTxToBuffer(uint8_t* buffer, size_t& size, WriteInfoBlock& info,
                         bool in_isr);

  /**
   * @brief Start the active request on the selected TX backend.
   * @brief 在选定 TX 后端上启动 active 请求。
   */
  bool StartActiveTransfer(bool in_isr);

  /**
   * @brief Start the active request and report queue completion ownership.
   * @brief 启动 active 请求并上报队列完成所有权交接。
   */
  bool StartAndReportActive(bool in_isr);

  /**
   * @brief Clear active TX state.
   * @brief 清除 active TX 状态。
   */
  void ClearActiveTx();

  /**
   * @brief Clear pending TX state.
   * @brief 清除 pending TX 状态。
   */
  void ClearPendingTx();

  /**
   * @brief Drain active TX bytes into the UART FIFO backend.
   * @brief 将 active TX 字节排入 UART FIFO 后端。
   */
  void FillTxFifo(bool in_isr);

  /**
   * @brief Push RX bytes into the software queue.
   * @brief 将 RX 字节推入软件队列。
   */
  void PushRxBytes(const uint8_t* data, size_t size, bool in_isr);

  /**
   * @brief Drain pending bytes from the hardware RX FIFO.
   * @brief 从硬件 RX FIFO 中取出待处理字节。
   */
  void DrainRxFifoFromIsr();

  /**
   * @brief Handle RX-side UART interrupt reasons.
   * @brief 处理 RX 侧 UART 中断原因。
   */
  void HandleRxInterrupt(uint32_t uart_intr_status);

  /**
   * @brief Handle TX-side UART interrupt reasons.
   * @brief 处理 TX 侧 UART 中断原因。
   */
  void HandleTxInterrupt(uint32_t uart_intr_status);

  /**
   * @brief Dispatch pending UART interrupt reasons.
   * @brief 分发待处理的 UART 中断原因。
   */
  void HandleUartInterrupt();

  /**
   * @brief Finalize one TX transfer result.
   * @brief 收尾一次 TX 传输结果。
   */
  void OnTxTransferDone(bool in_isr, ErrorCode result);

  uart_port_t uart_num_;  ///< Selected UART peripheral index.
  int tx_pin_;            ///< TX GPIO pin or `PIN_NO_CHANGE`.
  int rx_pin_;            ///< RX GPIO pin or `PIN_NO_CHANGE`.
  int rts_pin_;           ///< RTS GPIO pin or `PIN_NO_CHANGE`.
  int cts_pin_;           ///< CTS GPIO pin or `PIN_NO_CHANGE`.

  UART::Configuration config_;  ///< Current UART framing configuration.

  uint8_t* rx_isr_buffer_ = nullptr;  ///< Scratch buffer used by the RX ISR path.
  size_t rx_isr_buffer_size_ = 0;     ///< Size of `rx_isr_buffer_`.

  uint8_t* tx_storage_ = nullptr;      ///< Backing storage for the TX half-buffers.
  DoubleBuffer tx_dma_buffer_{};       ///< TX double-buffer view for the DMA path.
  WriteInfoBlock tx_active_info_ = {}; ///< Metadata for the active TX request.
  size_t tx_active_length_ = 0U;       ///< Active TX payload length in bytes.
  size_t tx_active_offset_ = 0U;       ///< Bytes already emitted for the active request.
  bool tx_active_valid_ = false;       ///< Whether the active TX metadata is valid.
  Flag::Plain tx_busy_;                ///< Hardware TX engine currently owns the request.
  Flag::Plain in_tx_isr_;              ///< Reentry guard while processing TX ISR work.

  bool uart_hw_enabled_ = false;            ///< UART hardware block was initialized.
  uart_hal_context_t uart_hal_ = {};        ///< ESP-IDF UART HAL context.
  intr_handle_t uart_intr_handle_ = nullptr;  ///< Registered UART interrupt handle.
  bool uart_isr_installed_ = false;         ///< UART ISR installation state.
  bool dma_requested_ = true;               ///< Constructor preference for DMA mode.

  ReadPort _read_port;   ///< Read-side queue bridge exposed to `UART`.
  WritePort _write_port; ///< Write-side queue bridge exposed to `UART`.

#if SOC_GDMA_SUPPORTED && SOC_UHCI_SUPPORTED
  bool dma_backend_enabled_ = false;   ///< UHCI/GDMA backend is active.
  uhci_hal_context_t uhci_hal_ = {};   ///< UHCI HAL context bound to this UART.
  gdma_channel_handle_t tx_dma_channel_ = nullptr;  ///< TX GDMA channel handle.
  gdma_channel_handle_t rx_dma_channel_ = nullptr;  ///< RX GDMA channel handle.
  uintptr_t tx_dma_head_addr_[2] = {0U, 0U};   ///< TX list head addresses.
  gdma_link_list_handle_t rx_dma_link_ = nullptr;  ///< RX GDMA ring list.
  uint8_t* rx_dma_storage_ = nullptr;            ///< RX DMA ring backing storage.
  size_t rx_dma_chunk_size_ = 0;                 ///< Size of one RX DMA ring node.
  uint32_t rx_dma_node_index_ = 0;               ///< Software consumer index in the RX ring.
#endif
};

}  // namespace LibXR
