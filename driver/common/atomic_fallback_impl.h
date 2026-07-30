#pragma once

#if !defined(LIBXR_ATOMIC_FALLBACK_ATTRIBUTES)
#error "Define LIBXR_ATOMIC_FALLBACK_ATTRIBUTES before including this file"
#endif

#if !defined(LIBXR_ATOMIC_FALLBACK_FULL) &&               \
    !defined(LIBXR_ATOMIC_FALLBACK_COMPARE_EXCHANGE_4) && \
    !defined(LIBXR_ATOMIC_FALLBACK_EXCHANGE_1)
#error "Select at least one atomic fallback operation"
#endif

_Static_assert(sizeof(unsigned int) == 4U,
               "The __atomic_*_4 compiler ABI requires a 32-bit unsigned int");

typedef unsigned int libxr_atomic_fallback_u32_t __attribute__((may_alias));

/* GCC permits a runtime helper to ignore a weaker requested order when it provides
 * seq_cst semantics. Two unconditional platform fences keep this fallback small and
 * make its ordering independent of compiler-specific memory-order encodings. */
static inline void libxr_atomic_fallback_before(int memorder)
{
  (void)memorder;
  libxr_atomic_fallback_fence();
}

static inline void libxr_atomic_fallback_after(int memorder)
{
  (void)memorder;
  libxr_atomic_fallback_fence();
}

#if defined(LIBXR_ATOMIC_FALLBACK_FULL) || \
    defined(LIBXR_ATOMIC_FALLBACK_COMPARE_EXCHANGE_4)
/* The sized libatomic helper is always strong and therefore omits the builtin's weak
 * argument. Use an assembler label so the compiler does not compare this five-argument
 * runtime ABI against its six-argument source builtin declaration. */
LIBXR_ATOMIC_FALLBACK_ATTRIBUTES _Bool libxr_atomic_fallback_compare_exchange_4(
    volatile void* ptr, void* expected, unsigned int desired, int success_memorder,
    int failure_memorder) __asm__("__atomic_compare_exchange_4");

LIBXR_ATOMIC_FALLBACK_ATTRIBUTES _Bool libxr_atomic_fallback_compare_exchange_4(
    volatile void* ptr, void* expected, unsigned int desired, int success_memorder,
    int failure_memorder)
{
  volatile libxr_atomic_fallback_u32_t* const value =
      (volatile libxr_atomic_fallback_u32_t*)ptr;
  libxr_atomic_fallback_u32_t* const expected_value =
      (libxr_atomic_fallback_u32_t*)expected;
  const libxr_atomic_fallback_guard_state_t interrupt_state =
      libxr_atomic_fallback_enter();
  libxr_atomic_fallback_before(success_memorder);

  const unsigned int observed = *value;
  const _Bool matched = observed == *expected_value;
  if (matched)
  {
    *value = desired;
  }
  else
  {
    *expected_value = observed;
  }

  libxr_atomic_fallback_after(matched ? success_memorder : failure_memorder);
  libxr_atomic_fallback_exit(interrupt_state);
  return matched;
}
#endif

#if defined(LIBXR_ATOMIC_FALLBACK_FULL)
LIBXR_ATOMIC_FALLBACK_ATTRIBUTES void __atomic_store_4(volatile void* ptr,
                                                       unsigned int desired, int memorder)
{
  volatile libxr_atomic_fallback_u32_t* const value =
      (volatile libxr_atomic_fallback_u32_t*)ptr;
  const libxr_atomic_fallback_guard_state_t interrupt_state =
      libxr_atomic_fallback_enter();
  libxr_atomic_fallback_before(memorder);
  *value = desired;
  libxr_atomic_fallback_after(memorder);
  libxr_atomic_fallback_exit(interrupt_state);
}

LIBXR_ATOMIC_FALLBACK_ATTRIBUTES unsigned int __atomic_load_4(const volatile void* ptr,
                                                              int memorder)
{
  const volatile libxr_atomic_fallback_u32_t* const value =
      (const volatile libxr_atomic_fallback_u32_t*)ptr;
  const libxr_atomic_fallback_guard_state_t interrupt_state =
      libxr_atomic_fallback_enter();
  libxr_atomic_fallback_before(memorder);
  const unsigned int observed = *value;
  libxr_atomic_fallback_after(memorder);
  libxr_atomic_fallback_exit(interrupt_state);
  return observed;
}

