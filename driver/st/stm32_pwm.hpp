#pragma once

#include "main.h"

#ifdef HAL_TIM_MODULE_ENABLED

#include "pwm.hpp"

namespace LibXR
{

class STM32PWM : public PWM
{
 public:
  STM32PWM(TIM_HandleTypeDef* htim, uint32_t channel) : htim_(htim), channel_(channel) {}

  ErrorCode SetDutyCycle(float value) override
  {
    if (value < 0.0f || value > 100.0f)
    {
      return ErrorCode::ARG_ERR;
    }

    uint32_t pulse =
        static_cast<uint32_t>(static_cast<float>(htim_->Init.Period + 1) * value);

    __HAL_TIM_SET_COMPARE(htim_, channel_, pulse);

    return ErrorCode::OK;
  }

  ErrorCode SetConfig(Configuration config) override
  {
    uint32_t clock_freq = 0;  // NOLINT

#if defined(STM32F0) || defined(STM32G0)
    // STM32F0 and STM32 G0 do not have PCLK1/PCLK2，use HCLK as reference clock
    clock_freq = HAL_RCC_GetHCLKFreq();
#else
    uint32_t& target_freq = config.frequency;
    if (target_freq == 0)
    {
      return ErrorCode::ARG_ERR;
    }
    else if (
#if defined(TIM1)
        htim_->Instance == TIM1 ||
#endif
#if defined(TIM8)
        htim_->Instance == TIM8 ||
#endif
#if defined(TIM9)
        htim_->Instance == TIM9 ||
#endif
#if defined(TIM10)
        htim_->Instance == TIM10 ||
#endif
#if defined(TIM11)
        htim_->Instance == TIM11 ||
#endif
#if defined(TIM15)
        htim_->Instance == TIM15 ||
#endif
#if defined(TIM16)
        htim_->Instance == TIM16 ||
#endif
#if defined(TIM17)
        htim_->Instance == TIM17 ||
#endif
#if defined(TIM20)
        htim_->Instance == TIM20 ||
#endif
        false)
    {
      clock_freq = HAL_RCC_GetPCLK2Freq();
    }
    else if (
#if defined(TIM2)
        htim_->Instance == TIM2 ||
#endif
#if defined(TIM3)
        htim_->Instance == TIM3 ||
#endif
#if defined(TIM4)
        htim_->Instance == TIM4 ||
#endif
#if defined(TIM5)
        htim_->Instance == TIM5 ||
#endif
#if defined(TIM6)
        htim_->Instance == TIM6 ||
#endif
#if defined(TIM7)
        htim_->Instance == TIM7 ||
#endif
#if defined(TIM12)
        htim_->Instance == TIM12 ||
#endif
#if defined(TIM13)
        htim_->Instance == TIM13 ||
#endif
#if defined(TIM14)
        htim_->Instance == TIM14 ||
#endif
        false)
    {
      clock_freq = HAL_RCC_GetPCLK1Freq();
    }
    else
    {
      return ErrorCode::NOT_SUPPORT;
    }
#endif

    if (clock_freq == 0)
    {
      return ErrorCode::INIT_ERR;
    }

    uint32_t prescaler = (clock_freq / (config.frequency * 65536)) + 1;
    uint32_t period = (clock_freq / (prescaler * config.frequency)) - 1;

    htim_->Init.Prescaler = prescaler - 1;
    htim_->Init.Period = period;

    if (HAL_TIM_PWM_Init(htim_) != HAL_OK)
    {
      return ErrorCode::INIT_ERR;
    }

    return ErrorCode::OK;
  }

  ErrorCode Enable() override
  {
    if (HAL_TIM_PWM_Start(htim_, channel_) != HAL_OK)
    {
      return ErrorCode::FAILED;
    }
    return ErrorCode::OK;
  }

  ErrorCode Disable() override
  {
    if (HAL_TIM_PWM_Stop(htim_, channel_) != HAL_OK)
    {
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
