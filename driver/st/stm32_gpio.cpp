#include "stm32_gpio.hpp"

#ifdef HAL_GPIO_MODULE_ENABLED

using namespace LibXR;

STM32GPIO* STM32GPIO::map[16] = {nullptr};

// NOLINTNEXTLINE
static inline uint8_t STM32_GPIO_PinToLine(uint16_t pin)
{
  ASSERT(pin != 0 && (pin & (pin - 1)) == 0);
  const uint8_t LINE = static_cast<uint8_t>(__builtin_ctz(static_cast<unsigned>(pin)));
  return LINE;
}

STM32GPIO::STM32GPIO(GPIO_TypeDef* port, uint16_t pin, IRQn_Type irq)
    : port_(port), pin_(pin), irq_(irq)
{
  if (irq_ != NonMaskableInt_IRQn)
  {
    map[STM32_GPIO_PinToLine(pin)] = this;
  }
}

ErrorCode STM32GPIO::EnableInterrupt()
{
  ASSERT(irq_ != NonMaskableInt_IRQn);
  HAL_NVIC_EnableIRQ(irq_);
  return ErrorCode::OK;
}

ErrorCode STM32GPIO::DisableInterrupt()
{
  ASSERT(irq_ != NonMaskableInt_IRQn);
  HAL_NVIC_DisableIRQ(irq_);
  return ErrorCode::OK;
}

static void STM32_GPIO_EXTI_Dispatch(uint16_t pin)
{
  const uint8_t LINE = STM32_GPIO_PinToLine(pin);
  if (auto* gpio = STM32GPIO::map[LINE])
  {
    gpio->callback_.Run(true);
  }
}

#if defined(STM32H5)
// H5 HAL replaces the legacy combined callback with edge-specific callbacks.
extern "C" void HAL_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin)
{
  STM32_GPIO_EXTI_Dispatch(GPIO_Pin);
}

extern "C" void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)
{
  STM32_GPIO_EXTI_Dispatch(GPIO_Pin);
}
#else
extern "C" void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  STM32_GPIO_EXTI_Dispatch(GPIO_Pin);
}
#endif

#endif
