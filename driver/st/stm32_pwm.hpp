#pragma once

#include "main.h"

#ifdef HAL_TIM_MODULE_ENABLED

#include "pwm.hpp"

namespace LibXR
{

class STM32PWM : public PWM
{
 public:
  STM32PWM(TIM_HandleTypeDef* htim, uint32_t channel, bool complementary = false);

  ErrorCode SetDutyCycle(float value) override;

  ErrorCode SetConfig(Configuration config) override;

  ErrorCode Enable() override;

  ErrorCode Disable() override;

 private:
  TIM_HandleTypeDef* htim_;
  uint32_t channel_;
  bool complementary_;
};

}  // namespace LibXR

#endif
