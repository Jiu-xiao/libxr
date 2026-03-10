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

extern "C" void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  const uint8_t LINE = STM32_GPIO_PinToLine(GPIO_Pin);
  if (auto* gpio = STM32GPIO::map[LINE])
  {
    gpio->callback_.Run(true);
  }
}

#endif
