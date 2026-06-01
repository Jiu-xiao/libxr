#pragma once

#include <ti/driverlib/dl_dac12.h>
#include "dac.hpp"
#include "ti_msp_dl_config.h"

namespace LibXR
{

class MSPM0DAC : public DAC
{
 public:
  struct Resources
  {
    DAC12_Regs* instance;
    float vref;
  };

  explicit MSPM0DAC(Resources res, float init_voltage = 0.0f);

  ErrorCode Write(float voltage) override;

 private:
  DAC12_Regs* instance_;
  float vref_;
  uint16_t resolution_;
};

#define MSPM0_DAC_RES(instance, voltage_ref)                                \
  ::LibXR::MSPM0DAC::Resources                                              \
  {                                                                         \
    instance, static_cast<float>(voltage_ref)                               \
  }

}  // namespace LibXR
