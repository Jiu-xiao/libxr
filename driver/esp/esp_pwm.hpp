#pragma once

#include "driver/ledc.h"
#include "esp_err.h"
#include "pwm.hpp"

namespace LibXR
{

/**
 * @brief ESP32 PWM 驱动实现 / ESP32 PWM driver implementation
 */
class ESP32PWM : public PWM
{
 public:
  /**
   * @brief 构造 PWM 通道对象 / Construct PWM channel object
   *
   * @param gpio_num GPIO 编号 / GPIO number
   * @param channel LEDC 通道 / LEDC channel
   * @param timer LEDC 定时器 / LEDC timer
   * @param resolution 占空比分辨率 / Duty resolution
   */
  ESP32PWM(int gpio_num, ledc_channel_t channel, ledc_timer_t timer = LEDC_TIMER_0,
           ledc_timer_bit_t resolution = static_cast<ledc_timer_bit_t>(
               (static_cast<uint8_t>(LEDC_TIMER_BIT_MAX) - 1)))
      : gpio_num_(gpio_num),
        channel_(channel),
        timer_(timer),
        resolution_(resolution),
        max_duty_((1 << resolution) - 1)
  {
    ledc_channel_config_t channel_conf = {};
    channel_conf.gpio_num = gpio_num_;
    channel_conf.speed_mode = static_cast<ledc_mode_t>(0);
    channel_conf.channel = channel_;
    channel_conf.intr_type = LEDC_INTR_DISABLE;
    channel_conf.timer_sel = timer_;
    channel_conf.duty = 0;
    channel_conf.hpoint = 0;

    auto err = ledc_channel_config(&channel_conf);
    if (err != ESP_OK)
    {
      ASSERT(false);
    };
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

    uint32_t duty = static_cast<uint32_t>(max_duty_ * value);
    esp_err_t err = ledc_set_duty(static_cast<ledc_mode_t>(0), channel_, duty);
    if (err != ESP_OK) return ErrorCode::FAILED;

    err = ledc_update_duty(static_cast<ledc_mode_t>(0), channel_);
    return (err == ESP_OK) ? ErrorCode::OK : ErrorCode::FAILED;
  }

  ErrorCode SetConfig(Configuration config) override
  {
    if (config.frequency == 0)
    {
      return ErrorCode::ARG_ERR;
    }

    ledc_timer_config_t timer_conf = {};
    timer_conf.speed_mode = static_cast<ledc_mode_t>(0);
    timer_conf.duty_resolution = resolution_;
    timer_conf.timer_num = timer_;
    timer_conf.freq_hz = config.frequency;
    timer_conf.clk_cfg = LEDC_AUTO_CLK;

    esp_err_t err = ledc_timer_config(&timer_conf);
    if (err != ESP_OK) return ErrorCode::INIT_ERR;

    max_duty_ = (1 << resolution_) - 1;
    return ErrorCode::OK;
  }

  ErrorCode Enable() override
  {
    esp_err_t err = ledc_update_duty(static_cast<ledc_mode_t>(0), channel_);
    return (err == ESP_OK) ? ErrorCode::OK : ErrorCode::FAILED;
  }

  ErrorCode Disable() override
  {
    esp_err_t err = ledc_stop(static_cast<ledc_mode_t>(0), channel_, 0);
    return (err == ESP_OK) ? ErrorCode::OK : ErrorCode::FAILED;
  }

 private:
  int gpio_num_;
  ledc_channel_t channel_;
  ledc_timer_t timer_;
  ledc_timer_bit_t resolution_;
  uint32_t max_duty_;
};

}  // namespace LibXR
