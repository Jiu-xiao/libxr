#include <type_traits>

#include "esp_uart.hpp"
#include "esp_uart_fifo.hpp"

#ifndef LIBXR_ESP_UART_EXPECT_IRQ_SERIALIZATION
#error "Compile probe must declare the expected ESP UART execution policy."
#endif

static_assert(LibXR::Detail::ESP_UART_USES_IRQ_SERIALIZATION ==
              static_cast<bool>(LIBXR_ESP_UART_EXPECT_IRQ_SERIALIZATION));
static_assert(static_cast<bool>(LIBXR_SINGLE_CORE) !=
              static_cast<bool>(LIBXR_ESP_UART_EXPECT_IRQ_SERIALIZATION));

static_assert(std::is_base_of_v<LibXR::UART, LibXR::ESP32UartFifo>);
static_assert(
    std::is_constructible_v<LibXR::ESP32UartFifo, uart_port_t, int, int, int, int, size_t,
                            size_t, uint32_t, LibXR::UART::Configuration>);

#if LIBXR_ESP_UART_HAS_AHB_GDMA
static_assert(std::is_base_of_v<LibXR::UART, LibXR::ESP32UartDma>);
static_assert(
    std::is_constructible_v<LibXR::ESP32UartDma, uart_port_t, int, int, int, int, size_t,
                            size_t, uint32_t, LibXR::UART::Configuration>);
#endif

extern "C" void app_main(void)
{
  // This project is never flashed. Constructing the explicit class keeps its complete
  // backend dependency graph in the application ELF instead of proving only that its
  // translation unit can enter libxr.a.
  static LibXR::ESP32UartFifo fifo(UART_NUM_1, LibXR::ESP32UartFifo::PIN_NO_CHANGE,
                                   LibXR::ESP32UartFifo::PIN_NO_CHANGE,
                                   LibXR::ESP32UartFifo::PIN_NO_CHANGE,
                                   LibXR::ESP32UartFifo::PIN_NO_CHANGE, 32U, 32U, 2U,
                                   {115200U, LibXR::UART::Parity::NO_PARITY, 8U, 1U});
  (void)fifo;
}
