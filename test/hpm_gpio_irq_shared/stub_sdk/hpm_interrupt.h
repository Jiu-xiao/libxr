#pragma once

#include <cstdint>

#include "hpm_test_state.hpp"

inline void intc_m_enable_irq_with_priority(uint32_t irq, uint32_t priority)
{
  ++LibXRHpmTest::g_interrupt_state.enable_calls[irq];
  LibXRHpmTest::g_interrupt_state.priority[irq] = priority;
  LibXRHpmTest::g_interrupt_state.enabled[irq] = true;
}

inline void intc_m_disable_irq(uint32_t irq)
{
  ++LibXRHpmTest::g_interrupt_state.disable_calls[irq];
  LibXRHpmTest::g_interrupt_state.enabled[irq] = false;
}
