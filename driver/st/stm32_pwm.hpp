#pragma once

#include "main.h"

#ifdef HAL_TIM_MODULE_ENABLED

#include "pwm.hpp"

namespace LibXR {

class STM32PWM : public PWM {
 public:
  STM32PWM(TIM_HandleTypeDef* htim, uint32_t channel)
      : htim_(htim), channel_(channel) {}

  ErrorCode SetDutyCycle(float value) override {
    if (value < 0.0f || value > 100.0f) {
      return ErrorCode::ARG_ERR;
    }

    uint32_t pulse = static_cast<uint32_t>(
        static_cast<float>(htim_->Init.Period + 1) * value);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-volatile"
    __HAL_TIM_SET_COMPARE(htim_, channel_, pulse);
#pragma GCC diagnostic pop

    return ErrorCode::OK;
  }

  ErrorCode SetConfig(Configuration config) override {
    uint32_t& value = config.frequency;

    if (value == 0) {
      return ErrorCode::ARG_ERR;
    }

    uint32_t clock_freq;                                       // NOLINT
    if (htim_->Instance == TIM1 || htim_->Instance == TIM8) {  // NOLINT
      clock_freq = HAL_RCC_GetPCLK2Freq();
    } else {
      clock_freq = HAL_RCC_GetPCLK1Freq();
    }

    uint32_t prescaler = (clock_freq / (value * 65536)) + 1;
    uint32_t period = (clock_freq / (prescaler * value)) - 1;

    htim_->Init.Prescaler = prescaler - 1;
    htim_->Init.Period = period;

    if (HAL_TIM_PWM_Init(htim_) != HAL_OK) {
      return ErrorCode::INIT_ERR;
    }

    return ErrorCode::OK;
  }

  ErrorCode Enable() override {
    if (HAL_TIM_PWM_Start(htim_, channel_) != HAL_OK) {
      return ErrorCode::FAILED;
    }
    return ErrorCode::OK;
  }

  ErrorCode Disable() override {
    if (HAL_TIM_PWM_Stop(htim_, channel_) != HAL_OK) {
      return ErrorCode::FAILED;
    }
    return ErrorCode::OK;
  }

 private:
  TIM_HandleTypeDef* htim_;
  uint32_t channel_;
};

}  // namespace LibXR

#endif
