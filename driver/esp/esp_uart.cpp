#include "esp_uart.hpp"

#include <algorithm>
#include <cstring>

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
#if LIBXR_ESP_UART_HAS_AHB_GDMA && defined(SOC_UART_SUPPORT_XTAL_CLK) && \
    SOC_UART_SUPPORT_XTAL_CLK
// Runtime CONFIG can run from an ISR, so cache one fixed source frequency at startup.
constexpr uart_sclk_t UART_CLOCK_SOURCE = UART_SCLK_XTAL;
#else
constexpr uart_sclk_t UART_CLOCK_SOURCE = UART_SCLK_DEFAULT;
#endif

// RX interrupt reasons handled by the FIFO receive path.
// FIFO 接收路径处理的 RX 中断原因。
constexpr uint32_t UART_RX_INTR_MASK =
    UART_INTR_RXFIFO_FULL | UART_INTR_RXFIFO_TOUT | UART_INTR_RXFIFO_OVF;

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

void ESP32UARTIrqAdapter::LockAndMaskIrqDomain() noexcept
{
  portENTER_CRITICAL_SAFE(&owner_.irq_domain_lock_);
  owner_.SetIrqDomainEnabledLocked(false);
}

void ESP32UARTIrqAdapter::UnlockIrqDomain() noexcept
{
  portEXIT_CRITICAL_SAFE(&owner_.irq_domain_lock_);
}

void ESP32UARTIrqAdapter::LockIrqDomain() noexcept
{
  portENTER_CRITICAL_SAFE(&owner_.irq_domain_lock_);
}

void ESP32UARTIrqAdapter::RestoreAndUnlockIrqDomain() noexcept
{
  owner_.SetIrqDomainEnabledLocked(true);
  portEXIT_CRITICAL_SAFE(&owner_.irq_domain_lock_);
}

bool ESP32UART::IsCurrentTaskPinned()
{
#if defined(CONFIG_FREERTOS_SMP) && CONFIG_FREERTOS_SMP
  const UBaseType_t affinity = vTaskCoreAffinityGet(nullptr);
  return (affinity != 0U) && ((affinity & (affinity - 1U)) == 0U);
#else
  return xTaskGetCoreID(nullptr) != tskNO_AFFINITY;
#endif
}

void IRAM_ATTR ESP32UART::SetIrqDomainEnabled(bool enabled) noexcept
{
  portENTER_CRITICAL_SAFE(&irq_domain_lock_);
  SetIrqDomainEnabledLocked(enabled);
  portEXIT_CRITICAL_SAFE(&irq_domain_lock_);
}

void IRAM_ATTR ESP32UART::SetIrqDomainEnabledLocked(bool enabled) noexcept
{
  const bool in_isr = xPortInIsrContext() != pdFALSE;
  bool success = true;
  const auto set_handle = [enabled, &success](intr_handle_t handle)
  {
    if (handle == nullptr)
    {
      return;
    }
    const esp_err_t result = enabled ? esp_intr_enable(handle) : esp_intr_disable(handle);
    success = (result == ESP_OK) && success;
  };

  set_handle(uart_intr_handle_);
#if LIBXR_ESP_UART_HAS_AHB_GDMA
  set_handle(tx_gdma_intr_handle_);
  if (rx_gdma_intr_handle_ != tx_gdma_intr_handle_)
  {
    set_handle(rx_gdma_intr_handle_);
  }
#endif
  REQUIRE_FROM_CALLBACK(success, in_isr);
}

