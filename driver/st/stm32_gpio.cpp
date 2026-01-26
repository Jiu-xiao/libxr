#include "stm32_gpio.hpp"

#ifdef HAL_GPIO_MODULE_ENABLED

using namespace LibXR;

STM32GPIO* STM32GPIO::map[STM32_GPIO_EXTI_NUMBER] = {nullptr};

/**
 * @brief 获取 GPIO 扩展中断号
 *
 * @param pin 引脚
 * @return stm32_gpio_exti_t 中断号 Interrupt number
 */
stm32_gpio_exti_t STM32_GPIO_EXTI_GetID(uint16_t pin)
{  // NOLINT
  uint8_t pin_num = __builtin_ctz(pin);

  if (pin == 0 || pin_num > 15)
  {
    ASSERT(false);
    return STM32_GPIO_EXTI_NUMBER;
  }

#if defined(STM32F0) || defined(STM32G0) || defined(STM32L0)
  if (pin_num <= 1)
  {
    return STM32_GPIO_EXTI_0_1;
  }
  else if (pin_num <= 3)
  {
    return STM32_GPIO_EXTI_2_3;
  }
  else
  {
    return STM32_GPIO_EXTI_4_15;
  }

#else
  if (pin_num <= 4)
  {
    return static_cast<stm32_gpio_exti_t>(pin_num);
  }
  else if (pin_num <= 9)
  {
    return STM32_GPIO_EXTI_5_9;
  }
  else
  {
    return STM32_GPIO_EXTI_10_15;
  }

#endif

  return STM32_GPIO_EXTI_NUMBER;
}

STM32GPIO::STM32GPIO(GPIO_TypeDef* port, uint16_t pin, IRQn_Type irq)
    : port_(port), pin_(pin), irq_(irq)
{
  if (irq_ != NonMaskableInt_IRQn)
  {
    map[STM32_GPIO_EXTI_GetID(pin)] = this;
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
  auto id = STM32_GPIO_EXTI_GetID(GPIO_Pin);
  auto gpio = STM32GPIO::map[id];

  if (gpio)
  {
    gpio->callback_.Run(true);
  }
}

#endif
