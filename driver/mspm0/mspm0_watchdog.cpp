#include "mspm0_watchdog.hpp"

#if defined(__MSPM0_HAS_WWDT__)

using namespace LibXR;

namespace
{

constexpr DL_WWDT_CLOCK_DIVIDE DIV_TABLE[] = {
    DL_WWDT_CLOCK_DIVIDE_1, DL_WWDT_CLOCK_DIVIDE_2, DL_WWDT_CLOCK_DIVIDE_3,
    DL_WWDT_CLOCK_DIVIDE_4, DL_WWDT_CLOCK_DIVIDE_5, DL_WWDT_CLOCK_DIVIDE_6,
    DL_WWDT_CLOCK_DIVIDE_7, DL_WWDT_CLOCK_DIVIDE_8};

struct PeriodItem
{
  DL_WWDT_TIMER_PERIOD period;
  uint8_t bits;
};

constexpr PeriodItem PERIOD_TABLE[] = {
    {DL_WWDT_TIMER_PERIOD_6_BITS, 6},   {DL_WWDT_TIMER_PERIOD_8_BITS, 8},
    {DL_WWDT_TIMER_PERIOD_10_BITS, 10}, {DL_WWDT_TIMER_PERIOD_12_BITS, 12},
    {DL_WWDT_TIMER_PERIOD_15_BITS, 15}, {DL_WWDT_TIMER_PERIOD_18_BITS, 18},
    {DL_WWDT_TIMER_PERIOD_21_BITS, 21}, {DL_WWDT_TIMER_PERIOD_25_BITS, 25}};

}  // namespace

MSPM0Watchdog::MSPM0Watchdog(WWDT_Regs* wwdt, uint32_t timeout_ms, uint32_t feed_ms,
                             uint32_t clock)
    : wwdt_(wwdt),
      clock_(clock),
      divider_(DL_WWDT_CLOCK_DIVIDE_8),
      period_(DL_WWDT_TIMER_PERIOD_25_BITS),
      initialized_(false)
{
  ASSERT(wwdt_ != nullptr);
  ASSERT(clock_ > 0U);
  SetConfig({timeout_ms, feed_ms});
  Start();
}

ErrorCode MSPM0Watchdog::SetConfig(const Configuration& config)
{
  if (config.feed_ms == 0U || config.timeout_ms == 0U ||
      config.feed_ms > config.timeout_ms)
  {
    return ErrorCode::ARG_ERR;
  }

  bool found = false;
  DL_WWDT_CLOCK_DIVIDE best_divider = DL_WWDT_CLOCK_DIVIDE_8;
  DL_WWDT_TIMER_PERIOD best_period = DL_WWDT_TIMER_PERIOD_25_BITS;

  for (const auto& divider : DIV_TABLE)
  {
    const uint32_t divider_num = static_cast<uint32_t>(divider) + 1U;
    for (const auto& item : PERIOD_TABLE)
    {
      const uint32_t ticks = 1UL << item.bits;
      const uint64_t timeout_calc_ms =
          (static_cast<uint64_t>(ticks) * 1000ULL * static_cast<uint64_t>(divider_num)) /
          static_cast<uint64_t>(clock_);

      if (timeout_calc_ms >= config.timeout_ms)
      {
        best_divider = divider;
        best_period = item.period;
        found = true;
        break;
      }
    }

    if (found)
    {
      break;
    }
  }

  if (!found)
  {
    return ErrorCode::NOT_SUPPORT;
  }

  timeout_ms_ = config.timeout_ms;
  auto_feed_interval_ms = config.feed_ms;
  divider_ = best_divider;
  period_ = best_period;

  return ErrorCode::OK;
}

ErrorCode MSPM0Watchdog::Feed()
{
  if (!initialized_)
  {
    return ErrorCode::FAILED;
  }

  DL_WWDT_restart(wwdt_);
  return ErrorCode::OK;
}

ErrorCode MSPM0Watchdog::Start()
{
  if (!initialized_)
  {
    DL_WWDT_enablePower(wwdt_);
    DL_WWDT_initWatchdogMode(wwdt_, divider_, period_, DL_WWDT_RUN_IN_SLEEP,
                             DL_WWDT_WINDOW_PERIOD_0, DL_WWDT_WINDOW_PERIOD_0);
    initialized_ = true;
  }

  auto_feed_ = true;
  return ErrorCode::OK;
}

ErrorCode MSPM0Watchdog::Stop()
{
  auto_feed_ = false;
  return ErrorCode::NOT_SUPPORT;
}

#endif
