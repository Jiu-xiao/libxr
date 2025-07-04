#include "main.h"

#if defined(HAL_DAC_MODULE_ENABLED)

#ifdef DAC
#undef DAC
#endif

#include "dac.hpp"

namespace LibXR
{

/**
 * @class STM32DAC
 * @brief STM32 平台上的 DAC 实现
 * @brief DAC implementation for STM32 platform
 */
class STM32DAC : public DAC
{
 public:
  /**
   * @brief 构造函数
   * @brief Constructor
   */
  STM32DAC(DAC_HandleTypeDef* hdac, uint32_t channel, float init_voltage = 0.0f,
           float vref = 3.3f);

  /**
   * @brief 输出电压
   * @brief Outputs the voltage on STM32 DAC
   * @param voltage 模拟电压值
   * @param voltage The analog voltage value
   *
   * @return 错误码 ErrorCode
   */
  ErrorCode Write(float voltage) override;

 private:
  DAC_HandleTypeDef* hdac_;  ///< DAC 外设句柄 DAC device handle
  uint32_t channel_;         ///< DAC 通道 DAC channel
  float vref_;               ///< DAC 参考电压 DAC reference voltage
  uint32_t align_;           ///< DAC 对齐方式 DAC alignment
  uint16_t resolution_;      ///< DAC 分辨率 DAC resolution
};

}  // namespace LibXR

#endif
