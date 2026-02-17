#include "hpm_pwm.hpp"

using namespace LibXR;

HPMPWM::HPMPWM(LibXRHpmPwmType* pwm, clock_name_t clock, uint8_t pwm_index, uint8_t cmp_index,
               bool invert)
    : pwm_(pwm),
      clock_(clock),
      pwm_index_(pwm_index),
      cmp_index_(cmp_index),
      invert_(invert),
      reload_(0),
      configured_(false)
{
}

ErrorCode HPMPWM::SetDutyCycle(float value)
{
#if !LIBXR_HPM_PWM_SUPPORTED
  (void) value;
  return ErrorCode::NOT_SUPPORT;
#else
  if (!configured_)
  {
    return ErrorCode::INIT_ERR;
  }

  if (value < 0.0f)
  {
    value = 0.0f;
  }
  else if (value > 1.0f)
  {
    value = 1.0f;
  }

  const float duty_percent = value * 100.0f;
  if (pwm_update_duty_edge_aligned(pwm_, cmp_index_, duty_percent) != status_success)
  {
    return ErrorCode::FAILED;
  }

  return ErrorCode::OK;
#endif
}

ErrorCode HPMPWM::SetConfig(Configuration config)
{
#if !LIBXR_HPM_PWM_SUPPORTED
  (void) config;
  return ErrorCode::NOT_SUPPORT;
#else
  if (config.frequency == 0u)
  {
    return ErrorCode::ARG_ERR;
  }

  const uint32_t clock_hz = clock_get_frequency(clock_);
  if (clock_hz == 0u)
  {
    return ErrorCode::INIT_ERR;
  }

  uint32_t reload = clock_hz / config.frequency;
  if (reload == 0u || reload > 0xFFFFFFu)
  {
    return ErrorCode::INIT_ERR;
  }
  reload_ = reload;

  pwm_stop_counter(pwm_);

  pwm_config_t pwm_config{};
  pwm_cmp_config_t cmp_config{};

  pwm_get_default_pwm_config(pwm_, &pwm_config);
  pwm_get_default_cmp_config(pwm_, &cmp_config);

  pwm_config.enable_output = true;
  pwm_config.invert_output = invert_;
  pwm_config.dead_zone_in_half_cycle = 0;

  pwm_set_reload(pwm_, 0, reload_);
  pwm_set_start_count(pwm_, 0, 0);

  cmp_config.mode = pwm_cmp_mode_output_compare;
  cmp_config.cmp = reload_ + 1;
  cmp_config.update_trigger = pwm_shadow_register_update_on_modify;

  if (pwm_setup_waveform(pwm_, pwm_index_, &pwm_config, cmp_index_, &cmp_config, 1) !=
      status_success)
  {
    return ErrorCode::INIT_ERR;
  }

  pwm_issue_shadow_register_lock_event(pwm_);
  configured_ = true;
  return ErrorCode::OK;
#endif
}

ErrorCode HPMPWM::Enable()
{
#if !LIBXR_HPM_PWM_SUPPORTED
  return ErrorCode::NOT_SUPPORT;
#else
  if (!configured_)
  {
    return ErrorCode::INIT_ERR;
  }

  pwm_start_counter(pwm_);
  return ErrorCode::OK;
#endif
}

ErrorCode HPMPWM::Disable()
{
#if !LIBXR_HPM_PWM_SUPPORTED
  return ErrorCode::NOT_SUPPORT;
#else
  pwm_stop_counter(pwm_);
  return ErrorCode::OK;
#endif
}
