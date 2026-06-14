#include "esp_uart.hpp"

#include <algorithm>
#include <cstring>
#include <new>

#include "esp_attr.h"
#include "esp_clk_tree.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_private/periph_ctrl.h"
#include "esp_rom_gpio.h"
#include "hal/uart_ll.h"
#include "soc/gpio_sig_map.h"
#include "soc/uart_periph.h"

namespace
{
// RX interrupt reasons handled by the FIFO receive path.
// FIFO 接收路径处理的 RX 中断原因。
constexpr uint32_t UART_RX_INTR_MASK =
    UART_INTR_RXFIFO_FULL | UART_INTR_RXFIFO_TOUT | UART_INTR_RXFIFO_OVF;

// TX interrupt reason used by the FIFO transmit path.
// FIFO 发送路径使用的 TX 中断原因。
constexpr uint32_t UART_TX_INTR_MASK = UART_INTR_TXFIFO_EMPTY;

// Use the minimum non-zero timeout so short RX tails are flushed as close as
// possible to an idle-style boundary.
// 使用最小非零 timeout，让短 RX 尾包尽量贴近 idle 风格边界被冲刷出来。
constexpr uint8_t RX_TOUT_THRESHOLD = 1;

// Ask for more TX bytes once the hardware FIFO drops to roughly half depth.
// 当硬件 FIFO 下降到约一半深度时，请求补充更多 TX 字节。
constexpr uint16_t TX_EMPTY_THRESHOLD = SOC_UART_FIFO_LEN / 2U;

// This backend cannot coexist with the ESP-IDF console UART reservation.
// 该后端不能与 ESP-IDF 的控制台 UART 占用同时存在。
bool IsConsoleUartInUse(uart_port_t uart_num)
{
#if defined(CONFIG_ESP_CONSOLE_UART) && CONFIG_ESP_CONSOLE_UART
  return static_cast<int>(uart_num) == CONFIG_ESP_CONSOLE_UART_NUM;
#else
  (void)uart_num;
  return false;
#endif
}
}  // namespace

