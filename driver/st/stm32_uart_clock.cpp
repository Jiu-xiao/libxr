#include "stm32_uart.hpp"

#ifdef HAL_UART_MODULE_ENABLED

using namespace LibXR;

namespace
{
[[nodiscard]] uint32_t GetUartKernelClock(UART_HandleTypeDef* uart_handle)
{
#if defined(STM32WB0x_HAL_H) || defined(STM32WL3x_HAL_H)
#if defined(RCC_PERIPHCLK_LPUART1)
  if (UART_INSTANCE_LOWPOWER(uart_handle))
  {
    return HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_LPUART1);
  }
#endif
  return UART_PERIPHCLK;
#elif defined(UART_GETCLOCKSOURCE)
#if defined(STM32U0xx_HAL_H) || defined(STM32H5) || defined(STM32U5) || \
    defined(STM32U3) || defined(STM32N6)
  // These HALs return an RCC_PERIPHCLK_* selector, not a UART clock-source enum.
#if defined(STM32N6)
  uint64_t clock_source = 0U;
#else
  uint32_t clock_source = 0U;
#endif
  UART_GETCLOCKSOURCE(uart_handle, clock_source);
  return HAL_RCCEx_GetPeriphCLKFreq(clock_source);
#else
  UART_ClockSourceTypeDef clock_source{};
  UART_GETCLOCKSOURCE(uart_handle, clock_source);

#if defined(STM32H7RS)
  switch (clock_source)
  {
    case UART_CLOCKSOURCE_PCLK1:
      return HAL_RCC_GetPCLK1Freq();
    case UART_CLOCKSOURCE_PCLK2:
      return HAL_RCC_GetPCLK2Freq();
    case UART_CLOCKSOURCE_PCLK4:
      return HAL_RCC_GetPCLK4Freq();
    case UART_CLOCKSOURCE_PLL2Q:
      return HAL_RCC_GetPLL2QFreq();
    case UART_CLOCKSOURCE_PLL3Q:
      return HAL_RCC_GetPLL3QFreq();
    case UART_CLOCKSOURCE_HSI:
      if (__HAL_RCC_GET_FLAG(RCC_FLAG_HSIDIV) != 0U)
      {
        return static_cast<uint32_t>(HSI_VALUE >> (__HAL_RCC_GET_HSI_DIVIDER() >> 3U));
      }
      return static_cast<uint32_t>(HSI_VALUE);
    case UART_CLOCKSOURCE_CSI:
      return static_cast<uint32_t>(CSI_VALUE);
    case UART_CLOCKSOURCE_LSE:
      return static_cast<uint32_t>(LSE_VALUE);
    default:
      return 0U;
  }
#elif defined(RCC_D2CFGR_D2PPRE1) || defined(RCC_CDCFGR2_CDPPRE1)
  switch (clock_source)
  {
    case UART_CLOCKSOURCE_D2PCLK1:
      return HAL_RCC_GetPCLK1Freq();
    case UART_CLOCKSOURCE_D2PCLK2:
      return HAL_RCC_GetPCLK2Freq();
    case UART_CLOCKSOURCE_D3PCLK1:
      return HAL_RCCEx_GetD3PCLK1Freq();
    case UART_CLOCKSOURCE_PLL2:
    {
      PLL2_ClocksTypeDef clocks{};
      HAL_RCCEx_GetPLL2ClockFreq(&clocks);
      return clocks.PLL2_Q_Frequency;
    }
    case UART_CLOCKSOURCE_PLL3:
    {
      PLL3_ClocksTypeDef clocks{};
      HAL_RCCEx_GetPLL3ClockFreq(&clocks);
      return clocks.PLL3_Q_Frequency;
    }
    case UART_CLOCKSOURCE_HSI:
      if (__HAL_RCC_GET_FLAG(RCC_FLAG_HSIDIV) != 0U)
      {
        return static_cast<uint32_t>(HSI_VALUE >> (__HAL_RCC_GET_HSI_DIVIDER() >> 3U));
      }
      return static_cast<uint32_t>(HSI_VALUE);
    case UART_CLOCKSOURCE_CSI:
      return static_cast<uint32_t>(CSI_VALUE);
    case UART_CLOCKSOURCE_LSE:
      return static_cast<uint32_t>(LSE_VALUE);
    default:
      return 0U;
  }
#else
  switch (clock_source)
  {
    case UART_CLOCKSOURCE_PCLK1:
      return HAL_RCC_GetPCLK1Freq();
#if defined(RCC_CFGR_PPRE2)
    case UART_CLOCKSOURCE_PCLK2:
      return HAL_RCC_GetPCLK2Freq();
#endif
    case UART_CLOCKSOURCE_HSI:
#if defined(__HAL_RCC_GET_HSIKER_DIVIDER) && defined(RCC_CR_HSIKERDIV_Pos)
      return static_cast<uint32_t>(
          HSI_VALUE / ((__HAL_RCC_GET_HSIKER_DIVIDER() >> RCC_CR_HSIKERDIV_Pos) + 1U));
#elif defined(RCC_CR_HSIDIVEN)
      return (__HAL_RCC_GET_FLAG(RCC_FLAG_HSIDIV) != 0U)
                 ? static_cast<uint32_t>(HSI_VALUE >> 2U)
                 : static_cast<uint32_t>(HSI_VALUE);
#else
      return static_cast<uint32_t>(HSI_VALUE);
#endif
    case UART_CLOCKSOURCE_SYSCLK:
      return HAL_RCC_GetSysClockFreq();
    case UART_CLOCKSOURCE_LSE:
      return static_cast<uint32_t>(LSE_VALUE);
    default:
      return 0U;
  }
#endif
#endif
#else
  bool uses_pclk2 = false;
#ifdef USART1
  uses_pclk2 = uses_pclk2 || (uart_handle->Instance == USART1);
#endif
#ifdef USART6
  uses_pclk2 = uses_pclk2 || (uart_handle->Instance == USART6);
#endif
#ifdef UART9
  uses_pclk2 = uses_pclk2 || (uart_handle->Instance == UART9);
#endif
#ifdef UART10
  uses_pclk2 = uses_pclk2 || (uart_handle->Instance == UART10);
#endif
  return uses_pclk2 ? HAL_RCC_GetPCLK2Freq() : HAL_RCC_GetPCLK1Freq();
#endif
}

