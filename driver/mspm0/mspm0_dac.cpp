#include "mspm0_dac.hpp"

#if defined(__MSPM0_HAS_DAC12__)

#include <algorithm>

using namespace LibXR;

MSPM0DAC::MSPM0DAC(Resources res, float init_voltage, float vref)
    : instance_(res.instance), vref_(vref), resolution_(4095)
{
  ASSERT(instance_ != nullptr);
  ASSERT(vref_ > 0.0f);

  const uint32_t resolution = instance_->CTL0 & DAC12_CTL0_RES_MASK;
  if (resolution == DAC12_CTL0_RES__8BITS)
  {
    resolution_ = 255;
  }

  const ErrorCode write_ans = Write(init_voltage);
  ASSERT(write_ans == ErrorCode::OK);

  DL_DAC12_enableOutputPin(instance_);
  DL_DAC12_enable(instance_);
  ASSERT(DL_DAC12_isEnabled(instance_));
}

ErrorCode MSPM0DAC::Write(float voltage)
{
  const float clamped = std::clamp(voltage, 0.0f, vref_);
  const uint32_t code =
      static_cast<uint32_t>(clamped / vref_ * static_cast<float>(resolution_));

  if (resolution_ <= 255U)
  {
    DL_DAC12_output8(instance_, static_cast<uint8_t>(code));
  }
  else
  {
    DL_DAC12_output12(instance_, code);
  }

  return ErrorCode::OK;
}

#endif
