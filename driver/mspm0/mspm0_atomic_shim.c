#include <ti/devices/msp/msp.h>

/* Cortex-M0+ has no native 32-bit compare-exchange. This fallback serializes maskable
 * interrupt and thread contexts on one core; it does not cover NMI or SMP callers. */
typedef uint32_t libxr_atomic_fallback_guard_state_t;

static inline __attribute__((always_inline)) libxr_atomic_fallback_guard_state_t
libxr_atomic_fallback_enter(void)
{
  const uint32_t primask_state = __get_PRIMASK();
  __disable_irq();
  return primask_state;
}

static inline __attribute__((always_inline)) void libxr_atomic_fallback_exit(
    libxr_atomic_fallback_guard_state_t primask_state)
{
  __set_PRIMASK(primask_state);
}

static inline __attribute__((always_inline)) void libxr_atomic_fallback_fence(void)
{
  __DMB();
}

#define LIBXR_ATOMIC_FALLBACK_ATTRIBUTES __attribute__((used, noinline))
#define LIBXR_ATOMIC_FALLBACK_32BIT_OPERATIONS
#include "driver/atomic/atomic_fallback.inc"
