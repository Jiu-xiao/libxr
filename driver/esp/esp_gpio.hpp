#pragma once

#include "driver/gpio.h"
#include "esp_log.h"
#include "gpio.hpp"

namespace LibXR
{
class ESP32GPIO : public GPIO
{
 public:
  explicit ESP32GPIO(gpio_num_t gpio_num) : gpio_num_(gpio_num)
  {
    map_[gpio_num_] = this;
  }

  bool Read() override { return gpio_get_level(gpio_num_) != 0; }

  void Write(bool value) override { gpio_set_level(gpio_num_, value ? 1 : 0); }

  ErrorCode EnableInterrupt() override
  {
    if (!isr_service_installed_)
    {
      gpio_install_isr_service(0);
      isr_service_installed_ = true;
    }

    gpio_isr_handler_add(gpio_num_, ESP32GPIO::InterruptDispatcher, (void*)gpio_num_);
    gpio_intr_enable(gpio_num_);
    return ErrorCode::OK;
  }

  ErrorCode DisableInterrupt() override
  {
    gpio_intr_disable(gpio_num_);
    gpio_isr_handler_remove(gpio_num_);
    return ErrorCode::OK;
  }

  ErrorCode SetConfig(Configuration config) override
  {
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = 1ULL << gpio_num_;

    switch (config.direction)
    {
      case Direction::INPUT:
        io_conf.mode = GPIO_MODE_INPUT;
        break;
      case Direction::OUTPUT_PUSH_PULL:
        io_conf.mode = GPIO_MODE_OUTPUT;
        break;
      case Direction::OUTPUT_OPEN_DRAIN:
        io_conf.mode = GPIO_MODE_OUTPUT_OD;
        break;
      case Direction::FALL_INTERRUPT:
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.intr_type = GPIO_INTR_NEGEDGE;
        break;
      case Direction::RISING_INTERRUPT:
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.intr_type = GPIO_INTR_POSEDGE;
        break;
      case Direction::FALL_RISING_INTERRUPT:
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.intr_type = GPIO_INTR_ANYEDGE;
        break;
    }

    switch (config.pull)
    {
      case Pull::NONE:
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        break;
      case Pull::UP:
        io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        break;
      case Pull::DOWN:
        io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        break;
    }

    gpio_config(&io_conf);

    return ErrorCode::OK;
  }

  static void IRAM_ATTR InterruptDispatcher(void* arg)
  {
    auto gpio_num = static_cast<gpio_num_t>(reinterpret_cast<uintptr_t>(arg));
    auto gpio = map_[gpio_num];
    if (gpio)
    {
      gpio->callback_.Run(true);
    }
  }

 private:
  gpio_num_t gpio_num_;
  static inline bool isr_service_installed_ = false;
  static inline ESP32GPIO* map_[GPIO_NUM_MAX];
};

}  // namespace LibXR
