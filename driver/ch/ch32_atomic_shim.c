#include <stdint.h>

#include "ch32_interrupt_guard.h"

#if !defined(__riscv_zaamo)
#error "CH32 QingKe V4 requires the RISC-V Zaamo extension"
#endif

#if defined(__riscv_zalrsc) || defined(__riscv_atomic)
#error "CH32 QingKe V4 must be compiled with Zaamo, not the full RISC-V A extension"
#endif

/* QingKe V4 LR/SC is not a reservation protocol. Keep native word AMOs, but provide
 * conditional word updates and byte exchange through a bounded single-core guard. */
typedef uint32_t libxr_atomic_fallback_guard_state_t;

static inline libxr_atomic_fallback_guard_state_t libxr_atomic_fallback_enter(void)
{
  return libxr_ch32_interrupt_save_and_disable();
}

static inline void libxr_atomic_fallback_exit(
    libxr_atomic_fallback_guard_state_t interrupt_state)
{
  libxr_ch32_interrupt_restore(interrupt_state);
}

static inline void libxr_atomic_fallback_fence(void)
{
  __asm volatile("fence rw, rw" ::: "memory");
}

#define LIBXR_ATOMIC_FALLBACK_ATTRIBUTES __attribute__((used, noinline))
#define LIBXR_ATOMIC_FALLBACK_COMPARE_EXCHANGE_4
#define LIBXR_ATOMIC_FALLBACK_EXCHANGE_1
#include "../common/atomic_fallback_impl.h"