void ESP32UARTReadPort::OnRxDequeue(bool in_isr)
{
#if LIBXR_ESP_UART_HAS_AHB_GDMA
  if (owner_.dma_backend_enabled_)
  {
    return;
  }
#endif
  bool pushed_any = false;
  (void)owner_.execution_policy_.Invoke(
      ESP32UART::FIFO_EVENT_RX_DRAIN,
      [this, in_isr, &pushed_any](uint32_t events) noexcept
      { return owner_.ServiceFifo(events, in_isr, nullptr, pushed_any); });
  if (pushed_any)
  {
    owner_.read_port_->ProcessPendingReads(in_isr);
  }
}

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
      requested_config_(config),
      execution_policy_(*this),
      tx_storage_(AllocateTxStorage(tx_buffer_size * 2)),
      dma_requested_(enable_dma),
      _read_port(rx_buffer_size, *this),
      _write_port(tx_queue_size, tx_buffer_size),
      tx_dma_model_(*this, _write_port, RawData(tx_storage_, tx_buffer_size * 2U))
{
  ASSERT(!IsConsoleUartInUse(uart_num_));
  ASSERT(uart_num_ < UART_NUM_MAX);
  ASSERT(uart_num_ < SOC_UART_HP_NUM);
  ASSERT(rx_buffer_size > 0);
  ASSERT(tx_buffer_size > 0);
  if constexpr (ESP_UART_USES_IRQ_SERIALIZATION)
  {
    REQUIRE(IsCurrentTaskPinned());
  }

  _read_port = ReadFun;
  _write_port = WriteFun;

  if (InitUartHardware() != ErrorCode::OK)
  {
    ASSERT(false);
    return;
  }

#if LIBXR_ESP_UART_HAS_AHB_GDMA
  if (dma_requested_)
  {
    if (InitDmaBackend() != ErrorCode::OK)
    {
      ASSERT(false);
      return;
    }
  }

  if (dma_backend_enabled_)
  {
    if (InstallUartIsr() != ErrorCode::OK)
    {
      ASSERT(false);
      return;
    }
    ConfigureDmaErrorInterruptPath();
    DEV_ASSERT(esp_intr_get_cpu(uart_intr_handle_) ==
               esp_intr_get_cpu(tx_gdma_intr_handle_));
    DEV_ASSERT(esp_intr_get_cpu(uart_intr_handle_) ==
               esp_intr_get_cpu(rx_gdma_intr_handle_));
    SetIrqDomainEnabled(true);
  }
  else
  {
    if (InstallUartIsr() != ErrorCode::OK)
    {
      ASSERT(false);
      return;
    }
    ConfigureRxInterruptPath();
    if (esp_intr_enable(uart_intr_handle_) != ESP_OK)
    {
      ASSERT(false);
      return;
    }
  }
#else
  if (InstallUartIsr() != ErrorCode::OK)
  {
    ASSERT(false);
    return;
  }
  ConfigureRxInterruptPath();
  if (esp_intr_enable(uart_intr_handle_) != ESP_OK)
  {
    ASSERT(false);
    return;
  }
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

#if LIBXR_ESP_UART_HAS_AHB_GDMA
void ESP32UART::ConfigureDmaErrorInterruptPath()
{
  uart_hal_clr_intsts_mask(&uart_hal_, DMA_UART_ERROR_INTR_MASK);
  uart_hal_ena_intr_mask(&uart_hal_, DMA_UART_ERROR_INTR_MASK);
}
#endif

ErrorCode ESP32UART::SetConfig(UART::Configuration config)
{
  if (!uart_hw_enabled_)
  {
    return ErrorCode::STATE_ERR;
  }
  if ((config.baudrate == 0U) ||
      ((config.parity != UART::Parity::NO_PARITY) &&
       (config.parity != UART::Parity::EVEN) && (config.parity != UART::Parity::ODD)))
  {
    return ErrorCode::ARG_ERR;
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

#if LIBXR_ESP_UART_HAS_AHB_GDMA
  if (!dma_backend_enabled_)
  {
    return ErrorCode::NOT_SUPPORT;
  }
  if (!rx_config_gate_.TryReserveConfig())
  {
    return ErrorCode::BUSY;
  }
  requested_config_ = config;
  rx_config_gate_.PublishConfig();
  tx_dma_model_.RequestConfig(execution_policy_, xPortInIsrContext() != pdFALSE);
  return ErrorCode::OK;
#else
  (void)config;
  return ErrorCode::NOT_SUPPORT;
#endif
}

UartDmaControlResult ESP32UART::ApplyPendingConfig(bool in_isr)
{
#if LIBXR_ESP_UART_HAS_AHB_GDMA
  REQUIRE_FROM_CALLBACK(dma_backend_enabled_, in_isr);
  if (!rx_config_gate_.TryEnterConfig())
  {
    return UartDmaControlResult::PENDING;
  }

  REQUIRE_FROM_CALLBACK(gdma_stop(tx_dma_channel_) == ESP_OK, in_isr);
  REQUIRE_FROM_CALLBACK(gdma_stop(rx_dma_channel_) == ESP_OK, in_isr);
  REQUIRE_FROM_CALLBACK(gdma_reset(tx_dma_channel_) == ESP_OK, in_isr);
  REQUIRE_FROM_CALLBACK(gdma_reset(rx_dma_channel_) == ESP_OK, in_isr);

  gdma_hal_clear_intr(&tx_gdma_hal_, tx_gdma_channel_id_, GDMA_CHANNEL_DIRECTION_TX,
                      UINT32_MAX);
  gdma_hal_clear_intr(&rx_gdma_hal_, rx_gdma_channel_id_, GDMA_CHANNEL_DIRECTION_RX,
                      UINT32_MAX);
  (void)gdma_hal_read_intr_status(&tx_gdma_hal_, tx_gdma_channel_id_,
                                  GDMA_CHANNEL_DIRECTION_TX, true);
  (void)gdma_hal_read_intr_status(&rx_gdma_hal_, rx_gdma_channel_id_,
                                  GDMA_CHANNEL_DIRECTION_RX, true);

  const UART::Configuration config = requested_config_;
  REQUIRE_FROM_CALLBACK(ApplyConfigPayload(config), in_isr);
  return UartDmaControlResult::COMPLETED;
#else
  (void)in_isr;
  return UartDmaControlResult::COMPLETED;
#endif
}

void ESP32UART::ReleaseConfigAdmission(bool in_isr)
{
#if LIBXR_ESP_UART_HAS_AHB_GDMA
  if (dma_backend_enabled_)
  {
    REQUIRE_FROM_CALLBACK(ResetAndRestartRxDma(), in_isr);
  }
#else
  (void)in_isr;
#endif
  rx_config_gate_.LeaveConfig();
}

bool ESP32UART::ApplyConfigPayload(UART::Configuration config)
{
  if ((config.baudrate == 0U) ||
      ((config.parity != UART::Parity::NO_PARITY) &&
       (config.parity != UART::Parity::EVEN) && (config.parity != UART::Parity::ODD)))
  {
    return false;
  }

  uart_word_length_t word_length = UART_DATA_8_BITS;
  uart_stop_bits_t stop_bits = UART_STOP_BITS_1;
  if (!ResolveWordLength(config.data_bits, word_length) ||
      !ResolveStopBits(config.stop_bits, stop_bits))
  {
    return false;
  }

  if (uart_sclk_hz_ == 0U)
  {
    return false;
  }

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
  if (!uart_hal_set_baudrate(&uart_hal_, config.baudrate, uart_sclk_hz_))
  {
    return false;
  }
#else
  uart_hal_set_baudrate(&uart_hal_, config.baudrate, uart_sclk_hz_);
#endif

  uart_hal_set_data_bit_num(&uart_hal_, word_length);
  uart_hal_set_stop_bits(&uart_hal_, stop_bits);
  uart_hal_set_parity(&uart_hal_, ResolveParity(config.parity));
  uart_hal_set_hw_flow_ctrl(&uart_hal_, UART_HW_FLOWCTRL_DISABLE, 0);
  uart_hal_set_mode(&uart_hal_, UART_MODE_UART);
  uart_hal_set_txfifo_empty_thr(&uart_hal_, TX_EMPTY_THRESHOLD);
  uart_hal_txfifo_rst(&uart_hal_);
  uart_hal_rxfifo_rst(&uart_hal_);
  uart_hal_clr_intsts_mask(&uart_hal_, UINT32_MAX);

  config_ = config;
  return true;
}

#if !LIBXR_ESP_UART_HAS_AHB_GDMA
UartDmaTxStartResult ESP32UART::StartDmaTx(uint8_t*, size_t, int)
{
  return UartDmaTxStartResult::FAILED;
}
#endif

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
#if LIBXR_ESP_UART_HAS_AHB_GDMA
  if (uart->dma_backend_enabled_)
  {
    return uart->tx_dma_model_.Submit(uart->execution_policy_, in_isr);
  }
#endif
  return uart->SubmitFifoTx(in_isr);
}

// RX is interrupt-driven. Publishing a read waiter does not need any extra
// backend kick here.
// RX 由中断驱动，因此这里只需要表明：发布读 waiter 时不需要额外 kick 后端。
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

  uart_hal_set_sclk(&uart_hal_, static_cast<soc_module_clk_t>(UART_CLOCK_SOURCE));
  if ((esp_clk_tree_src_get_freq_hz(static_cast<soc_module_clk_t>(UART_CLOCK_SOURCE),
                                    ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED,
                                    &uart_sclk_hz_) != ESP_OK) ||
      (uart_sclk_hz_ == 0U))
  {
    return ErrorCode::INIT_ERR;
  }

  if (!ApplyConfigPayload(config_))
  {
    return ErrorCode::INIT_ERR;
  }
  uart_hw_enabled_ = true;

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

// Active state below belongs to the FIFO TX backend.
void IRAM_ATTR ESP32UART::ClearActiveTx()
{
  tx_active_length_ = 0U;
  tx_active_offset_ = 0U;
  tx_active_valid_ = false;
}

// RX bytes are pushed opportunistically until the software queue is full. The caller
// services pending reads only after descriptor/FIFO ownership is in a callback-safe
// state. RX 字节会尽量推进软件队列，直到队列满；完成后按批次触发待读回调。
bool IRAM_ATTR ESP32UART::PushRxBytes(const uint8_t* data, size_t size)
{
  size_t offset = 0;
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
  }
  return offset != 0U;
}

}  // namespace LibXR
