#include <stdbool.h>
#include <stdint.h>

#if !defined(__riscv_zaamo)
#error "CH32 QingKe V4 requires the RISC-V Zaamo extension"
#endif

#if defined(__riscv_zalrsc) || defined(__riscv_atomic)
#error "CH32 QingKe V4 must be compiled with Zaamo, not the full RISC-V A extension"
#endif

#if !defined(__riscv_zicsr) || !defined(__riscv_zifencei)
#error "CH32 QingKe V4 atomic shims require the RISC-V Zicsr and Zifencei extensions"
#endif

enum
{
  QINGKE_INTERRUPT_ENABLE_MASK = 0x88U,
};

static inline uint32_t atomic_enter(void)
{
  uint32_t interrupt_state;
  const uint32_t mask = QINGKE_INTERRUPT_ENABLE_MASK;
  __asm volatile("csrrc %0, 0x800, %1\n\tfence.i\n\tfence rw, rw"
                 : "=r"(interrupt_state)
                 : "r"(mask)
                 : "memory");
  return interrupt_state;
}

static inline void atomic_exit(uint32_t interrupt_state)
{
  __asm volatile("fence rw, rw" ::: "memory");
  const uint32_t restore = interrupt_state & QINGKE_INTERRUPT_ENABLE_MASK;
  if (restore != 0U)
  {
    __asm volatile("csrs 0x800, %0" : : "r"(restore) : "memory");
  }
}

/**
 * @brief Provide real single-core compare-exchange on QingKe V4.
 *
 * QingKe V4 implements AMO instructions but simplifies LR/SC to ordinary load/store
 * with an always-successful SC result. Compiling the target as Zaamo makes GCC call this
 * helper for conditional 32-bit updates while native fetch/exchange operations remain
 * single-instruction AMOs. The interrupt mask covers only the compare and optional store.
 */
__attribute__((used, noinline)) _Bool
__atomic_compare_exchange_4(volatile void* ptr, void* expected, unsigned int desired,
                            _Bool weak, int success_memorder, int failure_memorder)
{
  (void)weak;
  (void)success_memorder;
  (void)failure_memorder;

  volatile unsigned int* const value = (volatile unsigned int*)ptr;
  unsigned int* const expected_value = (unsigned int*)expected;
  const uint32_t interrupt_state = atomic_enter();
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

  atomic_exit(interrupt_state);
  return matched;
}

/**
 * @brief Provide byte exchange when Zaamo has no byte-sized AMO instruction.
 *
 * The same single-core critical section is used because GCC lowers atomic byte exchange
 * to this helper under the Zaamo-only target contract.
 */
__attribute__((used, noinline)) unsigned char __atomic_exchange_1(volatile void* ptr,
                                                                  unsigned char desired,
                                                                  int memorder)
{
  (void)memorder;

  volatile unsigned char* const value = (volatile unsigned char*)ptr;
  const uint32_t interrupt_state = atomic_enter();
  const unsigned char observed = *value;
  *value = desired;
  atomic_exit(interrupt_state);
  return observed;
}
