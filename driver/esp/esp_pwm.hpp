#pragma once

#include "esp_def.hpp"

#include "driver/ledc.h"
#include "esp_err.h"
#include "hal/ledc_hal.h"
#include "pwm.hpp"

namespace LibXR
{

/** @brief ESP32 PWM driver implementation. */
class ESP32PWM : public PWM
{
 public:
  /** @brief Construct a PWM channel object. */
  ESP32PWM(int gpio_num, ledc_channel_t channel, ledc_timer_t timer = LEDC_TIMER_0,
           ledc_timer_bit_t resolution = static_cast<ledc_timer_bit_t>(
               static_cast<uint8_t>(LEDC_TIMER_BIT_MAX) - 1))
      : gpio_num_(gpio_num),
        channel_(channel),
        timer_(timer),
        speed_mode_(LEDC_LOW_SPEED_MODE),
        resolution_(resolution),
        max_duty_((1U << static_cast<uint8_t>(resolution_)) - 1U)
  {
    ledc_hal_init(&hal_, speed_mode_);

    ledc_channel_config_t channel_conf = {};
    channel_conf.gpio_num = gpio_num_;
    channel_conf.speed_mode = speed_mode_;
    channel_conf.channel = channel_;
    channel_conf.intr_type = LEDC_INTR_DISABLE;
    channel_conf.timer_sel = timer_;
    channel_conf.duty = 0;
    channel_conf.hpoint = 0;

    const esp_err_t err = ledc_channel_config(&channel_conf);
    if (err != ESP_OK)
    {
      ASSERT(false);
    }
  }

  ErrorCode SetDutyCycle(float value) override
  {
    if (value < 0.0f)
    {
      value = 0.0f;
    }
    else if (value > 1.0f)
    {
      value = 1.0f;
    }

    const uint32_t duty = static_cast<uint32_t>(max_duty_ * value);
    ledc_hal_set_duty_int_part(&hal_, channel_, duty);
    ledc_hal_set_duty_start(&hal_, channel_);
    ledc_hal_ls_channel_update(&hal_, channel_);
    return ErrorCode::OK;
  }

  ErrorCode SetConfig(Configuration config) override
  {
    if (config.frequency == 0)
    {
      return ErrorCode::ARG_ERR;
    }

    for (int bits = static_cast<int>(resolution_);
         bits >= static_cast<int>(LEDC_TIMER_1_BIT); --bits)
    {
      ledc_timer_config_t timer_conf = {};
      timer_conf.speed_mode = speed_mode_;
      timer_conf.duty_resolution = static_cast<ledc_timer_bit_t>(bits);
      timer_conf.timer_num = timer_;
      timer_conf.freq_hz = config.frequency;
      timer_conf.clk_cfg = LEDC_AUTO_CLK;

      if (ledc_timer_config(&timer_conf) != ESP_OK)
      {
        continue;
      }

      resolution_ = static_cast<ledc_timer_bit_t>(bits);
      ledc_hal_bind_channel_timer(&hal_, channel_, timer_);
      ledc_hal_ls_timer_update(&hal_, timer_);
      max_duty_ = (1U << static_cast<uint8_t>(resolution_)) - 1U;
      return ErrorCode::OK;
    }

    return ErrorCode::INIT_ERR;
  }

  ErrorCode Enable() override
  {
    ledc_hal_set_sig_out_en(&hal_, channel_, true);
    ledc_hal_ls_channel_update(&hal_, channel_);
    return ErrorCode::OK;
  }

  ErrorCode Disable() override
  {
    ledc_hal_set_idle_level(&hal_, channel_, 0);
    ledc_hal_set_sig_out_en(&hal_, channel_, false);
    ledc_hal_ls_channel_update(&hal_, channel_);
    return ErrorCode::OK;
  }

 private:
  int gpio_num_;
  ledc_channel_t channel_;
  ledc_timer_t timer_;
  ledc_mode_t speed_mode_;
  ledc_hal_context_t hal_{};
  ledc_timer_bit_t resolution_;
  uint32_t max_duty_;
};

}  // namespace LibXR

