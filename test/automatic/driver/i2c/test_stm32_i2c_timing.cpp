#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>

#include "stm32_i2c_timing.hpp"

namespace
{
void Check(bool condition, int line)
{
  if (!condition)
  {
    std::fprintf(stderr, "I2C timing check failed at line %d\n", line);
    std::abort();
  }
}
#define CHECK(condition) Check((condition), __LINE__)

struct Limits
{
  uint32_t rise, fall, setup, data_valid, low, high;
};
Limits Spec(uint32_t rate)
{
  if (rate <= 100000U) return {1000, 300, 250, 3450, 4700, 4000};
  if (rate <= 400000U) return {300, 300, 100, 900, 1300, 600};
  return {120, 120, 50, 450, 500, 260};
}

// Independent register oracle: enumerate both SCL fields rather than using the
// production solver's analytical SCLH selection. All comparisons are exact.
uint64_t Oracle(uint32_t clock, uint32_t rate, bool analog, uint32_t dnf)
{
  constexpr uint64_t C = 1000000000ULL;
  const auto lim = Spec(rate);
  const int64_t af_min = analog ? 50LL * clock : 0;
  const int64_t af_max = analog ? 260LL * clock : 0;
  const int64_t lower = int64_t(lim.fall) * clock - af_min - int64_t(dnf + 3) * C;
  const int64_t upper =
      int64_t(lim.data_valid - lim.rise) * clock - af_max - int64_t(dnf + 4) * C;
  if (upper < 0 || upper < lower) return UINT64_MAX;
  const uint64_t target = (C * clock + rate - 1) / rate;
  const uint64_t sync = uint64_t(af_min) + (dnf + 2ULL) * C;
  uint64_t best = UINT64_MAX;
  for (uint32_t prescaler = 1; prescaler <= 16; ++prescaler)
  {
    const uint64_t unit = prescaler * C;
    bool data_hold = false, data_setup = false;
    for (uint32_t field = 0; field < 16; ++field)
    {
      const int64_t delay = int64_t(field * unit);
      data_hold |= delay >= lower && delay <= upper;
      data_setup |= (field + 1) * unit >= uint64_t(lim.rise + lim.setup) * clock;
    }
    if (!data_hold || !data_setup) continue;
    for (uint32_t low = 1; low <= 256; ++low)
    {
      if (low * unit <= 2 * C || low * unit + sync < uint64_t(lim.low) * clock) continue;
      for (uint32_t high = 1; high <= 256; ++high)
      {
        if (high * unit <= C || high * unit + sync < uint64_t(lim.high) * clock) continue;
        const uint64_t period = (low + high) * unit + 2 * sync;
        if (period >= target && period < best) best = period;
      }
    }
  }
  if (best == UINT64_MAX || best - target > target / 5) return UINT64_MAX;
  return best;
}

void Verify(uint32_t clock, uint32_t rate, bool analog, uint32_t dnf, uint32_t timing,
            uint64_t optimal)
{
  constexpr uint64_t C = 1000000000ULL;
  const uint64_t unit = ((timing >> 28) + 1ULL) * C;
  const uint64_t low = ((timing & 255U) + 1ULL) * unit;
  const uint64_t high = (((timing >> 8) & 255U) + 1ULL) * unit;
  const uint64_t sync = (analog ? 50ULL * clock : 0) + (dnf + 2ULL) * C;
  CHECK(low + high + 2 * sync == optimal);
  CHECK((timing & 0x0F000000U) == 0U);
  const auto lim = Spec(rate);
  const uint64_t setup = (((timing >> 20) & 15U) + 1ULL) * unit;
  const int64_t hold = ((timing >> 16) & 15U) * unit;
  CHECK(setup >= uint64_t(lim.rise + lim.setup) * clock);
  const int64_t lower =
      int64_t(lim.fall) * clock - (analog ? 50LL * clock : 0) - int64_t(dnf + 3) * C;
  const int64_t upper = int64_t(lim.data_valid - lim.rise) * clock -
                        (analog ? 260LL * clock : 0) - int64_t(dnf + 4) * C;
  CHECK(hold >= lower && hold <= upper);
  CHECK(low + sync >= uint64_t(lim.low) * clock && low > 2 * C);
  CHECK(high + sync >= uint64_t(lim.high) * clock && high > C);
  // Zero edge delay and the shortest analog-filter delay give the fastest SCL.
  // Never count the maximum rise/fall times as guaranteed period contributions.
  CHECK((low + high + 2 * sync) * rate >= C * clock);
}
}  // namespace

int main()
{
  uint32_t cases = 0, accepted = 0;
  for (uint32_t clock : {2000000U, 8000000U, 16000000U, 24000000U, 32000000U, 48000000U,
                         64000000U, 80000000U, 120000000U, 160000000U})
    for (uint32_t rate : {10000U, 100000U, 333333U, 400000U, 1000000U})
      for (bool analog : {false, true})
        for (uint32_t dnf : {0U, 2U, 15U})
        {
          uint32_t timing = 0xA5A5A5A5U;
          const bool ok =
              LibXR::STM32I2CTiming::Compute(clock, rate, analog, dnf, timing);
          const uint64_t optimal = Oracle(clock, rate, analog, dnf);
          CHECK(ok == (optimal != UINT64_MAX));
          if (ok)
          {
            Verify(clock, rate, analog, dnf, timing, optimal);
            ++accepted;
          }
          else
            CHECK(timing == 0xA5A5A5A5U);
          ++cases;
        }
  for (auto values :
       {std::array<uint32_t, 3>{0, 100000, 0}, std::array<uint32_t, 3>{16000000, 0, 0},
        std::array<uint32_t, 3>{16000000, 1000001, 0},
        std::array<uint32_t, 3>{16000000, 100000, 16},
        std::array<uint32_t, 3>{UINT32_MAX, 1, 0}})
  {
    uint32_t timing = 0xA5A5A5A5U;
    CHECK(!LibXR::STM32I2CTiming::Compute(values[0], values[1], true, values[2], timing));
    CHECK(timing == 0xA5A5A5A5U);
  }
  uint32_t timing = 0;
  CHECK(LibXR::STM32I2CTiming::Compute(16000000, 400000, true, 0, timing));
  std::printf("I2C timing: %u oracle cases, %u accepted, invalid inputs preserved\n",
              cases, accepted);
}
