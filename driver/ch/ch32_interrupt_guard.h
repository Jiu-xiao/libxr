#pragma once

#include <stdint.h>

#if !defined(__riscv_zicsr) || !defined(__riscv_zifencei)
#error "CH32 interrupt guards require the RISC-V Zicsr and Zifencei extensions"
#endif

enum
{
  LIBXR_CH32_INTERRUPT_ENABLE_MASK = 0x88U,
};

static inline uint32_t libxr_ch32_interrupt_save_and_disable(void)
{
  uint32_t interrupt_state;
  const uint32_t mask = LIBXR_CH32_INTERRUPT_ENABLE_MASK;
  __asm volatile("csrrc %0, 0x800, %1\n\tfence.i"
                 : "=r"(interrupt_state)
                 : "r"(mask)
                 : "memory");
  return interrupt_state;
}

static inline void libxr_ch32_interrupt_restore(uint32_t interrupt_state)
{
  const uint32_t restore = interrupt_state & LIBXR_CH32_INTERRUPT_ENABLE_MASK;
  if (restore != 0U)
  {
    __asm volatile("csrs 0x800, %0" : : "r"(restore) : "memory");
  }
}
