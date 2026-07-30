#include <cstdint>

#include "stm32_timebase_internal.hpp"

namespace
{
using LibXR::STM32TimebaseInternal::ResolveSysTickSample;
using LibXR::STM32TimebaseInternal::ResolveUpCounterSample;

constexpr auto UP_STABLE = ResolveUpCounterSample(100U, 980U, false, 985U, 100U, 1000U);
static_assert(UP_STABLE.stable && UP_STABLE.microseconds == 100985U);

constexpr auto UP_PENDING = ResolveUpCounterSample(100U, 3U, true, 7U, 100U, 1000U);
static_assert(UP_PENDING.stable && UP_PENDING.microseconds == 101007U);

constexpr auto UP_CROSSED = ResolveUpCounterSample(100U, 998U, false, 2U, 100U, 1000U);
static_assert(UP_CROSSED.stable && UP_CROSSED.microseconds == 101002U);

constexpr auto UP_SCALED = ResolveUpCounterSample(100U, 500U, false, 1000U, 100U, 2000U);
static_assert(UP_SCALED.stable && UP_SCALED.microseconds == 100500U);

constexpr auto UP_RETRY = ResolveUpCounterSample(100U, 998U, false, 2U, 101U, 1000U);
static_assert(!UP_RETRY.stable);

constexpr auto UP_TICK_WRAP =
    ResolveUpCounterSample(UINT32_MAX, 998U, false, 2U, UINT32_MAX, 1000U);
static_assert(UP_TICK_WRAP.stable && UP_TICK_WRAP.microseconds == 2U);

constexpr auto SYSTICK_STABLE =
    ResolveSysTickSample(100U, 700U, false, 690U, 100U, 1000U);
static_assert(SYSTICK_STABLE.stable && SYSTICK_STABLE.microseconds == 100310U);

constexpr auto SYSTICK_PENDING = ResolveSysTickSample(100U, 3U, true, 997U, 100U, 1000U);
static_assert(SYSTICK_PENDING.stable && SYSTICK_PENDING.microseconds == 101003U);

constexpr auto SYSTICK_ZERO = ResolveSysTickSample(100U, 1U, false, 0U, 100U, 1000U);
static_assert(SYSTICK_ZERO.stable && SYSTICK_ZERO.microseconds == 101000U);

constexpr auto SYSTICK_RELOAD = ResolveSysTickSample(100U, 1U, false, 999U, 100U, 1000U);
static_assert(SYSTICK_RELOAD.stable && SYSTICK_RELOAD.microseconds == 101001U);

constexpr auto SYSTICK_PENDING_ZERO =
    ResolveSysTickSample(100U, 0U, true, 0U, 100U, 1000U);
static_assert(SYSTICK_PENDING_ZERO.stable &&
              SYSTICK_PENDING_ZERO.microseconds == 101000U);

constexpr auto SYSTICK_RETIRED_ZERO =
    ResolveSysTickSample(101U, 0U, false, 0U, 101U, 1000U);
static_assert(SYSTICK_RETIRED_ZERO.stable &&
              SYSTICK_RETIRED_ZERO.microseconds == 101000U);

constexpr auto SYSTICK_RETIRED_RELOAD =
    ResolveSysTickSample(101U, 0U, false, 999U, 101U, 1000U);
static_assert(SYSTICK_RETIRED_RELOAD.stable &&
              SYSTICK_RETIRED_RELOAD.microseconds == 101001U);

constexpr auto SYSTICK_REAL_PERIOD =
    ResolveSysTickSample(100U, 167999U, false, 167999U, 100U, 168000U);
static_assert(SYSTICK_REAL_PERIOD.stable && SYSTICK_REAL_PERIOD.microseconds == 100001U);

constexpr auto SYSTICK_ROUND_TO_NEXT_TICK =
    ResolveSysTickSample(100U, 1U, true, 1U, 100U, 168000U);
static_assert(SYSTICK_ROUND_TO_NEXT_TICK.stable &&
              SYSTICK_ROUND_TO_NEXT_TICK.microseconds == 102000U);

constexpr auto SYSTICK_FRACTION_TICK_WRAP =
    ResolveSysTickSample(UINT32_MAX, 1U, false, 1U, UINT32_MAX, 168000U);
static_assert(SYSTICK_FRACTION_TICK_WRAP.stable &&
              SYSTICK_FRACTION_TICK_WRAP.microseconds == 0U);

constexpr auto SYSTICK_RETRY = ResolveSysTickSample(100U, 1U, false, 999U, 101U, 1000U);
static_assert(!SYSTICK_RETRY.stable);

constexpr auto SYSTICK_PENDING_TICK_WRAP =
    ResolveSysTickSample(UINT32_MAX, 0U, true, 0U, UINT32_MAX, 1000U);
static_assert(SYSTICK_PENDING_TICK_WRAP.stable &&
              SYSTICK_PENDING_TICK_WRAP.microseconds == 0U);
}  // namespace
