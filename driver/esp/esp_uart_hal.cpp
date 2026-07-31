#include "esp_uart_hal.hpp"

#include "driver/gpio.h"
#include "esp_clk_tree.h"
#include "esp_idf_version.h"
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wliteral-suffix"
#endif
#include "esp_private/uart_share_hw_ctrl.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include "esp_rom_gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "hal/uart_ll.h"
#include "sdkconfig.h"
#include "soc/gpio_sig_map.h"
#include "soc/uart_periph.h"

namespace
{

struct ResolvedConfig
{
  uart_word_length_t word_length;
  uart_stop_bits_t stop_bits;
  uart_parity_t parity;
};

bool ResolveWordLength(uint8_t data_bits, uart_word_length_t& out)
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

bool ResolveStopBits(uint8_t stop_bits, uart_stop_bits_t& out)
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

bool ResolveParity(LibXR::UART::Parity parity, uart_parity_t& out)
{
  switch (parity)
  {
    case LibXR::UART::Parity::NO_PARITY:
      out = UART_PARITY_DISABLE;
      return true;
    case LibXR::UART::Parity::EVEN:
      out = UART_PARITY_EVEN;
      return true;
    case LibXR::UART::Parity::ODD:
      out = UART_PARITY_ODD;
      return true;
    default:
      return false;
  }
}

bool IsBaudrateRepresentable(uint32_t baudrate, uint32_t source_clock_hz)
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

bool ResolveConfig(LibXR::UART::Configuration config, uint32_t source_clock_hz,
                   ResolvedConfig& out)
{
  return IsBaudrateRepresentable(config.baudrate, source_clock_hz) &&
         ResolveWordLength(config.data_bits, out.word_length) &&
         ResolveStopBits(config.stop_bits, out.stop_bits) &&
         ResolveParity(config.parity, out.parity);
}

}  // namespace

