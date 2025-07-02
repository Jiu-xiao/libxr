#pragma once

#include "gpio.hpp"
#include "main.h"

#ifdef HAL_GPIO_MODULE_ENABLED

typedef enum
{
#if defined(STM32F0) || defined(STM32G0) || defined(STM32L0)
  STM32_GPIO_EXTI_0_1,
  STM32_GPIO_EXTI_2_3,
  STM32_GPIO_EXTI_4_15,
#elif defined(STM32WB0)
  STM32_GPIO_EXTI_GPIOA,
  STM32_GPIO_EXTI_GPIOB,
#else
  STM32_GPIO_EXTI_0,
  STM32_GPIO_EXTI_1,
  STM32_GPIO_EXTI_2,
  STM32_GPIO_EXTI_3,
  STM32_GPIO_EXTI_4,
  STM32_GPIO_EXTI_5_9,
  STM32_GPIO_EXTI_10_15,
#endif
  STM32_GPIO_EXTI_NUMBER
} stm32_gpio_exti_t;

stm32_gpio_exti_t STM32_GPIO_EXTI_GetID(uint16_t pin);  // NOLINT

namespace LibXR
{
class STM32GPIO : public GPIO
{
 public:
  STM32GPIO(GPIO_TypeDef* port, uint16_t pin, IRQn_Type irq = NonMaskableInt_IRQn);

  bool Read();

  ErrorCode Write(bool value);

  ErrorCode EnableInterrupt();

  ErrorCode DisableInterrupt();

  ErrorCode SetConfig(Configuration config);

  static STM32GPIO* map[STM32_GPIO_EXTI_NUMBER];  // NOLINT

 private:
  GPIO_TypeDef* port_;
  uint16_t pin_;
  IRQn_Type irq_;
};

}  // namespace LibXR

#endif
