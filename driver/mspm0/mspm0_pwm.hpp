#pragma once

#include "mspm0_conf.hpp"
#include "pwm.hpp"
#include "ti_msp_dl_config.h"

namespace LibXR
{

class MSPM0PWM : public PWM
{
 public:
  struct Resources
  {
    GPTIMER_Regs* timer;
    DL_TIMER_CC_INDEX channel;
    uint32_t clock_freq;
  };

  MSPM0PWM(Resources res);

  ErrorCode SetDutyCycle(float value);

  ErrorCode SetConfig(Configuration config);

  ErrorCode Enable();

  ErrorCode Disable();

 private:
  GPTIMER_Regs* timer_;
  DL_TIMER_CC_INDEX channel_;
  uint32_t clock_freq_;
};

// Helper macro to define PWM instance resources from SysConfig macros
#define MSPM0_PWM_CH(name, ch)                                          \
  LibXR::MSPM0PWM::Resources                                            \
  {                                                                     \
    name##_INST, DL_TIMER_CC_##ch##_INDEX,                              \
        name##_INST_CLK_FREQ* LibXR::MSPM0Config::name##_INST_CLK_DIV*( \
            LibXR::MSPM0Config::name##_INST_CLK_PSC + 1)                \
  }

}  // namespace LibXR