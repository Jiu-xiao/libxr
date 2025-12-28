#pragma once

#include "gpio.hpp"
#include "main.h"

#ifdef HAL_GPIO_MODULE_ENABLED

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

  static STM32GPIO* map[16];  // NOLINT

 private:
  GPIO_TypeDef* port_;
  uint16_t pin_;
  IRQn_Type irq_;
};

}  // namespace LibXR

#endif
