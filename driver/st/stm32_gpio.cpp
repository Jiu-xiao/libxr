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

bool STM32GPIO::Read() { return HAL_GPIO_ReadPin(port_, pin_) == GPIO_PIN_SET; }

ErrorCode STM32GPIO::Write(bool value)
{
  HAL_GPIO_WritePin(port_, pin_, value ? GPIO_PIN_SET : GPIO_PIN_RESET);
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

extern "C" void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  const uint8_t LINE = STM32_GPIO_PinToLine(GPIO_Pin);
  if (auto* gpio = STM32GPIO::map[LINE])
  {
    gpio->callback_.Run(true);
  }
}

#endif
