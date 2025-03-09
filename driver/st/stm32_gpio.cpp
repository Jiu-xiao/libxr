#include "stm32_gpio.hpp"

#ifdef HAL_GPIO_MODULE_ENABLED

using namespace LibXR;

STM32GPIO *STM32GPIO::map[STM32_GPIO_EXTI_NUMBER] = {nullptr};

stm32_gpio_exti_t STM32_GPIO_EXTI_GetID(uint16_t pin) {  // NOLINT
  pin = __builtin_ctz(pin);
  if (pin < 5) {
    return static_cast<stm32_gpio_exti_t>(pin);
  } else if (pin < 10) {
    return STM32_GPIO_EXTI_5_9;
  } else if (pin < 16) {
    return STM32_GPIO_EXTI_10_15;
  } else {
    ASSERT(false);
  }

  return STM32_GPIO_EXTI_NUMBER;
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
  auto id = STM32_GPIO_EXTI_GetID(GPIO_Pin);
  auto gpio = STM32GPIO::map[id];

  gpio->callback_.Run(true);
}

#endif