namespace LibXR
{

// Prefer aligned DMA-capable allocation first, then fall back to the broader
// DMA-capable heap if the strict aligned allocation API is unavailable.
// 优先使用对齐的 DMA 可访问分配；若失败，再退回到更宽松的 DMA heap。
uint8_t* ESP32UART::AllocateTxStorage(size_t size)
{
  void* aligned = heap_caps_aligned_alloc(
      4, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
  if (aligned != nullptr)
  {
    return static_cast<uint8_t*>(aligned);
  }

  return static_cast<uint8_t*>(
      heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
}

// Translate the public UART index into the ESP peripheral module gate.
// 将公开 UART 序号转换为 ESP 外设模块门控对象。
ErrorCode ESP32UART::ResolveUartPeriph(uart_port_t uart_num, periph_module_t& out)
{
  switch (uart_num)
  {
    case UART_NUM_0:
      out = PERIPH_UART0_MODULE;
      return ErrorCode::OK;
    case UART_NUM_1:
      out = PERIPH_UART1_MODULE;
      return ErrorCode::OK;
#if SOC_UART_HP_NUM > 2
    case UART_NUM_2:
      out = PERIPH_UART2_MODULE;
      return ErrorCode::OK;
#endif
    default:
      return ErrorCode::NOT_SUPPORT;
  }
}

// Construct queue plumbing first, then bind the storage into the TX
// double-buffer view before touching hardware.
// 先构造队列连接，再把 storage 绑定到 TX 双缓冲视图，最后再触碰硬件。
ESP32UART::ESP32UART(uart_port_t uart_num, int tx_pin, int rx_pin, int rts_pin,
                     int cts_pin, size_t rx_buffer_size, size_t tx_buffer_size,
                     uint32_t tx_queue_size, UART::Configuration config, bool enable_dma)
    : UART(&_read_port, &_write_port),
      uart_num_(uart_num),
      tx_pin_(tx_pin),
      rx_pin_(rx_pin),
      rts_pin_(rts_pin),
      cts_pin_(cts_pin),
      config_(config),
      rx_isr_buffer_(new (std::nothrow) uint8_t[rx_buffer_size]),
      rx_isr_buffer_size_(rx_buffer_size),
      tx_storage_(AllocateTxStorage(tx_buffer_size * 2)),
      dma_requested_(enable_dma),
      _read_port(rx_buffer_size),
      _write_port(tx_queue_size, tx_buffer_size)
{
  ASSERT(!IsConsoleUartInUse(uart_num_));
  ASSERT(uart_num_ < UART_NUM_MAX);
  ASSERT(uart_num_ < SOC_UART_HP_NUM);
  ASSERT(rx_isr_buffer_size_ > 0);
  ASSERT(tx_buffer_size > 0);
  ASSERT(rx_isr_buffer_ != nullptr);
  ASSERT(tx_storage_ != nullptr);

  tx_dma_buffer_.Init({tx_storage_, tx_buffer_size * 2U});

  _read_port = ReadFun;
  _write_port = WriteFun;

  if (InitUartHardware() != ErrorCode::OK)
  {
    ASSERT(false);
    return;
  }

#if SOC_GDMA_SUPPORTED && SOC_UHCI_SUPPORTED
  if (dma_requested_)
  {
    if (InitDmaBackend() != ErrorCode::OK)
    {
      ASSERT(false);
      return;
    }
  }

  if (!dma_backend_enabled_)
  {
    if (InstallUartIsr() != ErrorCode::OK)
    {
      ASSERT(false);
      return;
    }
    ConfigureRxInterruptPath();
  }
#else
  if (InstallUartIsr() != ErrorCode::OK)
  {
    ASSERT(false);
    return;
  }
  ConfigureRxInterruptPath();
#endif
}

// Use a half-FIFO RX threshold in FIFO mode. Short residual tails are handled
// by the minimum non-zero RX timeout above.
// FIFO 模式下使用半 FIFO 的 RX 阈值，剩余短尾包交给上面的最小非零 RX timeout。
void ESP32UART::ConfigureRxInterruptPath()
{
  const uint16_t full_thr = static_cast<uint16_t>(SOC_UART_FIFO_LEN / 2U);

  uart_hal_set_rxfifo_full_thr(&uart_hal_, full_thr);
  uart_hal_set_rx_timeout(&uart_hal_, RX_TOUT_THRESHOLD);
  uart_hal_clr_intsts_mask(&uart_hal_, UART_RX_INTR_MASK);
  uart_hal_ena_intr_mask(&uart_hal_, UART_RX_INTR_MASK);
}

// Reconfigure framing in place while preserving the current software queue
// model. If TX is already in progress, resume the backend instead of surfacing
// a synthetic BUSY state.
// 原地重配帧格式，同时保持软件队列模型不变。若 TX 已在进行，则恢复后端，
// 而不是人为抛出 BUSY 状态。
ErrorCode ESP32UART::SetConfig(UART::Configuration config)
{
  if (!uart_hw_enabled_)
  {
    return ErrorCode::STATE_ERR;
  }

  uart_word_length_t word_length = UART_DATA_8_BITS;
  uart_stop_bits_t stop_bits = UART_STOP_BITS_1;

  if (!ResolveWordLength(config.data_bits, word_length))
  {
    return ErrorCode::ARG_ERR;
  }

  if (!ResolveStopBits(config.stop_bits, stop_bits))
  {
    return ErrorCode::ARG_ERR;
  }

  const uart_sclk_t sclk = UART_SCLK_DEFAULT;
  uart_hal_set_sclk(&uart_hal_, static_cast<soc_module_clk_t>(sclk));

  uint32_t sclk_hz = 0;
  if ((esp_clk_tree_src_get_freq_hz(static_cast<soc_module_clk_t>(sclk),
                                    ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED,
                                    &sclk_hz) != ESP_OK) ||
      (sclk_hz == 0))
  {
    return ErrorCode::INIT_ERR;
  }

  if (!uart_hal_set_baudrate(&uart_hal_, config.baudrate, sclk_hz))
  {
    return ErrorCode::INIT_ERR;
  }

  uart_hal_set_data_bit_num(&uart_hal_, word_length);
  uart_hal_set_stop_bits(&uart_hal_, stop_bits);
  uart_hal_set_parity(&uart_hal_, ResolveParity(config.parity));
  uart_hal_set_hw_flow_ctrl(&uart_hal_, UART_HW_FLOWCTRL_DISABLE, 0);
  uart_hal_set_mode(&uart_hal_, UART_MODE_UART);
  uart_hal_set_txfifo_empty_thr(&uart_hal_, TX_EMPTY_THRESHOLD);
  // Drop stale hardware RX FIFO bytes from the previous baud.
  // Keep software read queue semantics aligned with ST/CH (no read_port reset).
  uart_hal_rxfifo_rst(&uart_hal_);
  uart_hal_clr_intsts_mask(&uart_hal_, UART_INTR_RXFIFO_FULL | UART_INTR_RXFIFO_TOUT);

#if SOC_GDMA_SUPPORTED && SOC_UHCI_SUPPORTED
  if (dma_backend_enabled_)
  {
    // Re-align the circular RX DMA window after reconfig to avoid
    // carrying stale pre-switch bytes into the next frame.
    HandleDmaRxError();
  }
#endif

  // Align with ST/CH SetConfig semantics: if TX was in-flight during
  // reconfiguration, keep transfer progression instead of surfacing BUSY.
  if (tx_busy_.IsSet() && tx_active_valid_)
  {
#if SOC_GDMA_SUPPORTED && SOC_UHCI_SUPPORTED
    if (dma_backend_enabled_)
    {
      (void)StartDmaTx();
    }
    else
#endif
    {
      uart_hal_clr_intsts_mask(&uart_hal_, UART_TX_INTR_MASK);
      uart_hal_ena_intr_mask(&uart_hal_, UART_TX_INTR_MASK);
      FillTxFifo(false);
    }
  }

  config_ = config;
  return ErrorCode::OK;
}

// Internal UART loopback is exposed as a direct peripheral toggle for backend
// self-test and board-free link checks.
// UART 内部环回直接作为外设开关暴露，用于后端自测和无需外部短接的链路检查。
ErrorCode ESP32UART::SetLoopback(bool enable)
{
  if (!uart_hw_enabled_)
  {
    return ErrorCode::STATE_ERR;
  }

  uart_ll_set_loop_back(uart_hal_.dev, enable);
  return ErrorCode::OK;
}

// `WritePort` only needs a trampoline back into the owning UART instance.
// `WritePort` 只需要一个回跳到所属 UART 实例的跳板。
ErrorCode IRAM_ATTR ESP32UART::WriteFun(WritePort& port, bool in_isr)
{
  auto* uart = LibXR::ContainerOf(&port, &ESP32UART::_write_port);
  return uart->TryStartTx(in_isr);
}

// The current RX path is interrupt-driven, so `ReadPort` has no active kick.
// 当前 RX 路径是中断驱动的，因此 `ReadPort` 不需要主动启动。
ErrorCode ESP32UART::ReadFun(ReadPort&, bool) { return ErrorCode::PENDING; }

// Convert libxr data-bit semantics into the ESP HAL value set.
// 将 libxr 数据位语义转换为 ESP HAL 取值。
bool ESP32UART::ResolveWordLength(uint8_t data_bits, uart_word_length_t& out)
{
  switch (data_bits)
  {
    case 5:
      out = UART_DATA_5_BITS;
      return true;
    case 6:
      out = UART_DATA_6_BITS;
      return true;
    case 7:
      out = UART_DATA_7_BITS;
      return true;
    case 8:
      out = UART_DATA_8_BITS;
      return true;
    default:
      return false;
  }
}

// Convert libxr stop-bit semantics into the ESP HAL value set.
// 将 libxr 停止位语义转换为 ESP HAL 取值。
bool ESP32UART::ResolveStopBits(uint8_t stop_bits, uart_stop_bits_t& out)
{
  switch (stop_bits)
  {
    case 1:
      out = UART_STOP_BITS_1;
      return true;
    case 2:
      out = UART_STOP_BITS_2;
      return true;
    default:
      return false;
  }
}

// Convert libxr parity semantics into the ESP HAL value set.
// 将 libxr 校验位语义转换为 ESP HAL 取值。
uart_parity_t ESP32UART::ResolveParity(UART::Parity parity)
{
  switch (parity)
  {
    case UART::Parity::NO_PARITY:
      return UART_PARITY_DISABLE;
    case UART::Parity::EVEN:
      return UART_PARITY_EVEN;
    case UART::Parity::ODD:
      return UART_PARITY_ODD;
    default:
      return UART_PARITY_DISABLE;
  }
}

// Bring the UART block into a known idle state before higher-level ISR or DMA
// plumbing is attached.
// 在挂接更高层 ISR 或 DMA 连接前，先把 UART 模块拉到已知空闲状态。
ErrorCode ESP32UART::InitUartHardware()
{
  if (uart_num_ >= UART_NUM_MAX)
  {
    return ErrorCode::NOT_SUPPORT;
  }

  periph_module_t uart_module = PERIPH_MODULE_MAX;
  if (ResolveUartPeriph(uart_num_, uart_module) != ErrorCode::OK)
  {
    return ErrorCode::NOT_SUPPORT;
  }

  uart_hal_.dev = UART_LL_GET_HW(uart_num_);
  if (uart_hal_.dev == nullptr)
  {
    return ErrorCode::NOT_SUPPORT;
  }

  periph_module_enable(uart_module);
  periph_module_reset(uart_module);

  uart_ll_sclk_enable(uart_hal_.dev);
  uart_hal_init(&uart_hal_, uart_num_);

  uart_hw_enabled_ = true;
  if (SetConfig(config_) != ErrorCode::OK)
  {
    uart_hw_enabled_ = false;
    return ErrorCode::INIT_ERR;
  }

  if (ConfigurePins() != ErrorCode::OK)
  {
    uart_hw_enabled_ = false;
    return ErrorCode::INIT_ERR;
  }

  uart_hal_txfifo_rst(&uart_hal_);
  uart_hal_rxfifo_rst(&uart_hal_);
  uart_hal_clr_intsts_mask(&uart_hal_, UINT32_MAX);
  uart_hal_disable_intr_mask(&uart_hal_, UINT32_MAX);

  return ErrorCode::OK;
}

// GPIO mapping stays explicit because ESP UART routing is per-pin configurable.
// GPIO 映射保持显式写法，因为 ESP UART 路由是逐引脚可配置的。
ErrorCode ESP32UART::ConfigurePins()
{
  if (tx_pin_ >= 0)
  {
    if (!GPIO_IS_VALID_OUTPUT_GPIO(tx_pin_))
    {
      return ErrorCode::ARG_ERR;
    }
    esp_rom_gpio_pad_select_gpio(static_cast<uint32_t>(tx_pin_));
    esp_rom_gpio_connect_out_signal(
        tx_pin_, UART_PERIPH_SIGNAL(uart_num_, SOC_UART_TX_PIN_IDX), false, false);
  }

  if (rx_pin_ >= 0)
  {
    if (!GPIO_IS_VALID_GPIO(rx_pin_))
    {
      return ErrorCode::ARG_ERR;
    }
    gpio_input_enable(static_cast<gpio_num_t>(rx_pin_));
    esp_rom_gpio_connect_in_signal(
        rx_pin_, UART_PERIPH_SIGNAL(uart_num_, SOC_UART_RX_PIN_IDX), false);
  }

  if (rts_pin_ >= 0)
  {
    if (!GPIO_IS_VALID_OUTPUT_GPIO(rts_pin_))
    {
      return ErrorCode::ARG_ERR;
    }
    esp_rom_gpio_pad_select_gpio(static_cast<uint32_t>(rts_pin_));
    esp_rom_gpio_connect_out_signal(
        rts_pin_, UART_PERIPH_SIGNAL(uart_num_, SOC_UART_RTS_PIN_IDX), false, false);
  }

  if (cts_pin_ >= 0)
  {
    if (!GPIO_IS_VALID_GPIO(cts_pin_))
    {
      return ErrorCode::ARG_ERR;
    }
    gpio_pullup_en(static_cast<gpio_num_t>(cts_pin_));
    gpio_input_enable(static_cast<gpio_num_t>(cts_pin_));
    esp_rom_gpio_connect_in_signal(
        cts_pin_, UART_PERIPH_SIGNAL(uart_num_, SOC_UART_CTS_PIN_IDX), false);
  }

  return ErrorCode::OK;
}

// Active state is shared by FIFO and DMA TX backends.
// Active 状态由 FIFO 和 DMA 两条 TX 后端共用。
void IRAM_ATTR ESP32UART::ClearActiveTx()
{
  tx_active_length_ = 0U;
  tx_active_offset_ = 0U;
  tx_active_info_ = {};
  tx_active_valid_ = false;
}

// Only the DMA backend carries a distinct pending TX preload state.
// 只有 DMA 后端维护独立的 pending TX 预装状态。
void IRAM_ATTR ESP32UART::ClearPendingTx()
{
#if SOC_GDMA_SUPPORTED && SOC_UHCI_SUPPORTED
  if (dma_backend_enabled_)
  {
    tx_dma_buffer_.Reset();
  }
#endif
}

// Once hardware accepts the active request, completion ownership moves to the
// backend ISR path and the queued write can be reported as accepted.
// 一旦硬件接管 active 请求，完成所有权就转移到后端 ISR 路径，队列侧即可
// 报告该写请求已经被接受。
bool IRAM_ATTR ESP32UART::StartAndReportActive(bool in_isr)
{
  if (!StartActiveTransfer(in_isr))
  {
    write_port_->Finish(in_isr, ErrorCode::FAILED, tx_active_info_);
    ClearActiveTx();
    return false;
  }

  // Align with STM/CH semantics: once the active write is kicked to HW,
  // WritePort owns the completion notification and the ISR only advances queues.
  write_port_->Finish(in_isr, ErrorCode::OK, tx_active_info_);
  return true;
}

// TX start logic fans out into two models:
// 1. DMA mode keeps one active request plus one preloaded pending request.
// 2. FIFO mode streams directly from the queue with only one active request.
// TX 启动逻辑分成两种模型：
// 1. DMA 模式维持一个 active 请求和一个预装 pending 请求。
// 2. FIFO 模式只保留一个 active 请求并直接从队列流式发送。
ErrorCode IRAM_ATTR ESP32UART::TryStartTx(bool in_isr)
{
  if (in_tx_isr_.IsSet())
  {
    return ErrorCode::PENDING;
  }

#if SOC_GDMA_SUPPORTED && SOC_UHCI_SUPPORTED
  if (dma_backend_enabled_ && StartPendingTxIfIdle(in_isr))
  {
    return ErrorCode::PENDING;
  }
#endif

  if (!tx_active_valid_)
  {
    (void)LoadActiveTxFromQueue(in_isr);
  }

  if (!tx_busy_.IsSet() && tx_active_valid_)
  {
    if (!StartActiveTransfer(in_isr))
    {
      ClearActiveTx();
      return ErrorCode::FAILED;
    }

    return ErrorCode::OK;
  }

#if SOC_GDMA_SUPPORTED && SOC_UHCI_SUPPORTED
  if (dma_backend_enabled_ && !tx_dma_buffer_.HasPending())
  {
    (void)LoadPendingTxFromQueue(in_isr);
    (void)StartPendingTxIfIdle(in_isr);
  }
#endif

  return ErrorCode::PENDING;
}

// Active TX load is shared by both backends. DMA writes payload bytes into the
// double buffer, while FIFO mode only claims metadata and length.
// Active TX 装载由两条后端共用。DMA 会把 payload 写进双缓冲；FIFO 模式只
// 认领元数据和长度。
bool IRAM_ATTR ESP32UART::LoadActiveTxFromQueue(bool in_isr)
{
  (void)in_isr;

  size_t active_length = 0U;
  WriteInfoBlock active_info = {};
  uint8_t* active_buffer = nullptr;
#if SOC_GDMA_SUPPORTED && SOC_UHCI_SUPPORTED
  if (dma_backend_enabled_)
  {
    active_buffer = tx_dma_buffer_.ActiveBuffer();
  }
#endif
  if (!DequeueTxToBuffer(active_buffer, active_length, active_info, in_isr))
  {
    return false;
  }

#if SOC_GDMA_SUPPORTED && SOC_UHCI_SUPPORTED
  if (dma_backend_enabled_)
  {
    tx_dma_buffer_.SetActiveLength(active_length);
  }
  else
#endif
  {
    tx_active_length_ = active_length;
  }
  tx_active_offset_ = 0U;
  tx_active_info_ = active_info;
  tx_active_valid_ = true;
  return true;
}

// Pending preload exists only for the DMA backend because FIFO mode can stream
// directly from the queue without staging a second payload block.
// Pending 预装只存在于 DMA 后端，因为 FIFO 模式可以直接从队列流式发送，
// 不需要再暂存第二块 payload。
bool IRAM_ATTR ESP32UART::LoadPendingTxFromQueue(bool in_isr)
{
  (void)in_isr;

#if !(SOC_GDMA_SUPPORTED && SOC_UHCI_SUPPORTED)
  return false;
#else
  if (!dma_backend_enabled_ || tx_dma_buffer_.HasPending())
  {
    return false;
  }

  size_t pending_length = 0U;
  WriteInfoBlock pending_info = {};
  if (!DequeueTxToBuffer(tx_dma_buffer_.PendingBuffer(), pending_length, pending_info,
                         in_isr))
  {
    return false;
  }

  tx_dma_buffer_.SetPendingLength(pending_length);
  tx_dma_buffer_.EnablePending();
  return true;
#endif
}

// Promote the preloaded DMA payload into the active slot when the hardware is
// idle and the software state machine has no current active request.
// 当硬件空闲且软件状态机没有 active 请求时，把预装 DMA payload 提升为
// active 槽位。
bool IRAM_ATTR ESP32UART::StartPendingTxIfIdle(bool in_isr)
{
#if !(SOC_GDMA_SUPPORTED && SOC_UHCI_SUPPORTED)
  (void)in_isr;
  return false;
#else
  if (!dma_backend_enabled_)
  {
    (void)in_isr;
    return false;
  }

  if (tx_busy_.IsSet() || tx_active_valid_ || !tx_dma_buffer_.HasPending())
  {
    return false;
  }

  const size_t pending_length = tx_dma_buffer_.GetPendingLength();
  tx_dma_buffer_.Switch();
  tx_dma_buffer_.SetActiveLength(pending_length);

  if (write_port_->queue_info_->Pop(tx_active_info_) != ErrorCode::OK)
  {
    ASSERT(false);
    return false;
  }
  tx_active_length_ = pending_length;
  tx_active_offset_ = 0U;
  tx_active_valid_ = true;

  return StartAndReportActive(in_isr);
#endif
}

// Queue-data and queue-info stay decoupled, so dequeue first validates the
// next metadata entry and only then moves bytes or ownership forward.
// `queue_data_` 和 `queue_info_` 保持解耦，因此这里先验证下一条元数据，
// 再推进字节或所有权。
bool IRAM_ATTR ESP32UART::DequeueTxToBuffer(uint8_t* buffer, size_t& size,
                                            WriteInfoBlock& info, bool in_isr)
{
  (void)in_isr;
  (void)buffer;

  WriteInfoBlock peek_info = {};
  if (write_port_->queue_info_->Peek(peek_info) != ErrorCode::OK)
  {
    return false;
  }

  size_t max_size = write_port_->queue_data_->MaxSize();
#if SOC_GDMA_SUPPORTED && SOC_UHCI_SUPPORTED
  if (dma_backend_enabled_)
  {
    max_size = tx_dma_buffer_.Size();
  }
#endif
  if (peek_info.data.size_ > max_size)
  {
    ASSERT(false);
    return false;
  }

#if SOC_GDMA_SUPPORTED && SOC_UHCI_SUPPORTED
  if (dma_backend_enabled_)
  {
    if (write_port_->queue_data_->PopBatch(buffer, peek_info.data.size_) != ErrorCode::OK)
    {
      return false;
    }
  }
#endif

  if (write_port_->queue_info_->Pop(info) != ErrorCode::OK)
  {
    return false;
  }

  size = peek_info.data.size_;
  return true;
}

// Backend start forks here:
// - DMA path launches the staged active payload as one DMA transfer.
// - FIFO path enables TX-empty interrupts and lets ISR-side refill drain data.
// 后端启动在这里分叉：
// - DMA 路径把已暂存的 active payload 作为一次 DMA 传输发出去。
// - FIFO 路径开启 TX-empty 中断，让 ISR 侧补料并持续排空。
bool IRAM_ATTR ESP32UART::StartActiveTransfer(bool)
{
  if (!tx_active_valid_)
  {
    return false;
  }

  if (tx_busy_.TestAndSet())
  {
    return true;
  }

  tx_active_offset_ = 0U;

#if SOC_GDMA_SUPPORTED && SOC_UHCI_SUPPORTED
  if (dma_backend_enabled_)
  {
    if (StartDmaTx())
    {
      return true;
    }

    tx_busy_.Clear();
    return false;
  }
#endif

  uart_hal_clr_intsts_mask(&uart_hal_, UART_TX_INTR_MASK);
  uart_hal_ena_intr_mask(&uart_hal_, UART_TX_INTR_MASK);
  FillTxFifo(false);

  return true;
}

// RX bytes are pushed opportunistically until the software queue is full, then
// pending read callbacks are serviced once per batch.
// RX 字节会尽量推进软件队列，直到队列满；完成后按批次触发待读回调。
void IRAM_ATTR ESP32UART::PushRxBytes(const uint8_t* data, size_t size, bool in_isr)
{
  size_t offset = 0;
  bool pushed_any = false;
  while (offset < size)
  {
    const size_t free_space = read_port_->queue_data_->EmptySize();
    if (free_space == 0)
    {
      break;
    }

    const size_t chunk = std::min(free_space, size - offset);
    if (read_port_->queue_data_->PushBatch(data + offset, chunk) != ErrorCode::OK)
    {
      break;
    }

    offset += chunk;
    pushed_any = true;
  }

  if (pushed_any)
  {
    read_port_->ProcessPendingReads(in_isr);
  }
}

// Completion handling also splits by backend model:
// - DMA mode may already hold one pending preloaded payload.
// - FIFO mode immediately loads the next request from the queue.
// 完成处理同样按后端模型分叉：
// - DMA 模式可能已经持有一个预装的 pending payload。
// - FIFO 模式则立刻从队列装载下一条请求。
void IRAM_ATTR ESP32UART::OnTxTransferDone(bool in_isr, ErrorCode result)
{
  Flag::ScopedRestore tx_flag(in_tx_isr_);
  tx_busy_.Clear();

  ClearActiveTx();

#if SOC_GDMA_SUPPORTED && SOC_UHCI_SUPPORTED
  if (dma_backend_enabled_)
  {
    if ((result != ErrorCode::OK) && tx_dma_buffer_.HasPending())
    {
      WriteInfoBlock dropped_info = {};
      if (write_port_->queue_info_->Pop(dropped_info) == ErrorCode::OK)
      {
        write_port_->Finish(in_isr, ErrorCode::FAILED, dropped_info);
      }
      else
      {
        ASSERT(false);
      }
      tx_dma_buffer_.Reset();
    }

    if (result != ErrorCode::OK)
    {
      return;
    }

    if (StartPendingTxIfIdle(in_isr))
    {
      if (!tx_dma_buffer_.HasPending())
      {
        (void)LoadPendingTxFromQueue(in_isr);
      }
      return;
    }

    if (!tx_dma_buffer_.HasPending())
    {
      (void)LoadPendingTxFromQueue(in_isr);
    }

    if (!tx_busy_.IsSet())
    {
      (void)StartPendingTxIfIdle(in_isr);
    }
    return;
  }
#endif

  if (result != ErrorCode::OK)
  {
    return;
  }

  if (LoadActiveTxFromQueue(in_isr))
  {
    (void)StartAndReportActive(in_isr);
  }
}

}  // namespace LibXR
