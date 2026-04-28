#include "hpm_test_state.hpp"

namespace LibXRHpmTest
{
GPIO_Type g_gpio0 = {};
GPIO_Type g_fgpio = {};
IOC_Type g_ioc = {};
PIOC_Type g_pioc = {};
InterruptState g_interrupt_state = {};

void InterruptState::Reset()
{
  std::memset(this, 0, sizeof(*this));
}

void ResetTestState()
{
  std::memset(&g_gpio0, 0, sizeof(g_gpio0));
  std::memset(&g_fgpio, 0, sizeof(g_fgpio));
  std::memset(&g_ioc, 0, sizeof(g_ioc));
  std::memset(&g_pioc, 0, sizeof(g_pioc));
  g_interrupt_state.Reset();
}
}  // namespace LibXRHpmTest
