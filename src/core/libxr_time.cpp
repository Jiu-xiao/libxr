#include "libxr_time.hpp"

using namespace LibXR;

uint64_t libxr_timebase_max_valid_us = UINT64_MAX;  // NOLINT
uint32_t libxr_timebase_max_valid_ms = UINT32_MAX;  // NOLINT

MicrosecondTimestamp::MicrosecondTimestamp() : microsecond_(0) {}

MicrosecondTimestamp::MicrosecondTimestamp(uint64_t microsecond)
    : microsecond_(microsecond)
{
}

MicrosecondTimestamp::operator uint64_t() const { return microsecond_; }

MicrosecondTimestamp::Duration::Duration(uint64_t diff) : diff_(diff) {}

MicrosecondTimestamp::Duration::Duration::operator uint64_t() const { return diff_; }

[[nodiscard]] double MicrosecondTimestamp::Duration::Duration::ToSecond() const
{
  return static_cast<double>(diff_) / 1000000.0;
}

[[nodiscard]] float MicrosecondTimestamp::Duration::Duration::ToSecondf() const
{
  return static_cast<float>(diff_) / 1000000.0f;
}

[[nodiscard]] uint64_t MicrosecondTimestamp::Duration::Duration::ToMicrosecond() const
{
  return diff_;
}

[[nodiscard]] uint32_t MicrosecondTimestamp::Duration::Duration::ToMillisecond() const
{
  return diff_ / 1000u;
}

MicrosecondTimestamp::Duration MicrosecondTimestamp::operator-(
    const MicrosecondTimestamp &old_microsecond) const
{
  uint64_t diff;  // NOLINT

  if (microsecond_ >= old_microsecond.microsecond_)
  {
    diff = microsecond_ - old_microsecond.microsecond_;
  }
  else
  {
    diff = microsecond_ + (libxr_timebase_max_valid_us - old_microsecond.microsecond_);
  }

  ASSERT(diff <= libxr_timebase_max_valid_us);

  return Duration(diff);
}

MicrosecondTimestamp &MicrosecondTimestamp::operator=(const MicrosecondTimestamp &other)
{
  if (this != &other)
  {
    microsecond_ = other.microsecond_;
  }
  return *this;
}

MillisecondTimestamp::MillisecondTimestamp() : millisecond_(0) {}

MillisecondTimestamp::MillisecondTimestamp(uint32_t millisecond)
    : millisecond_(millisecond)
{
}

MillisecondTimestamp::operator uint32_t() const { return millisecond_; }

MillisecondTimestamp::Duration::Duration(uint32_t diff) : diff_(diff) {}

MillisecondTimestamp::Duration::operator uint32_t() const { return diff_; }

[[nodiscard]] double MillisecondTimestamp::Duration::ToSecond() const
{
  return static_cast<double>(diff_) / 1000.0;
}

[[nodiscard]] float MillisecondTimestamp::Duration::ToSecondf() const
{
  return static_cast<float>(diff_) / 1000.0f;
}

[[nodiscard]] uint32_t MillisecondTimestamp::Duration::ToMillisecond() const
{
  return diff_;
}

[[nodiscard]] uint64_t MillisecondTimestamp::Duration::ToMicrosecond() const
{
  return static_cast<uint64_t>(diff_) * 1000u;
}

[[nodiscard]] MillisecondTimestamp::Duration MillisecondTimestamp::operator-(
    const MillisecondTimestamp &old_millisecond) const
{
  uint32_t diff;  // NOLINT

  if (millisecond_ >= old_millisecond.millisecond_)
  {
    diff = millisecond_ - old_millisecond.millisecond_;
  }
  else
  {
    diff = millisecond_ + (libxr_timebase_max_valid_ms - old_millisecond.millisecond_);
  }

  ASSERT(diff <= libxr_timebase_max_valid_ms);

  return Duration(diff);
}
