#pragma once

#include "dl_gpio.h"
#include "gpio.hpp"

namespace LibXR
{

// Constants for MSPM0 GPIO ports / MSPM0 GPIO 端口常量
// Note: Values referenced from pinmap for each device family
#if defined(__MSPM0C1105__) || defined(DeviceFamily_MSPM0C1105_C1106)
constexpr uint8_t MAX_PORTS = 2;  // GPIOA, GPIOB
#elif defined(__MSPM0C110X__) || defined(DeviceFamily_MSPM0C110X)
constexpr uint8_t MAX_PORTS = 1;  // GPIOA only
#elif defined(__MSPM0G110X__) || defined(DeviceFamily_MSPM0G110X)
constexpr uint8_t MAX_PORTS = 2;  // GPIOA, GPIOB
#elif defined(__MSPM0G120X__) || defined(DeviceFamily_MSPM0G120X)
constexpr uint8_t MAX_PORTS = 2;  // GPIOA, GPIOB
#elif defined(__MSPM0G121X__) || defined(DeviceFamily_MSPM0G121X)
constexpr uint8_t MAX_PORTS = 2;  // GPIOA, GPIOB
#elif defined(__MSPM0G150X__) || defined(DeviceFamily_MSPM0G150X)
constexpr uint8_t MAX_PORTS = 2;  // GPIOA, GPIOB
#elif defined(__MSPM0G151X__) || defined(DeviceFamily_MSPM0G151X)
constexpr uint8_t MAX_PORTS = 3;  // GPIOA, GPIOB, GPIOC
#elif defined(__MSPM0G310X__) || defined(DeviceFamily_MSPM0G310X)
constexpr uint8_t MAX_PORTS = 2;  // GPIOA, GPIOB
#elif defined(__MSPM0G320X__) || defined(DeviceFamily_MSPM0G320X)
constexpr uint8_t MAX_PORTS = 2;  // GPIOA, GPIOB
#elif defined(__MSPM0G321X__) || defined(DeviceFamily_MSPM0G321X)
constexpr uint8_t MAX_PORTS = 2;  // GPIOA, GPIOB
#elif defined(__MSPM0G3507__) || defined(DeviceFamily_MSPM0G350X)
constexpr uint8_t MAX_PORTS = 2;  // GPIOA, GPIOB
#elif defined(DeviceFamily_MSPM0G351X)
constexpr uint8_t MAX_PORTS = 3;  // GPIOA, GPIOB, GPIOC
#elif defined(__MSPM0G511X__) || defined(DeviceFamily_MSPM0G511X)
constexpr uint8_t MAX_PORTS = 2;  // GPIOA, GPIOB
#elif defined(__MSPM0G518X__) || defined(DeviceFamily_MSPM0G518X)
constexpr uint8_t MAX_PORTS = 2;  // GPIOA, GPIOB
#elif defined(__MSPM0H321X__) || defined(DeviceFamily_MSPM0H321X)
constexpr uint8_t MAX_PORTS = 2;  // GPIOA, GPIOB
#elif defined(__MSPM0L110X__) || defined(DeviceFamily_MSPM0L110X)
constexpr uint8_t MAX_PORTS = 1;  // GPIOA only
#elif defined(__MSPM0L111X__) || defined(DeviceFamily_MSPM0L111X)
constexpr uint8_t MAX_PORTS = 2;  // GPIOA, GPIOB
#elif defined(__MSPM0L112X__) || defined(DeviceFamily_MSPM0L112X)
constexpr uint8_t MAX_PORTS = 2;  // GPIOA, GPIOB
#elif defined(__MSPM0L122X__) || defined(DeviceFamily_MSPM0L122X)
constexpr uint8_t MAX_PORTS = 3;  // GPIOA, GPIOB, GPIOC
#elif defined(__MSPM0L1306__) || defined(DeviceFamily_MSPM0L130X)
constexpr uint8_t MAX_PORTS = 1;  // GPIOA only
#elif defined(__MSPM0L1306__) || defined(DeviceFamily_MSPM0L134X)
constexpr uint8_t MAX_PORTS = 1;  // GPIOA only
#elif defined(__MSPM0L210X__) || defined(DeviceFamily_MSPM0L210X)
constexpr uint8_t MAX_PORTS = 2;  // GPIOA, GPIOB
#elif defined(__MSPM0L211X__) || defined(DeviceFamily_MSPM0L211X)
constexpr uint8_t MAX_PORTS = 2;  // GPIOA, GPIOB
#elif defined(__MSPM0L222X__) || defined(DeviceFamily_MSPM0L222X)
constexpr uint8_t MAX_PORTS = 3;  // GPIOA, GPIOB, GPIOC
#else
#error "LibXR: Unsupported MSPM0 Device Family or Missing Define for MAX_PORTS."
#endif

class MSPM0GPIO : public GPIO
{
 public:
  /**
   * @brief Constructor / 构造函数
   * @param port GPIO port pointer / GPIO 端口指针
   * @param pin_mask Pin mask (e.g., DL_GPIO_PIN_0) / 引脚掩码 (例如 DL_GPIO_PIN_0)
   * @param pincm PINCM index for IOMUX configuration / IOMUX 配置的 PINCM 索引
   */
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