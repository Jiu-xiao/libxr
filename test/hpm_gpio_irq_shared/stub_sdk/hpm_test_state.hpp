#pragma once

#include <cstdint>
#include <cstring>

#include "hpm_gpio_drv.h"
#include "hpm_ioc_regs.h"

namespace LibXRHpmTest
{
constexpr uint32_t kStubIrqCount = 256u;

struct InterruptState
{
  uint32_t enable_calls[kStubIrqCount] = {};
  uint32_t disable_calls[kStubIrqCount] = {};
  uint32_t priority[kStubIrqCount] = {};
  bool enabled[kStubIrqCount] = {};

  void Reset();
};

extern GPIO_Type g_gpio0;
extern GPIO_Type g_fgpio;
extern IOC_Type g_ioc;
extern PIOC_Type g_pioc;
extern InterruptState g_interrupt_state;

void ResetTestState();
}  // namespace LibXRHpmTest
