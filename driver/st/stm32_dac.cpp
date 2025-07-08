#include "stm32_dac.hpp"

#if defined(HAL_DAC_MODULE_ENABLED)

using namespace LibXR;

STM32DAC::STM32DAC(DAC_HandleTypeDef* hadc, uint32_t channel, float init_voltage,
                   float vref)
    : hdac_(hadc), channel_(channel), vref_(vref)
{
// NOLINTBEGIN
#if defined(DAC_ALIGN_12B_R)
  align_ = DAC_ALIGN_12B_R;
  resolution_ = 4095;
#elif defined(DAC_ALIGN_8B_R)
  align_ = DAC_ALIGN_8B_R;
  resolution_ = 255;
#else
#error "No supported DAC_ALIGN_xxx defined"
#endif
  // NOLINTEND

  Write(init_voltage);
  HAL_DAC_Start(hdac_, channel_);
}

ErrorCode STM32DAC::Write(float voltage)
{
  voltage = std::clamp(voltage, 0.0f, vref_);
  return HAL_DAC_SetValue(
             hdac_, channel_, align_,
             static_cast<uint16_t>(voltage / vref_ * static_cast<float>(resolution_))) ==
                 HAL_OK
             ? ErrorCode::OK
             : ErrorCode::FAILED;
}

#endif
