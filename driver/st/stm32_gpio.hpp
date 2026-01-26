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
class STM32GPIO final : public GPIO
{
 public:
  STM32GPIO(GPIO_TypeDef* port, uint16_t pin, IRQn_Type irq = NonMaskableInt_IRQn);

  inline bool Read() { return (port_->IDR & pin_) != 0u; }

  inline void Write(bool value)
  {
    if (value)
    {
      port_->BSRR = static_cast<uint32_t>(pin_);
    }
    else
    {
      port_->BSRR = static_cast<uint32_t>(pin_) << 16;
    }
  }

  ErrorCode EnableInterrupt();

  ErrorCode DisableInterrupt();

  inline ErrorCode SetConfig(Configuration config)
  {
// LL快速路径，适用于非外部中断的输入/输出配置
#ifdef USE_FULL_LL_DRIVER
    if (static_cast<uint8_t>(config.direction) <=
        static_cast<uint8_t>(LibXR::GPIO::Direction::OUTPUT_OPEN_DRAIN))
    {
      LL_GPIO_InitTypeDef ll = {};
      ll.Pin = pin_;
      ll.Speed = LL_GPIO_SPEED_FREQ_HIGH;

      // Pull
      switch (config.pull)
      {
        case Pull::NONE:
          ll.Pull = LL_GPIO_PULL_NO;
          break;
        case Pull::UP:
          ll.Pull = LL_GPIO_PULL_UP;
          break;
        case Pull::DOWN:
          ll.Pull = LL_GPIO_PULL_DOWN;
          break;
        default:
          ll.Pull = LL_GPIO_PULL_NO;
          break;
      }

      // Mode + OutputType
      switch (config.direction)
      {
        case Direction::INPUT:
          ll.Mode = LL_GPIO_MODE_INPUT;
          break;

        case Direction::OUTPUT_PUSH_PULL:
          ll.Mode = LL_GPIO_MODE_OUTPUT;
          ll.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
          break;

        case Direction::OUTPUT_OPEN_DRAIN:
          ll.Mode = LL_GPIO_MODE_OUTPUT;
          ll.OutputType = LL_GPIO_OUTPUT_OPENDRAIN;
          break;

        default:
          return ErrorCode::ARG_ERR;
      }

      LL_GPIO_Init(port_, &ll);

      return ErrorCode::OK;
    }
#endif

    GPIO_InitTypeDef gpio_init = {};

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
