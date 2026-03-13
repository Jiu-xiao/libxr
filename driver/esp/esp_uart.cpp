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
constexpr uint32_t kUartRxIntrMask =
    UART_INTR_RXFIFO_FULL | UART_INTR_RXFIFO_TOUT | UART_INTR_RXFIFO_OVF;
constexpr uint32_t kUartTxIntrMask = UART_INTR_TXFIFO_EMPTY;

constexpr uint8_t kRxToutThreshold = 2;
constexpr uint16_t kTxEmptyThreshold = 24;

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
      tx_storage_size_(tx_buffer_size * 2),
      tx_buffer_size_(tx_buffer_size),
      dma_requested_(enable_dma),
      _read_port(rx_buffer_size),
      _write_port(tx_queue_size, tx_buffer_size)
{
  ASSERT(!IsConsoleUartInUse(uart_num_));
  ASSERT(uart_num_ < UART_NUM_MAX);
  ASSERT(uart_num_ < SOC_UART_HP_NUM);
  ASSERT(rx_isr_buffer_size_ > 0);
  ASSERT(tx_buffer_size_ > 0);
  ASSERT(rx_isr_buffer_ != nullptr);
  ASSERT(tx_storage_ != nullptr);
  ASSERT(tx_storage_size_ >= (tx_buffer_size_ * 2U));

  tx_active_buffer_ = tx_storage_;
  tx_pending_buffer_ = tx_storage_ + tx_buffer_size_;
  tx_active_length_ = 0;
  tx_pending_length_ = 0;

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

void ESP32UART::ConfigureRxInterruptPath()
{
  const size_t rx_full_floor = 16;
  const size_t rx_full_ceil = std::max<size_t>(rx_full_floor, SOC_UART_FIFO_LEN / 4);
  const uint16_t full_thr = static_cast<uint16_t>(std::min<size_t>(
      rx_full_ceil, std::max<size_t>(rx_full_floor, rx_isr_buffer_size_ / 16)));

  uart_hal_set_rxfifo_full_thr(&uart_hal_, full_thr);
  uart_hal_set_rx_timeout(&uart_hal_, kRxToutThreshold);
  uart_hal_clr_intsts_mask(&uart_hal_, kUartRxIntrMask);
  uart_hal_ena_intr_mask(&uart_hal_, kUartRxIntrMask);
}

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
  uart_hal_set_txfifo_empty_thr(&uart_hal_, kTxEmptyThreshold);
  // Drop stale hardware RX FIFO bytes from the previous baud.
  // Keep software read queue semantics aligned with ST/CH (no read_port reset).
  // TODO: classic ESP32 FIFO+ISR still shows a small startup RX transient in the
  // first legacy loopback window. Find the driver-side source so the external
  // benchmark warm-up workaround can be removed.
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
      uart_hal_clr_intsts_mask(&uart_hal_, kUartTxIntrMask);
      uart_hal_ena_intr_mask(&uart_hal_, kUartTxIntrMask);
      FillTxFifo(false);
    }
  }

  config_ = config;
  return ErrorCode::OK;
}

ErrorCode ESP32UART::SetLoopback(bool enable)
{
  if (!uart_hw_enabled_)
  {
    return ErrorCode::STATE_ERR;
  }

  uart_ll_set_loop_back(uart_hal_.dev, enable);
  return ErrorCode::OK;
}

ErrorCode IRAM_ATTR ESP32UART::WriteFun(WritePort& port, bool in_isr)
{
  auto* uart = CONTAINER_OF(&port, ESP32UART, _write_port);
  return uart->TryStartTx(in_isr);
}

ErrorCode ESP32UART::ReadFun(ReadPort&, bool) { return ErrorCode::PENDING; }

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

void IRAM_ATTR ESP32UART::ClearActiveTx()
{
  tx_active_length_ = 0;
  tx_active_offset_ = 0;
  tx_active_info_ = {};
  tx_active_valid_ = false;
  tx_active_reported_ = false;
}

void IRAM_ATTR ESP32UART::ClearPendingTx()
{
  tx_pending_length_ = 0;
  tx_pending_info_ = {};
  tx_pending_valid_ = false;
}