namespace LibXR::Detail
{

bool IsEspConsoleUartInUse(uart_port_t uart_num)
{
#if defined(CONFIG_ESP_CONSOLE_UART) && CONFIG_ESP_CONSOLE_UART
  return static_cast<int>(uart_num) == CONFIG_ESP_CONSOLE_UART_NUM;
#else
  (void)uart_num;
  return false;
#endif
}

bool IsCurrentTaskPinnedToOneCore()
{
#if defined(CONFIG_FREERTOS_SMP) && CONFIG_FREERTOS_SMP
  const UBaseType_t affinity = vTaskCoreAffinityGet(nullptr);
  return (affinity != 0U) && ((affinity & (affinity - 1U)) == 0U);
#else
  return xTaskGetCoreID(nullptr) != tskNO_AFFINITY;
#endif
}

int GetEspUartInterruptSource(uart_port_t uart_num)
{
  return uart_periph_signal[uart_num].irq;
}

ErrorCode InitEspUartHal(uart_port_t uart_num, uart_hal_context_t& uart_hal,
                         uint32_t& source_clock_hz)
{
  if ((uart_num >= UART_NUM_MAX) || (uart_num >= SOC_UART_HP_NUM))
  {
    return ErrorCode::NOT_SUPPORT;
  }

  uart_hal.dev = UART_LL_GET_HW(uart_num);
  if (uart_hal.dev == nullptr)
  {
    return ErrorCode::NOT_SUPPORT;
  }

  HP_UART_BUS_CLK_ATOMIC()
  {
    uart_ll_enable_bus_clock(uart_num, true);
    uart_ll_reset_register(uart_num);
  }
  uart_hal_init(&uart_hal, uart_num);

  HP_UART_SRC_CLK_ATOMIC()
  {
    uart_ll_sclk_enable(uart_hal.dev);
    uart_hal_set_sclk(&uart_hal, static_cast<soc_module_clk_t>(ESP_UART_CLOCK_SOURCE));
  }
  if ((esp_clk_tree_src_get_freq_hz(static_cast<soc_module_clk_t>(ESP_UART_CLOCK_SOURCE),
                                    ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED,
                                    &source_clock_hz) != ESP_OK) ||
      (source_clock_hz == 0U))
  {
    return ErrorCode::INIT_ERR;
  }

  return ErrorCode::OK;
}

ErrorCode ValidateEspUartConfig(UART::Configuration config, uint32_t source_clock_hz,
                                bool uart_hw_enabled)
{
  if (!uart_hw_enabled)
  {
    return ErrorCode::STATE_ERR;
  }
  ResolvedConfig resolved{};
  if (!ResolveConfig(config, source_clock_hz, resolved))
  {
    return ErrorCode::ARG_ERR;
  }
  return ErrorCode::OK;
}

bool ApplyEspUartConfig(uart_hal_context_t& uart_hal, UART::Configuration config,
                        uint32_t source_clock_hz)
{
  ResolvedConfig resolved{};
  if (!ResolveConfig(config, source_clock_hz, resolved))
  {
    return false;
  }

  bool baudrate_applied = true;
  HP_UART_SRC_CLK_ATOMIC()
  {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
    baudrate_applied = uart_hal_set_baudrate(&uart_hal, config.baudrate, source_clock_hz);
#else
    uart_hal_set_baudrate(&uart_hal, config.baudrate, source_clock_hz);
#endif
  }
  if (!baudrate_applied)
  {
    return false;
  }

  uart_hal_set_data_bit_num(&uart_hal, resolved.word_length);
  uart_hal_set_stop_bits(&uart_hal, resolved.stop_bits);
  uart_hal_set_parity(&uart_hal, resolved.parity);
  uart_hal_set_hw_flow_ctrl(&uart_hal, UART_HW_FLOWCTRL_DISABLE, 0);
  uart_hal_set_mode(&uart_hal, UART_MODE_UART);
  return true;
}

ErrorCode ConfigureEspUartPins(uart_port_t uart_num, int tx_pin, int rx_pin, int rts_pin,
                               int cts_pin)
{
  if (tx_pin >= 0)
  {
    if (!GPIO_IS_VALID_OUTPUT_GPIO(tx_pin))
    {
      return ErrorCode::ARG_ERR;
    }
    esp_rom_gpio_pad_select_gpio(static_cast<uint32_t>(tx_pin));
    esp_rom_gpio_connect_out_signal(
        tx_pin, UART_PERIPH_SIGNAL(uart_num, SOC_UART_TX_PIN_IDX), false, false);
  }

  if (rx_pin >= 0)
  {
    if (!GPIO_IS_VALID_GPIO(rx_pin))
    {
      return ErrorCode::ARG_ERR;
    }
    gpio_input_enable(static_cast<gpio_num_t>(rx_pin));
    esp_rom_gpio_connect_in_signal(
        rx_pin, UART_PERIPH_SIGNAL(uart_num, SOC_UART_RX_PIN_IDX), false);
  }

  if (rts_pin >= 0)
  {
    if (!GPIO_IS_VALID_OUTPUT_GPIO(rts_pin))
    {
      return ErrorCode::ARG_ERR;
    }
    esp_rom_gpio_pad_select_gpio(static_cast<uint32_t>(rts_pin));
    esp_rom_gpio_connect_out_signal(
        rts_pin, UART_PERIPH_SIGNAL(uart_num, SOC_UART_RTS_PIN_IDX), false, false);
  }

  if (cts_pin >= 0)
  {
    if (!GPIO_IS_VALID_GPIO(cts_pin))
    {
      return ErrorCode::ARG_ERR;
    }
    gpio_pullup_en(static_cast<gpio_num_t>(cts_pin));
    gpio_input_enable(static_cast<gpio_num_t>(cts_pin));
    esp_rom_gpio_connect_in_signal(
        cts_pin, UART_PERIPH_SIGNAL(uart_num, SOC_UART_CTS_PIN_IDX), false);
  }
  return ErrorCode::OK;
}

void SetEspUartLoopback(uart_hal_context_t& uart_hal, bool enable)
{
  uart_ll_set_loop_back(uart_hal.dev, enable);
}

}  // namespace LibXR::Detail
