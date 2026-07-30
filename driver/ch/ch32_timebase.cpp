// NOLINTBEGIN(cppcoreguidelines-pro-type-cstyle-cast,performance-no-int-to-ptr)
#include "ch32_timebase.hpp"

using namespace LibXR;

namespace
{

constexpr uint32_t CH32_SYSTICK_ENABLE = 1U << 0U;
constexpr uint32_t CH32_SYSTICK_CLOCK_HCLK = 1U << 2U;
constexpr uint32_t CH32_SYSTICK_AUTO_RELOAD = 1U << 3U;
constexpr uint32_t CH32_SYSTICK_COUNT_DOWN = 1U << 4U;
constexpr uint64_t MICROSECONDS_PER_SECOND = 1000000ULL;
constexpr uint64_t MILLISECONDS_PER_SECOND = 1000ULL;

uint32_t timebase_clock_hz = 0U;

uint64_t ReadSysTickCounter()
{
  auto* count_words = reinterpret_cast<volatile uint32_t*>(&SysTick->CNT);
  uint32_t high_before = 0U;
  uint32_t low = 0U;
  uint32_t high_after = 0U;

  do
  {
    high_before = count_words[1];
    low = count_words[0];
    high_after = count_words[1];
  } while (high_before != high_after);

  return (static_cast<uint64_t>(high_after) << 32U) | low;
}

uint64_t CyclesToUnits(uint64_t cycles, uint64_t units_per_second)
{
  const uint64_t seconds = cycles / timebase_clock_hz;
  const uint64_t remaining_cycles = cycles % timebase_clock_hz;
  return seconds * units_per_second +
         remaining_cycles * units_per_second / timebase_clock_hz;
}

}  // namespace

CH32Timebase::CH32Timebase()
{
  const uint32_t control = SysTick->CTLR;
  const bool valid_control =
      (control & (CH32_SYSTICK_ENABLE | CH32_SYSTICK_CLOCK_HCLK)) ==
          (CH32_SYSTICK_ENABLE | CH32_SYSTICK_CLOCK_HCLK) &&
      (control & (CH32_SYSTICK_AUTO_RELOAD | CH32_SYSTICK_COUNT_DOWN)) == 0U;
  if (!valid_control || SystemCoreClock < MICROSECONDS_PER_SECOND)
  {
    libxr_fatal_error(__FILE__, __LINE__, false);
  }

  timebase_clock_hz = SystemCoreClock;
  ConfigureWrapRange(UINT64_MAX, UINT32_MAX);
  SetReady();
}

MicrosecondTimestamp Timebase::GetMicroseconds()
{
  return MicrosecondTimestamp(
      CyclesToUnits(ReadSysTickCounter(), MICROSECONDS_PER_SECOND));
}

MillisecondTimestamp Timebase::GetMilliseconds()
{
  return MillisecondTimestamp(static_cast<uint32_t>(
      CyclesToUnits(ReadSysTickCounter(), MILLISECONDS_PER_SECOND)));
}

// NOLINTEND(cppcoreguidelines-pro-type-cstyle-cast,performance-no-int-to-ptr)