bool IRAM_ATTR ESP32UART::StartAndReportActive(bool in_isr)
{
  // Mark the op as already reported before kicking HW so a very-fast EOF ISR
  // cannot race back in and report the same callback twice.
  const bool report_now = !tx_active_reported_;
  if (report_now)
  {
    tx_active_reported_ = true;
  }

  if (!StartActiveTransfer(in_isr))
  {
    tx_active_reported_ = false;
    write_port_->Finish(in_isr, ErrorCode::FAILED, tx_active_info_);
    ClearActiveTx();
    return false;
  }

  if (report_now)
  {
    // Align with STM/CH semantics: op is considered complete once transfer is kicked.
    write_port_->Finish(in_isr, ErrorCode::OK, tx_active_info_);
  }
  return true;
}

ErrorCode IRAM_ATTR ESP32UART::TryStartTx(bool in_isr)
{
  if (in_tx_isr_.IsSet())
  {
    return ErrorCode::PENDING;
  }
  if (!tx_active_valid_)
  {
    (void)LoadActiveTxFromQueue(in_isr);
  }

  if (!tx_busy_.IsSet() && tx_active_valid_)
  {
    const bool report_now = !tx_active_reported_;
    if (report_now)
    {
      tx_active_reported_ = true;
    }

    if (!StartActiveTransfer(in_isr))
    {
      ClearActiveTx();
      return ErrorCode::FAILED;
    }

    if (report_now)
    {
      // Current op completion is reported by WritePort when TryStartTx returns OK.
      if (!tx_pending_valid_)
      {
        (void)LoadPendingTxFromQueue(in_isr);
      }
      return ErrorCode::OK;
    }
  }

  if (!tx_pending_valid_)
  {
    (void)LoadPendingTxFromQueue(in_isr);
  }

  return ErrorCode::PENDING;
}

bool IRAM_ATTR ESP32UART::LoadActiveTxFromQueue(bool in_isr)
{
  (void)in_isr;

  size_t active_length = 0U;
  if (!DequeueTxToBuffer(tx_active_buffer_, active_length, tx_active_info_, in_isr))
  {
    return false;
  }

  tx_active_length_ = active_length;
  tx_active_offset_ = 0;
  tx_active_valid_ = true;
  tx_active_reported_ = false;
  return true;
}

bool IRAM_ATTR ESP32UART::LoadPendingTxFromQueue(bool in_isr)
{
  (void)in_isr;

  if (tx_pending_valid_)
  {
    return false;
  }

  size_t pending_length = 0U;
  if (!DequeueTxToBuffer(tx_pending_buffer_, pending_length, tx_pending_info_, in_isr))
  {
    return false;
  }

  tx_pending_length_ = pending_length;
  tx_pending_valid_ = true;
  return true;
}

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

  if (peek_info.data.size_ > tx_buffer_size_)
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

  tx_active_offset_ = 0;

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

  uart_hal_clr_intsts_mask(&uart_hal_, kUartTxIntrMask);
  uart_hal_ena_intr_mask(&uart_hal_, kUartTxIntrMask);
  FillTxFifo(false);

  return true;
}

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
void IRAM_ATTR ESP32UART::OnTxTransferDone(bool in_isr, ErrorCode result)
{
  Flag::ScopedRestore tx_flag(in_tx_isr_);
  tx_busy_.Clear();

  if (tx_active_valid_ && !tx_active_reported_)
  {
    write_port_->Finish(in_isr, result, tx_active_info_);
    tx_active_reported_ = true;
  }

  ClearActiveTx();

  if ((result != ErrorCode::OK) && tx_pending_valid_)
  {
    write_port_->Finish(in_isr, ErrorCode::FAILED, tx_pending_info_);
    ClearPendingTx();
  }

  if (result != ErrorCode::OK)
  {
    ClearPendingTx();
    return;
  }

  if (tx_pending_valid_)
  {
    std::swap(tx_active_buffer_, tx_pending_buffer_);
    tx_active_length_ = tx_pending_length_;
    tx_pending_length_ = 0;
    tx_active_info_ = tx_pending_info_;
    tx_active_valid_ = true;
    tx_active_reported_ = false;
    ClearPendingTx();
    (void)StartAndReportActive(in_isr);
  }
  else
  {
    if (LoadActiveTxFromQueue(in_isr))
    {
      (void)StartAndReportActive(in_isr);
    }
  }

  if (!tx_pending_valid_)
  {
    (void)LoadPendingTxFromQueue(in_isr);
  }
}

}  // namespace LibXR
