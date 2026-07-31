#pragma once

#include <cstdint>

#include "hal/uart_hal.h"
#include "hal/uart_types.h"
#include "soc/soc_caps.h"
#include "uart.hpp"

namespace LibXR::Detail
{

#if defined(SOC_UART_SUPPORT_XTAL_CLK) && SOC_UART_SUPPORT_XTAL_CLK
inline constexpr uart_sclk_t ESP_UART_CLOCK_SOURCE = UART_SCLK_XTAL;
inline constexpr bool ESP_UART_CLOCK_REQUIRES_APB_LOCK = false;
#else
inline constexpr uart_sclk_t ESP_UART_CLOCK_SOURCE = UART_SCLK_DEFAULT;
inline constexpr bool ESP_UART_CLOCK_REQUIRES_APB_LOCK = true;
#endif

/** @brief 检查 ESP-IDF 是否将所选 UART 保留为 console / Check whether ESP-IDF reserves
 * the selected UART for the console. */
bool IsEspConsoleUartInUse(uart_port_t uart_num);

/** @brief 检查当前 task 是否恰好固定到一个 CPU core / Check whether the current task is
 * pinned to exactly one CPU core. */
bool IsCurrentTaskPinnedToOneCore();

/** @brief 返回 UART 对应的 ESP-IDF interrupt source / Return the ESP-IDF interrupt
 * source for one UART peripheral. */
[[nodiscard]] int GetEspUartInterruptSource(uart_port_t uart_num);

/** @brief 初始化共用 UART HAL、时钟源和源时钟频率 / Initialize the common UART HAL,
 * clock source, and source-clock frequency. */
[[nodiscard]] ErrorCode InitEspUartHal(uart_port_t uart_num, uart_hal_context_t& uart_hal,
                                       uint32_t& source_clock_hz);

/** @brief 根据 ESP UART 时钟分频器验证 LibXR 配置 / Validate a LibXR UART
 * configuration against the ESP UART clock divider. */
[[nodiscard]] ErrorCode ValidateEspUartConfig(UART::Configuration config,
                                              uint32_t source_clock_hz,
                                              bool uart_hw_enabled);

/** @brief 向已初始化的 ESP UART HAL 应用帧格式和波特率 / Apply framing and baud rate to
 * an initialized ESP UART HAL context. */
[[nodiscard]] bool ApplyEspUartConfig(uart_hal_context_t& uart_hal,
                                      UART::Configuration config,
                                      uint32_t source_clock_hz);

/** @brief 将可选 TX、RX、RTS 和 CTS 引脚路由到 ESP UART / Route optional TX, RX, RTS,
 * and CTS pins to one ESP UART peripheral. */
[[nodiscard]] ErrorCode ConfigureEspUartPins(uart_port_t uart_num, int tx_pin, int rx_pin,
                                             int rts_pin, int cts_pin);

/** @brief 设置 ESP UART 外设内部 loopback 位 / Set the ESP UART peripheral's internal
 * loopback bit. */
void SetEspUartLoopback(uart_hal_context_t& uart_hal, bool enable);

}  // namespace LibXR::Detail
