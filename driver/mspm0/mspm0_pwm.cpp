#include "mspm0_pwm.hpp"

using namespace LibXR;

MSPM0PWM::MSPM0PWM(MSPM0PWM::Resources res)
    : timer_(res.timer), channel_(res.channel), clock_freq_(res.clock_freq)
{
  ASSERT(timer_ != nullptr);
  ASSERT(clock_freq_ > 0);
}

ErrorCode MSPM0PWM::SetDutyCycle(float value)
{
  if (value < 0.0f)
  {
    value = 0.0f;
  }
  if (value > 1.0f)
  {
    value = 1.0f;
  }

  uint32_t period = DL_Timer_getLoadValue(timer_);
  uint32_t compare_value = 0;

  if (value <= 0.0f)
  {
    compare_value = period;
  }
  else if (value >= 1.0f)
  {
    compare_value = period + 1;
  }
  else
  {
    compare_value = static_cast<uint32_t>(static_cast<float>(period) * (1.0f - value));
  }

  DL_Timer_setCaptureCompareValue(timer_, compare_value, channel_);

  return ErrorCode::OK;
}

ErrorCode MSPM0PWM::SetConfig(Configuration config)
{
  const uint32_t SOURCE_CLOCK = clock_freq_;
  if (config.frequency > SOURCE_CLOCK || config.frequency == 0)
  {
    return ErrorCode::ARG_ERR;
  }

  const uint32_t TOTAL_CYCLES_NEEDED = SOURCE_CLOCK / config.frequency;
  uint32_t min_total_prescale = (TOTAL_CYCLES_NEEDED + 65535) >> 16;
  if (min_total_prescale == 0)
  {
    min_total_prescale = 1;
  }

  uint32_t best_div = (min_total_prescale + 255) >> 8;
  if (best_div > 8)
  {
    return ErrorCode::NOT_SUPPORT;  // Frequency too low
  }
  if (best_div == 0)
  {
    best_div = 1;
  }

  uint32_t best_prescaler = (min_total_prescale + best_div - 1) / best_div;
  if (best_prescaler == 0)
  {
    best_prescaler = 1;
  }

  uint32_t best_load = (SOURCE_CLOCK / (best_div * best_prescaler * config.frequency));
  if (best_load > 0)
  {
    best_load -= 1;
  }

  DL_Timer_ClockConfig clock_config;
  DL_Timer_getClockConfig(timer_, &clock_config);

  clock_config.divideRatio = static_cast<DL_TIMER_CLOCK_DIVIDE>(best_div - 1);
  clock_config.prescale = static_cast<uint8_t>(best_prescaler - 1);

  DL_Timer_setClockConfig(timer_, &clock_config);
  DL_Timer_setLoadValue(timer_, best_load);

  return ErrorCode::OK;
}

ErrorCode MSPM0PWM::Enable()
{
  DL_Timer_startCounter(timer_);
  return ErrorCode::OK;
}

ErrorCode MSPM0PWM::Disable()
{
  DL_Timer_stopCounter(timer_);
  return ErrorCode::OK;
}