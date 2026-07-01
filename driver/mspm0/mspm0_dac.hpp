#pragma once

#include <ti/driverlib/dl_dac12.h>

#include "dac.hpp"
#include "ti_msp_dl_config.h"

#if defined(__MSPM0_HAS_DAC12__)

namespace LibXR
{

class MSPM0DAC : public DAC
{
 public:
  struct Resources
  {
    DAC12_Regs* instance;
  };

  explicit MSPM0DAC(Resources res, float init_voltage = 0.0f, float vref = 3.3f);

  ErrorCode Write(float voltage) override;

 private:
  DAC12_Regs* instance_;
  float vref_;
  uint16_t resolution_;
};

#define MSPM0_DAC_INIT(name) \
  ::LibXR::MSPM0DAC::Resources { name }

}  // namespace LibXR

#endif
