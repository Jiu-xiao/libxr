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

bool STM32GPIO::Read() { return (port_->IDR & pin_) != 0u; }

void STM32GPIO::Write(bool value)
{
  if (value)
  {
    port_->BSRR = static_cast<uint32_t>(pin_);
  }
  else
  {
    port_->BSRR = static_cast<uint32_t>(pin_) << 16;
  }
  return ErrorCode::OK;
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

ErrorCode STM32GPIO::SetConfig(Configuration config)
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
