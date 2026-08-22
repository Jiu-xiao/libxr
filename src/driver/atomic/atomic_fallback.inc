/**
 * @file atomic_fallback.inc
 * @brief Compiler atomic fallback implementation for one interrupt domain.
 *
 * Include this fragment exactly once from a platform C file after defining:
 * - `libxr_atomic_fallback_guard_state_t`;
 * - `libxr_atomic_fallback_enter()`, `libxr_atomic_fallback_exit()`, and
 *   `libxr_atomic_fallback_fence()`;
 * - `LIBXR_ATOMIC_FALLBACK_ATTRIBUTES`;
 * - one or more `LIBXR_ATOMIC_FALLBACK_*` operation selectors.
 *
 * Atomicity covers maskable thread and interrupt contexts on one CPU. It does not cover
 * another CPU, NMI/HardFault, or DMA bus masters. Every requested memory order is
 * conservatively implemented with sequential consistency.
 */

#if !defined(LIBXR_ATOMIC_FALLBACK_ATTRIBUTES)
#error "Define LIBXR_ATOMIC_FALLBACK_ATTRIBUTES before including this file"
#endif

#if !defined(LIBXR_ATOMIC_FALLBACK_32BIT_OPERATIONS) && \
    !defined(LIBXR_ATOMIC_FALLBACK_COMPARE_EXCHANGE_4)
#error "Select at least one atomic fallback operation"
#endif

_Static_assert(sizeof(unsigned int) == 4U,
               "The __atomic_*_4 compiler ABI requires a 32-bit unsigned int");

typedef unsigned int libxr_atomic_fallback_u32_t __attribute__((may_alias));

/* GCC permits a runtime helper to implement a weaker request as seq_cst. The two
 * platform fences also avoid depending on compiler-specific memory-order encodings. */
static inline __attribute__((always_inline)) void libxr_atomic_fallback_before(
    int memorder)
{
  (void)memorder;
  libxr_atomic_fallback_fence();
}

static inline __attribute__((always_inline)) void libxr_atomic_fallback_after(
    int memorder)
{
  (void)memorder;
  libxr_atomic_fallback_fence();
}

#if defined(LIBXR_ATOMIC_FALLBACK_32BIT_OPERATIONS) || \
    defined(LIBXR_ATOMIC_FALLBACK_COMPARE_EXCHANGE_4)
/* The sized libatomic helper is always a strong CAS and omits the source builtin's weak
 * argument. The assembler label keeps its five-argument runtime ABI separate from the
 * six-argument builtin declaration. */
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

#if defined(LIBXR_ATOMIC_FALLBACK_32BIT_OPERATIONS)
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

#undef LIBXR_ATOMIC_FALLBACK_32BIT_OPERATIONS
#undef LIBXR_ATOMIC_FALLBACK_COMPARE_EXCHANGE_4
#undef LIBXR_ATOMIC_FALLBACK_ATTRIBUTES
