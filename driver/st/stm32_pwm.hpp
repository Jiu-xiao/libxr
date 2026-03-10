#pragma once

#include "main.h"

#ifdef HAL_TIM_MODULE_ENABLED

#include "pwm.hpp"

namespace LibXR
{

/**
 * @brief STM32 PWM 驱动实现 / STM32 PWM driver implementation
 */
class STM32PWM : public PWM
{
 public:
  /**
   * @brief 构造 PWM 对象 / Construct PWM object
   */
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
