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
  STM32GPIO(GPIO_TypeDef* port, uint16_t pin, IRQn_Type irq = NonMaskableInt_IRQn)
      : port_(port), pin_(pin), irq_(irq)
  {
    if (irq_ != NonMaskableInt_IRQn)
    {
      map[STM32_GPIO_EXTI_GetID(pin)] = this;
    }
  }

  bool Read() { return HAL_GPIO_ReadPin(port_, pin_) == GPIO_PIN_SET; }

  ErrorCode Write(bool value)
  {
    HAL_GPIO_WritePin(port_, pin_, value ? GPIO_PIN_SET : GPIO_PIN_RESET);
    return ErrorCode::OK;
  }

  ErrorCode EnableInterrupt()
  {
    ASSERT(irq_ != NonMaskableInt_IRQn);
    HAL_NVIC_EnableIRQ(irq_);
    return ErrorCode::OK;
  }

  ErrorCode DisableInterrupt()
  {
    ASSERT(irq_ != NonMaskableInt_IRQn);
    HAL_NVIC_DisableIRQ(irq_);
    return ErrorCode::OK;
  }

  ErrorCode SetConfig(Configuration config)
  {
    GPIO_InitTypeDef gpio_init = {};

    HAL_GPIO_DeInit(port_, pin_);

    gpio_init.Pin = pin_;

    switch (config.direction)
    {
      case Direction::INPUT:
        gpio_init.Mode = GPIO_MODE_INPUT;
        break;
      case Direction::OUTPUT_PUSH_PULL:
        gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
        break;
      case Direction::OUTPUT_OPEN_DRAIN:
        gpio_init.Mode = GPIO_MODE_OUTPUT_OD;
        break;
      case Direction::FALL_INTERRUPT:
        gpio_init.Mode = GPIO_MODE_IT_FALLING;
        break;
      case Direction::RISING_INTERRUPT:
        gpio_init.Mode = GPIO_MODE_IT_RISING;
        break;
      case Direction::FALL_RISING_INTERRUPT:
        gpio_init.Mode = GPIO_MODE_IT_RISING_FALLING;
        break;
    }

    switch (config.pull)
    {
      case Pull::NONE:
        gpio_init.Pull = GPIO_NOPULL;
        break;
      case Pull::UP:
        gpio_init.Pull = GPIO_PULLUP;
        break;
      case Pull::DOWN:
        gpio_init.Pull = GPIO_PULLDOWN;
        break;
    }

    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;

    HAL_GPIO_Init(port_, &gpio_init);

    return ErrorCode::OK;
  }

  static STM32GPIO* map[STM32_GPIO_EXTI_NUMBER];  // NOLINT

 private:
  GPIO_TypeDef* port_;
  uint16_t pin_;
  IRQn_Type irq_;
};

}  // namespace LibXR

#endif
