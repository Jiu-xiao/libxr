#pragma once

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

// SysConfig splits the timer instance and GPIO channel into two macro groups.
#define MSPM0_PWM_INIT(timer_name, gpio_name)             \
  ::LibXR::MSPM0PWM::Resources                            \
  {                                                       \
    timer_name##_INST, gpio_name##_IDX,                   \
        static_cast<uint32_t>(timer_name##_INST_CLK_FREQ) \
  }

}  // namespace LibXR
