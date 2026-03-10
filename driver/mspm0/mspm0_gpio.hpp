#pragma once

#include "dl_gpio.h"
#include "gpio.hpp"

namespace LibXR
{

#ifdef GPIOC_BASE
    constexpr uint8_t MAX_PORTS = 3;
#elif defined(GPIOB_BASE)
    constexpr uint8_t MAX_PORTS = 2;
#else
    constexpr uint8_t MAX_PORTS = 1;
#endif

class MSPM0GPIO : public GPIO
{
 public:
  explicit MSPM0GPIO(GPIO_Regs* port, uint32_t pin_mask, uint32_t pincm);

  bool Read() override;

  ErrorCode Write(bool value) override;

  ErrorCode EnableInterrupt() override;

  ErrorCode DisableInterrupt() override;

  ErrorCode SetConfig(Configuration config) override;

  static inline void OnInterrupt(GPIO_Regs* port)
  {
    const int idx = GetPortIndex(reinterpret_cast<uint32_t>(port));  // NOLINT
    OnInterruptDispatch(port, idx);
  }

 private:
  static void OnInterruptDispatch(GPIO_Regs* port, int port_idx);

  static constexpr int GetPortIndex(uint32_t base_addr)
  {
    if (base_addr == GPIOA_BASE)
    {
      return 0;
    }
#ifdef GPIOB_BASE
    if (base_addr == GPIOB_BASE)
    {
      return 1;
    }
#endif
#ifdef GPIOC_BASE
    if (base_addr == GPIOC_BASE)
    {
      return 2;
    }
#endif
    return -1;
  }

  GPIO_Regs* port_;
  uint32_t pin_mask_;
  uint32_t pincm_;
  LibXR::GPIO::Direction current_direction_ = LibXR::GPIO::Direction::INPUT;

  // 安全的编译时nullptr数组初始化 / Safe compile-time initialization of nullptr array
  // NOLINTNEXTLINE(bugprone-dynamic-static-initializers)
  static MSPM0GPIO* instance_map_[MAX_PORTS][32];
};

}  // namespace LibXR