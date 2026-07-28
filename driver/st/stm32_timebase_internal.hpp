#pragma once

#include <cstdint>

namespace LibXR
{
namespace STM32TimebaseInternal
{
struct SampleResult
{
  bool stable;
  uint64_t microseconds;
};

constexpr SampleResult ResolveUpCounterSample(uint32_t tick_before,
                                              uint32_t counter_before,
                                              bool update_pending, uint32_t counter_after,
                                              uint32_t tick_after, uint64_t period_counts)
{
  if (tick_before != tick_after)
  {
    return {false, 0U};
  }

  const bool period_elapsed = update_pending || counter_after < counter_before;
  const uint32_t logical_tick = tick_before + static_cast<uint32_t>(period_elapsed);
  const uint64_t fraction = static_cast<uint64_t>(counter_after) * 1000U / period_counts;
  return {true, static_cast<uint64_t>(logical_tick) * 1000U + fraction};
}

constexpr SampleResult ResolveSysTickSample(uint32_t tick_before, uint32_t counter_before,
                                            bool exception_pending,
                                            uint32_t counter_after, uint32_t tick_after,
                                            uint64_t period_counts)
{
  if (tick_before != tick_after)
  {
    return {false, 0U};
  }

  const bool crossed_zero =
      counter_before != 0U && (counter_after == 0U || counter_after > counter_before);
  const bool period_elapsed = exception_pending || crossed_zero;
  uint32_t logical_tick = tick_before + static_cast<uint32_t>(period_elapsed);

  // VAL remains zero for the boundary cycle, then reloads on the next counter clock.
  uint64_t fraction = 0U;
  if (counter_after != 0U)
  {
    fraction = 1000U - static_cast<uint64_t>(counter_after) * 1000U / period_counts;
    if (fraction == 1000U)
    {
      ++logical_tick;
      fraction = 0U;
    }
  }
  return {true, static_cast<uint64_t>(logical_tick) * 1000U + fraction};
}
}  // namespace STM32TimebaseInternal
}  // namespace LibXR