[[nodiscard]] bool UartBaudrateSupported(UART_HandleTypeDef* uart_handle,
                                         uint32_t baudrate)
{
  constexpr uint32_t uart_brr_min = 0x10U;
  constexpr uint32_t uart_brr_max = 0xFFFFU;

  if ((baudrate == 0U) || (IS_UART_BAUDRATE(baudrate) == 0U))
  {
    return false;
  }

  const uint32_t kernel_clock = GetUartKernelClock(uart_handle);
  if (kernel_clock == 0U)
  {
    return false;
  }

#if defined(UART_PRESCALER_DIV1)
  if (IS_UART_PRESCALER(uart_handle->Init.ClockPrescaler) == 0U)
  {
    return false;
  }
#endif

#if defined(UART_INSTANCE_LOWPOWER)
  if (UART_INSTANCE_LOWPOWER(uart_handle))
  {
    constexpr uint32_t lpuart_brr_min = 0x300U;
    constexpr uint32_t lpuart_brr_max = 0xFFFFFU;
#if defined(UART_PRESCALER_DIV1)
    const uint32_t prescaler = UARTPrescTable[uart_handle->Init.ClockPrescaler];
    const uint32_t prescaled_clock = kernel_clock / prescaler;
    const uint32_t divisor =
        UART_DIV_LPUART(kernel_clock, baudrate, uart_handle->Init.ClockPrescaler);
#else
    const uint32_t prescaled_clock = kernel_clock;
    const uint32_t divisor = UART_DIV_LPUART(kernel_clock, baudrate);
#endif
    const uint64_t baudrate_wide = baudrate;
    return (static_cast<uint64_t>(prescaled_clock) >= (3U * baudrate_wide)) &&
           (static_cast<uint64_t>(prescaled_clock) <= (4096U * baudrate_wide)) &&
           (divisor >= lpuart_brr_min) && (divisor <= lpuart_brr_max);
  }
#endif

#if defined(UART_GETCLOCKSOURCE) || defined(STM32WB0x_HAL_H) || defined(STM32WL3x_HAL_H)
  uint32_t divisor = 0U;
  if (uart_handle->Init.OverSampling == UART_OVERSAMPLING_8)
  {
#if defined(UART_PRESCALER_DIV1)
    divisor =
        UART_DIV_SAMPLING8(kernel_clock, baudrate, uart_handle->Init.ClockPrescaler);
#else
    divisor = UART_DIV_SAMPLING8(kernel_clock, baudrate);
#endif
  }
  else if (uart_handle->Init.OverSampling == UART_OVERSAMPLING_16)
  {
#if defined(UART_PRESCALER_DIV1)
    divisor =
        UART_DIV_SAMPLING16(kernel_clock, baudrate, uart_handle->Init.ClockPrescaler);
#else
    divisor = UART_DIV_SAMPLING16(kernel_clock, baudrate);
#endif
  }
  return (divisor >= uart_brr_min) && (divisor <= uart_brr_max);
#else
  uint32_t brr = 0U;
#if defined(USART_CR1_OVER8)
  if (uart_handle->Init.OverSampling == UART_OVERSAMPLING_8)
  {
    brr = UART_BRR_SAMPLING8(kernel_clock, baudrate);
  }
  else if (uart_handle->Init.OverSampling == UART_OVERSAMPLING_16)
#endif
  {
    brr = UART_BRR_SAMPLING16(kernel_clock, baudrate);
  }
  return (brr >= uart_brr_min) && (brr <= uart_brr_max);
#endif
}
}  // namespace

ErrorCode STM32UART::ValidateConfig(UART::Configuration config) const
{
  if (!UartBaudrateSupported(uart_handle_, config.baudrate) || (config.data_bits != 8U) ||
      ((config.stop_bits != 1U) && (config.stop_bits != 2U)))
  {
    return ErrorCode::ARG_ERR;
  }
  if ((config.parity != UART::Parity::NO_PARITY) &&
      (config.parity != UART::Parity::EVEN) && (config.parity != UART::Parity::ODD))
  {
    return ErrorCode::ARG_ERR;
  }
  return ErrorCode::OK;
}

#endif