LIBXR_ATOMIC_FALLBACK_ATTRIBUTES unsigned int __atomic_exchange_4(volatile void* ptr,
                                                                  unsigned int desired,
                                                                  int memorder)
{
  volatile libxr_atomic_fallback_u32_t* const value =
      (volatile libxr_atomic_fallback_u32_t*)ptr;
  const libxr_atomic_fallback_guard_state_t interrupt_state =
      libxr_atomic_fallback_enter();
  libxr_atomic_fallback_before(memorder);
  const unsigned int observed = *value;
  *value = desired;
  libxr_atomic_fallback_after(memorder);
  libxr_atomic_fallback_exit(interrupt_state);
  return observed;
}

LIBXR_ATOMIC_FALLBACK_ATTRIBUTES unsigned int __atomic_fetch_add_4(volatile void* ptr,
                                                                   unsigned int operand,
                                                                   int memorder)
{
  volatile libxr_atomic_fallback_u32_t* const value =
      (volatile libxr_atomic_fallback_u32_t*)ptr;
  const libxr_atomic_fallback_guard_state_t interrupt_state =
      libxr_atomic_fallback_enter();
  libxr_atomic_fallback_before(memorder);
  const unsigned int observed = *value;
  *value = observed + operand;
  libxr_atomic_fallback_after(memorder);
  libxr_atomic_fallback_exit(interrupt_state);
  return observed;
}

LIBXR_ATOMIC_FALLBACK_ATTRIBUTES unsigned int __atomic_fetch_sub_4(volatile void* ptr,
                                                                   unsigned int operand,
                                                                   int memorder)
{
  volatile libxr_atomic_fallback_u32_t* const value =
      (volatile libxr_atomic_fallback_u32_t*)ptr;
  const libxr_atomic_fallback_guard_state_t interrupt_state =
      libxr_atomic_fallback_enter();
  libxr_atomic_fallback_before(memorder);
  const unsigned int observed = *value;
  *value = observed - operand;
  libxr_atomic_fallback_after(memorder);
  libxr_atomic_fallback_exit(interrupt_state);
  return observed;
}

LIBXR_ATOMIC_FALLBACK_ATTRIBUTES unsigned int __atomic_fetch_or_4(volatile void* ptr,
                                                                  unsigned int operand,
                                                                  int memorder)
{
  volatile libxr_atomic_fallback_u32_t* const value =
      (volatile libxr_atomic_fallback_u32_t*)ptr;
  const libxr_atomic_fallback_guard_state_t interrupt_state =
      libxr_atomic_fallback_enter();
  libxr_atomic_fallback_before(memorder);
  const unsigned int observed = *value;
  *value = observed | operand;
  libxr_atomic_fallback_after(memorder);
  libxr_atomic_fallback_exit(interrupt_state);
  return observed;
}
#endif

#if defined(LIBXR_ATOMIC_FALLBACK_FULL) || defined(LIBXR_ATOMIC_FALLBACK_EXCHANGE_1)
LIBXR_ATOMIC_FALLBACK_ATTRIBUTES unsigned char __atomic_exchange_1(volatile void* ptr,
                                                                   unsigned char desired,
                                                                   int memorder)
{
  volatile unsigned char* const value = (volatile unsigned char*)ptr;
  const libxr_atomic_fallback_guard_state_t interrupt_state =
      libxr_atomic_fallback_enter();
  libxr_atomic_fallback_before(memorder);
  const unsigned char observed = *value;
  *value = desired;
  libxr_atomic_fallback_after(memorder);
  libxr_atomic_fallback_exit(interrupt_state);
  return observed;
}
#endif

#if defined(LIBXR_ATOMIC_FALLBACK_FULL)
LIBXR_ATOMIC_FALLBACK_ATTRIBUTES void __atomic_store_1(volatile void* ptr,
                                                       unsigned char desired,
                                                       int memorder)
{
  volatile unsigned char* const value = (volatile unsigned char*)ptr;
  const libxr_atomic_fallback_guard_state_t interrupt_state =
      libxr_atomic_fallback_enter();
  libxr_atomic_fallback_before(memorder);
  *value = desired;
  libxr_atomic_fallback_after(memorder);
  libxr_atomic_fallback_exit(interrupt_state);
}

#if !defined(__clang__)
LIBXR_ATOMIC_FALLBACK_ATTRIBUTES _Bool __atomic_test_and_set(volatile void* ptr,
                                                             int memorder)
{
  volatile unsigned char* const value = (volatile unsigned char*)ptr;
  const libxr_atomic_fallback_guard_state_t interrupt_state =
      libxr_atomic_fallback_enter();
  libxr_atomic_fallback_before(memorder);
  const unsigned char observed = *value;
  *value = 1U;
  libxr_atomic_fallback_after(memorder);
  libxr_atomic_fallback_exit(interrupt_state);
  return observed != 0U;
}
#endif
#endif
