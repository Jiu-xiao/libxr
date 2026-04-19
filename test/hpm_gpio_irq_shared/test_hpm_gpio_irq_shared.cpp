#include <cstdlib>
#include <iostream>

#include "hpm_gpio.hpp"
#include "hpm_test_state.hpp"

namespace
{
void Expect(bool condition, const char* message)
{
  if (!condition)
  {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

void CountInterrupt(bool, uint32_t* counter)
{
  ++(*counter);
}
}  // namespace

int main()
{
  using LibXR::ErrorCode;
  using LibXR::GPIO;
  using LibXR::HPMGPIO;

  LibXRHpmTest::ResetTestState();

  uint32_t pin3_irq_count = 0u;
  uint32_t pin5_irq_count = 0u;

  constexpr uint32_t shared_port = GPIO_DI_GPIOA;
  constexpr uint32_t shared_irq = 42u;

  HPMGPIO pin3(HPM_GPIO0, shared_port, 3u, shared_irq);
  HPMGPIO pin5(HPM_GPIO0, shared_port, 5u, shared_irq);

  pin3.RegisterCallback(GPIO::Callback::Create(CountInterrupt, &pin3_irq_count));
  pin5.RegisterCallback(GPIO::Callback::Create(CountInterrupt, &pin5_irq_count));

  Expect(pin3.EnableInterrupt() == ErrorCode::OK, "pin3 enable should succeed");
  Expect(LibXRHpmTest::g_interrupt_state.enable_calls[shared_irq] == 1u,
         "first pin enable should route the shared port IRQ once");

  Expect(pin3.EnableInterrupt() == ErrorCode::OK, "repeated pin3 enable should be a no-op");
  Expect(LibXRHpmTest::g_interrupt_state.enable_calls[shared_irq] == 1u,
         "repeated enable must not re-enable the shared port IRQ");

  Expect(pin5.EnableInterrupt() == ErrorCode::OK, "pin5 enable should succeed");
  Expect(LibXRHpmTest::g_interrupt_state.enable_calls[shared_irq] == 1u,
         "second pin on same port must not re-enable the shared port IRQ");
  Expect(LibXRHpmTest::g_interrupt_state.enabled[shared_irq],
         "shared port IRQ should stay enabled while any pin is active");
  Expect(LibXRHpmTest::g_gpio0.IE[shared_port].VALUE == ((1u << 3u) | (1u << 5u)),
         "both pin interrupt enable bits should be set");

  LibXRHpmTest::g_gpio0.IF[shared_port].VALUE = (1u << 3u) | (1u << 5u);
  HPMGPIO::CheckInterrupt(shared_port);
  Expect(pin3_irq_count == 1u, "pin3 callback should fire on the shared port IRQ");
  Expect(pin5_irq_count == 1u, "pin5 callback should fire on the shared port IRQ");
  Expect(LibXRHpmTest::g_gpio0.IF[shared_port].VALUE == 0u,
         "port interrupt flags should be cleared after dispatch");

  Expect(pin5.DisableInterrupt() == ErrorCode::OK, "pin5 disable should succeed");
  Expect(LibXRHpmTest::g_interrupt_state.disable_calls[shared_irq] == 0u,
         "disabling one shared pin must not disable the shared port IRQ");
  Expect(LibXRHpmTest::g_gpio0.IE[shared_port].VALUE == (1u << 3u),
         "disabling pin5 must leave pin3 interrupt enable bit intact");

  LibXRHpmTest::g_gpio0.IF[shared_port].VALUE = (1u << 3u);
  HPMGPIO::CheckInterrupt(shared_port);
  Expect(pin3_irq_count == 2u, "pin3 should still receive interrupts after pin5 is disabled");
  Expect(pin5_irq_count == 1u, "pin5 callback count should remain unchanged after disable");

  Expect(pin3.DisableInterrupt() == ErrorCode::OK, "pin3 disable should succeed");
  Expect(LibXRHpmTest::g_interrupt_state.disable_calls[shared_irq] == 1u,
         "last active pin must disable the shared port IRQ exactly once");
  Expect(!LibXRHpmTest::g_interrupt_state.enabled[shared_irq],
         "shared port IRQ should be disabled after the last pin is released");

  Expect(pin3.DisableInterrupt() == ErrorCode::OK, "repeated pin3 disable should be a no-op");
  Expect(LibXRHpmTest::g_interrupt_state.disable_calls[shared_irq] == 1u,
         "repeated disable must not touch the shared port IRQ");

  const auto enable_calls_before_never_enabled =
      LibXRHpmTest::g_interrupt_state.enable_calls[shared_irq];
  const auto disable_calls_before_never_enabled =
      LibXRHpmTest::g_interrupt_state.disable_calls[shared_irq];
  HPMGPIO never_enabled_pin(HPM_GPIO0, shared_port, 7u, shared_irq);
  Expect(never_enabled_pin.DisableInterrupt() == ErrorCode::OK,
         "DisableInterrupt on a never-enabled pin must succeed");
  Expect(LibXRHpmTest::g_interrupt_state.enable_calls[shared_irq] ==
             enable_calls_before_never_enabled,
         "DisableInterrupt on a never-enabled pin must not change shared IRQ enable count");
  Expect(LibXRHpmTest::g_interrupt_state.disable_calls[shared_irq] ==
             disable_calls_before_never_enabled,
         "DisableInterrupt on a never-enabled pin must not change shared IRQ disable count");

  constexpr uint32_t mismatch_port = GPIO_DI_GPIOB;
  constexpr uint32_t port_b_irq = 11u;
  constexpr uint32_t mismatched_irq = 12u;

  HPMGPIO port_b_pin1(HPM_GPIO0, mismatch_port, 1u, port_b_irq);
  HPMGPIO port_b_pin2(HPM_GPIO0, mismatch_port, 2u, mismatched_irq);

  Expect(port_b_pin1.EnableInterrupt() == ErrorCode::OK, "first pin on port B should enable");
  Expect(port_b_pin2.EnableInterrupt() == ErrorCode::STATE_ERR,
         "same port with mismatched IRQ number must be rejected");
  Expect(LibXRHpmTest::g_interrupt_state.enable_calls[port_b_irq] == 1u,
         "accepted port B IRQ should be routed once");
  Expect(LibXRHpmTest::g_interrupt_state.enable_calls[mismatched_irq] == 0u,
         "rejected mismatched IRQ must never be enabled");
  Expect(LibXRHpmTest::g_gpio0.IE[mismatch_port].VALUE == (1u << 1u),
         "rejected mismatched IRQ must not enable its pin bit");

  Expect(port_b_pin1.DisableInterrupt() == ErrorCode::OK, "port B cleanup should succeed");
  Expect(LibXRHpmTest::g_interrupt_state.disable_calls[port_b_irq] == 1u,
         "port B IRQ should be disabled once after cleanup");

  std::cout << "test_hpm_gpio_irq_shared passed\n";
  return 0;
}
