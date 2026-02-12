#include "esp_gpio.hpp"

namespace LibXR
{

void IRAM_ATTR ESP32GPIO::InterruptDispatcher(void* arg)
{
  auto gpio_num = static_cast<gpio_num_t>(reinterpret_cast<uintptr_t>(arg));
  auto gpio = map_[gpio_num];
  if (gpio)
  {
    gpio->callback_.Run(true);
  }
}

}  // namespace LibXR
