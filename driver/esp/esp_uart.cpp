#include "esp_uart.hpp"

#if LIBXR_ESP_UART_HAS_AHB_GDMA

#include <algorithm>

#include "esp_attr.h"
#include "esp_clk_tree.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_private/esp_cache_private.h"
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wliteral-suffix"
#endif
#include "esp_private/uart_share_hw_ctrl.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include "esp_rom_gpio.h"
#include "hal/uart_ll.h"
#include "soc/gpio_sig_map.h"
#include "soc/uart_periph.h"

namespace
{
#if defined(SOC_UART_SUPPORT_XTAL_CLK) && SOC_UART_SUPPORT_XTAL_CLK
constexpr uart_sclk_t UART_CLOCK_SOURCE = UART_SCLK_XTAL;
constexpr bool UART_CLOCK_REQUIRES_APB_LOCK = false;
#else
constexpr uart_sclk_t UART_CLOCK_SOURCE = UART_SCLK_DEFAULT;
constexpr bool UART_CLOCK_REQUIRES_APB_LOCK = true;
#endif

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

bool ESP32UartDma::IsCurrentTaskPinned()
{
#if defined(CONFIG_FREERTOS_SMP) && CONFIG_FREERTOS_SMP
  const UBaseType_t affinity = vTaskCoreAffinityGet(nullptr);
  return (affinity != 0U) && ((affinity & (affinity - 1U)) == 0U);
#else
  return xTaskGetCoreID(nullptr) != tskNO_AFFINITY;
#endif
}

ErrorCode ESP32UartDma::InitPowerManagement()
{
#if defined(CONFIG_PM_ENABLE) && CONFIG_PM_ENABLE
  if (pm_lock_ != nullptr)
  {
    return ErrorCode::STATE_ERR;
  }

  const esp_pm_lock_type_t lock_type =
      UART_CLOCK_REQUIRES_APB_LOCK ? ESP_PM_APB_FREQ_MAX : ESP_PM_NO_LIGHT_SLEEP;
  if (esp_pm_lock_create(lock_type, 0, "libxr_uart", &pm_lock_) != ESP_OK)
  {
    return ErrorCode::INIT_ERR;
  }
  if (esp_pm_lock_acquire(pm_lock_) != ESP_OK)
  {
    (void)esp_pm_lock_delete(pm_lock_);
    pm_lock_ = nullptr;
    return ErrorCode::INIT_ERR;
  }
#else
  (void)UART_CLOCK_REQUIRES_APB_LOCK;
#endif
  return ErrorCode::OK;
}

void IRAM_ATTR ESP32UartDma::SetIrqDomainEnabled(bool enabled) noexcept
{
  portENTER_CRITICAL_SAFE(&irq_domain_lock_);
  SetIrqDomainEnabledLocked(enabled);
  portEXIT_CRITICAL_SAFE(&irq_domain_lock_);
}

void IRAM_ATTR ESP32UartDma::SetIrqDomainEnabledLocked(bool enabled) noexcept
{
  const bool masked = !enabled;
  if (irq_domain_masked_ == masked)
  {
    return;
  }

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
  set_handle(tx_gdma_intr_handle_);
  if (rx_gdma_intr_handle_ != tx_gdma_intr_handle_)
  {
    set_handle(rx_gdma_intr_handle_);
  }
  REQUIRE_FROM_CALLBACK(success, in_isr);
  irq_domain_masked_ = masked;
}

ESP32UartDma::TxStorage ESP32UartDma::AllocateTxStorage(size_t block_size)
{
  TxStorage storage{};
  if (block_size == 0U)
  {
    return storage;
  }

  size_t cache_line_size = 1U;
  if (esp_cache_get_alignment(MALLOC_CAP_INTERNAL, &cache_line_size) != ESP_OK)
  {
    return storage;
  }
  cache_line_size = std::max<size_t>(cache_line_size, 1U);
  const size_t alignment = std::max<size_t>(alignof(size_t), cache_line_size);
  if (((alignment & (alignment - 1U)) != 0U) ||
      (block_size > (SIZE_MAX - (alignment - 1U))))
  {
    return storage;
  }

  const size_t block_stride = ((block_size + alignment - 1U) / alignment) * alignment;
  if ((block_stride == 0U) || (block_stride > (SIZE_MAX / 2U)))
  {
    return storage;
  }

  const size_t size = block_stride * 2U;
  auto* data = static_cast<uint8_t*>(heap_caps_aligned_alloc(
      alignment, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  if (data == nullptr)
  {
    return storage;
  }

  storage.data_ = data;
  storage.size_ = size;
  storage.block_stride_ = block_stride;
  storage.cache_line_size_ = cache_line_size;
  return storage;
}

ESP32UartDma::ESP32UartDma(uart_port_t uart_num, int tx_pin, int rx_pin, int rts_pin,
                           int cts_pin, size_t rx_buffer_size, size_t tx_buffer_size,
                           uint32_t tx_queue_size, UART::Configuration config)
    : UART(&_read_port, &_write_port),
      uart_num_(uart_num),
      tx_pin_(tx_pin),
      rx_pin_(rx_pin),
      rts_pin_(rts_pin),
      cts_pin_(cts_pin),
      config_(config),
      execution_policy_(*this),
      tx_storage_(AllocateTxStorage(tx_buffer_size)),
      _read_port(rx_buffer_size),
      _write_port(tx_queue_size, tx_buffer_size),
      dma_model_(*this, execution_policy_, _write_port,
                 RawData(tx_storage_.data_, tx_storage_.size_))
{
  REQUIRE(!IsConsoleUartInUse(uart_num_));
  REQUIRE(uart_num_ < UART_NUM_MAX);
  REQUIRE(uart_num_ < SOC_UART_HP_NUM);
  REQUIRE(rx_buffer_size > 0U);
  REQUIRE(tx_buffer_size > 0U);
  if constexpr (Detail::ESP_UART_USES_IRQ_SERIALIZATION)
  {
    REQUIRE(IsCurrentTaskPinned());
  }

  _read_port = ReadFun;
  _write_port = WriteFun;

  REQUIRE(InitUartHardware() == ErrorCode::OK);
  REQUIRE(InitPowerManagement() == ErrorCode::OK);
  REQUIRE(InitDmaBackend() == ErrorCode::OK);
  REQUIRE(InstallUartIsr() == ErrorCode::OK);
  ConfigureDmaErrorInterruptPath();
  DEV_ASSERT(esp_intr_get_cpu(uart_intr_handle_) ==
             esp_intr_get_cpu(tx_gdma_intr_handle_));
  DEV_ASSERT(esp_intr_get_cpu(uart_intr_handle_) ==
             esp_intr_get_cpu(rx_gdma_intr_handle_));
  SetIrqDomainEnabled(true);
}

void IRAM_ATTR ESP32UartDma::UartIsrEntry(void* arg)
{
  auto* self = static_cast<ESP32UartDma*>(arg);
  if (self == nullptr)
  {
    return;
  }
  (void)self->dma_model_.InvokeIrq(
      [self]() noexcept { return self->ServiceDmaUartStatus(true); }, true);
}

ErrorCode ESP32UartDma::InstallUartIsr()
{
  if (uart_isr_installed_)
  {
    return ErrorCode::OK;
  }

  constexpr int UART_INTR_FLAGS = ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_INTRDISABLED;
  const esp_err_t result =
      esp_intr_alloc(uart_periph_signal[uart_num_].irq, UART_INTR_FLAGS, UartIsrEntry,
                     this, &uart_intr_handle_);
  if (result != ESP_OK)
  {
    return ErrorCode::INIT_ERR;
  }
  uart_isr_installed_ = true;
  return ErrorCode::OK;
}

void ESP32UartDma::ConfigureDmaErrorInterruptPath()
{
  uart_hal_clr_intsts_mask(&uart_hal_, DMA_UART_ERROR_INTR_MASK);
  uart_hal_ena_intr_mask(&uart_hal_, DMA_UART_ERROR_INTR_MASK);
}

void ESP32UartDma::ArmConfigTxIdleInterrupt()
{
  if (config_tx_idle_interrupt_armed_)
  {
    return;
  }

  // TX_DONE belongs to the current TX generation: StartDmaTx() cleared any stale raw
  // bit before launching it. Publish the armed state before enabling the source so an
  // immediate IRQ can retain the valid carrier when the UART FSM is not idle yet.
  config_tx_idle_interrupt_armed_ = true;
  uart_hal_ena_intr_mask(&uart_hal_, UART_INTR_TX_DONE);
}

void ESP32UartDma::DisarmConfigTxIdleInterrupt()
{
  if (!config_tx_idle_interrupt_armed_)
  {
    return;
  }
  uart_hal_disable_intr_mask(&uart_hal_, UART_INTR_TX_DONE);
  uart_hal_clr_intsts_mask(&uart_hal_, UART_INTR_TX_DONE);
  config_tx_idle_interrupt_armed_ = false;
}

ErrorCode ESP32UartDma::SetConfig(UART::Configuration config)
{
  return dma_model_.SetConfig(config, xPortInIsrContext() != pdFALSE);
}

bool ESP32UartDma::IsBaudrateRepresentable(uint32_t baudrate, uint32_t source_clock_hz)
{
  if ((baudrate == 0U) || (source_clock_hz == 0U))
  {
    return false;
  }

#if defined(UART_SCLK_DIV_NUM_V)
  constexpr uint32_t MAX_SOURCE_DIV = UART_SCLK_DIV_NUM_V + 1U;
#elif defined(PCR_UART0_SCLK_DIV_NUM_V)
  constexpr uint32_t MAX_SOURCE_DIV = PCR_UART0_SCLK_DIV_NUM_V + 1U;
#elif defined(HP_SYS_CLKRST_REG_UART0_SCLK_DIV_NUM_V)
  constexpr uint32_t MAX_SOURCE_DIV = HP_SYS_CLKRST_REG_UART0_SCLK_DIV_NUM_V + 1U;
#else
  const uint64_t clock_dividend = static_cast<uint64_t>(source_clock_hz) << 4U;
  const uint64_t clock_divider = clock_dividend / baudrate;
  return (clock_divider != 0U) && ((clock_divider >> 4U) <= UART_CLKDIV_V);
#endif

#if defined(UART_SCLK_DIV_NUM_V) || defined(PCR_UART0_SCLK_DIV_NUM_V) || \
    defined(HP_SYS_CLKRST_REG_UART0_SCLK_DIV_NUM_V)
  const uint64_t max_uart_divider = UART_CLKDIV_V;
  const uint64_t denominator = max_uart_divider * baudrate;
  const uint64_t source_divider =
      (static_cast<uint64_t>(source_clock_hz) + denominator - 1U) / denominator;
  if ((source_divider == 0U) || (source_divider > MAX_SOURCE_DIV))
  {
    return false;
  }

  const uint64_t clock_divider = (static_cast<uint64_t>(source_clock_hz) << 4U) /
                                 (static_cast<uint64_t>(baudrate) * source_divider);
  return (clock_divider != 0U) && ((clock_divider >> 4U) <= max_uart_divider);
#endif
}

ErrorCode ESP32UartDma::ValidateConfig(UART::Configuration config) const
{
  if (!uart_hw_enabled_)
  {
    return ErrorCode::STATE_ERR;
  }
  if (((config.parity != UART::Parity::NO_PARITY) &&
       (config.parity != UART::Parity::EVEN) && (config.parity != UART::Parity::ODD)) ||
      !IsBaudrateRepresentable(config.baudrate, uart_sclk_hz_))
  {
    return ErrorCode::ARG_ERR;
  }

  uart_word_length_t word_length = UART_DATA_8_BITS;
  uart_stop_bits_t stop_bits = UART_STOP_BITS_1;
  if (!ResolveWordLength(config.data_bits, word_length) ||
      !ResolveStopBits(config.stop_bits, stop_bits))
  {
    return ErrorCode::ARG_ERR;
  }
  return ErrorCode::OK;
}

UartDmaControlResult ESP32UartDma::AdvanceConfig(UART::Configuration config,
                                                 bool active_tx, bool in_isr)
{
  if (!config_waiting_tx_idle_)
  {
    config_old_tx_terminal_ = StopAndResetDma(active_tx, in_isr);
    config_waiting_tx_idle_ = true;
  }

  if (!uart_hal_is_tx_idle(&uart_hal_))
  {
    ArmConfigTxIdleInterrupt();
    if (!uart_hal_is_tx_idle(&uart_hal_))
    {
      return UartDmaControlResult::Pending();
    }
  }

  DisarmConfigTxIdleInterrupt();
  const UartOldTxTerminal terminal = config_old_tx_terminal_;
  config_old_tx_terminal_ = UartOldTxTerminal::NONE;
  config_waiting_tx_idle_ = false;
  REQUIRE_FROM_CALLBACK(ApplyConfigPayload(config), in_isr);
  return UartDmaControlResult::Completed(terminal);
}

UartDmaControlProgress ESP32UartDma::CompleteConfig(bool in_isr)
{
  REQUIRE_FROM_CALLBACK(ResetAndRestartRxDma(), in_isr);
  return UartDmaControlProgress::COMPLETED;
}

bool ESP32UartDma::ApplyConfigPayload(UART::Configuration config)
{
  uart_word_length_t word_length = UART_DATA_8_BITS;
  uart_stop_bits_t stop_bits = UART_STOP_BITS_1;
  if (((config.parity != UART::Parity::NO_PARITY) &&
       (config.parity != UART::Parity::EVEN) && (config.parity != UART::Parity::ODD)) ||
      !ResolveWordLength(config.data_bits, word_length) ||
      !ResolveStopBits(config.stop_bits, stop_bits) ||
      !IsBaudrateRepresentable(config.baudrate, uart_sclk_hz_))
  {
    return false;
  }

  bool baudrate_applied = true;
  HP_UART_SRC_CLK_ATOMIC()
  {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
    baudrate_applied = uart_hal_set_baudrate(&uart_hal_, config.baudrate, uart_sclk_hz_);
#else
    uart_hal_set_baudrate(&uart_hal_, config.baudrate, uart_sclk_hz_);
#endif
  }
  if (!baudrate_applied)
  {
    return false;
  }

  uart_hal_set_data_bit_num(&uart_hal_, word_length);
  uart_hal_set_stop_bits(&uart_hal_, stop_bits);
  uart_hal_set_parity(&uart_hal_, ResolveParity(config.parity));
  uart_hal_set_hw_flow_ctrl(&uart_hal_, UART_HW_FLOWCTRL_DISABLE, 0);
  uart_hal_set_mode(&uart_hal_, UART_MODE_UART);
  uart_hal_txfifo_rst(&uart_hal_);
  uart_hal_rxfifo_rst(&uart_hal_);
  uart_hal_clr_intsts_mask(&uart_hal_, UINT32_MAX);

  config_ = config;
  return true;
}

ErrorCode ESP32UartDma::SetLoopback(bool enable)
{
  if (!uart_hw_enabled_)
  {
    return ErrorCode::STATE_ERR;
  }
  uart_ll_set_loop_back(uart_hal_.dev, enable);
  return ErrorCode::OK;
}

ErrorCode IRAM_ATTR ESP32UartDma::WriteFun(WritePort& port, bool in_isr)
{
  auto* uart = LibXR::ContainerOf(&port, &ESP32UartDma::_write_port);
  return uart->dma_model_.Submit(in_isr);
}

ErrorCode ESP32UartDma::ReadFun(ReadPort&, bool) { return ErrorCode::PENDING; }

bool ESP32UartDma::ResolveWordLength(uint8_t data_bits, uart_word_length_t& out)
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

bool ESP32UartDma::ResolveStopBits(uint8_t stop_bits, uart_stop_bits_t& out)
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

uart_parity_t ESP32UartDma::ResolveParity(UART::Parity parity)
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

ErrorCode ESP32UartDma::InitUartHardware()
{
  if ((uart_num_ >= UART_NUM_MAX) || (uart_num_ >= SOC_UART_HP_NUM))
  {
    return ErrorCode::NOT_SUPPORT;
  }

  uart_hal_.dev = UART_LL_GET_HW(uart_num_);
  if (uart_hal_.dev == nullptr)
  {
    return ErrorCode::NOT_SUPPORT;
  }

  HP_UART_BUS_CLK_ATOMIC()
  {
    uart_ll_enable_bus_clock(uart_num_, true);
    uart_ll_reset_register(uart_num_);
  }
  uart_hal_init(&uart_hal_, uart_num_);

  HP_UART_SRC_CLK_ATOMIC()
  {
    uart_ll_sclk_enable(uart_hal_.dev);
    uart_hal_set_sclk(&uart_hal_, static_cast<soc_module_clk_t>(UART_CLOCK_SOURCE));
  }
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

ErrorCode ESP32UartDma::ConfigurePins()
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

bool IRAM_ATTR ESP32UartDma::PushRxBytes(const uint8_t* data, size_t size)
{
  size_t offset = 0U;
  while (offset < size)
  {
    const size_t free_space = read_port_->queue_data_->EmptySize();
    if (free_space == 0U)
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

#endif  // LIBXR_ESP_UART_HAS_AHB_GDMA
