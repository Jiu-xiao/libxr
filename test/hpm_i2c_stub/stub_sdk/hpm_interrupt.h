#pragma once

#include <stdint.h>

#define SDK_DECLARE_EXT_ISR_M(irq, name)

static inline void intc_m_enable_irq_with_priority(uint32_t irq, uint32_t priority)
{
  (void)irq;
  (void)priority;
}

static inline void intc_m_disable_irq(uint32_t irq) { (void)irq; }
