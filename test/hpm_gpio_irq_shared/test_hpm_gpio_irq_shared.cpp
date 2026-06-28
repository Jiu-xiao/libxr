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

  LibXRHpmTest::g_gpio0.IF[shared_port].VALUE = (1u << 3u) | (1u << 5u);
  HPMGPIO::CheckInterrupt(shared_port);
  Expect(pin3_irq_count == 2u, "pin3 should still receive interrupts after pin5 is disabled");
  Expect(pin5_irq_count == 1u,
         "disabled pin5 must not dispatch even if a stale interrupt flag remains set");

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

  HPMGPIO no_both_edge_pin(HPM_GPIO0, shared_port, 11u, shared_irq);
  Expect(no_both_edge_pin.SetConfig(
             {GPIO::Direction::FALL_RISING_INTERRUPT, GPIO::Pull::NONE}) ==
             ErrorCode::NOT_SUPPORT,
         "both-edge interrupt config should be rejected when the SDK feature macro is absent");
  Expect(LibXRHpmTest::g_ioc.PAD[IOC_PAD_PA00 + 11u].FUNC_CTL == 0u,
         "unsupported both-edge config must not change pad mux state");

  constexpr uint32_t gpioz_irq = 99u;
  HPMGPIO invalid_pull_resolved_pin(HPM_GPIO0, shared_port, 12u, shared_irq);
  Expect(invalid_pull_resolved_pin.SetConfig(
             {GPIO::Direction::OUTPUT_PUSH_PULL, static_cast<GPIO::Pull>(0xFFu)}) ==
             ErrorCode::ARG_ERR,
         "invalid pull should be rejected before configuring a resolved pad");
  Expect((LibXRHpmTest::g_gpio0.OE[shared_port].VALUE & (1u << 12u)) == 0u,
         "invalid pull must not change GPIO direction on a resolved pad");
  Expect(LibXRHpmTest::g_ioc.PAD[IOC_PAD_PA00 + 12u].FUNC_CTL == 0u,
         "invalid pull must not change pad mux on a resolved pad");

  HPMGPIO invalid_pull_unresolved_pin(HPM_GPIO0, GPIO_DI_GPIOZ, 3u, gpioz_irq);
  Expect(invalid_pull_unresolved_pin.SetConfig(
             {GPIO::Direction::OUTPUT_PUSH_PULL, static_cast<GPIO::Pull>(0xFEu)}) ==
             ErrorCode::ARG_ERR,
         "invalid pull should be rejected even when the pad cannot be resolved");
  Expect((LibXRHpmTest::g_gpio0.OE[GPIO_DI_GPIOZ].VALUE & (1u << 3u)) == 0u,
         "invalid pull must not change GPIO direction on an unresolved pad");

  uint32_t gpioz_irq_count = 0u;
  HPMGPIO gpioz_pin(HPM_GPIO0, GPIO_DI_GPIOZ, 2u, gpioz_irq);
  gpioz_pin.RegisterCallback(GPIO::Callback::Create(CountInterrupt, &gpioz_irq_count));
  Expect(gpioz_pin.EnableInterrupt() == ErrorCode::OK,
         "GPIOZ should be accepted when the SDK exposes GPIO_DI_GPIOZ");
  Expect(LibXRHpmTest::g_interrupt_state.enable_calls[gpioz_irq] == 1u,
         "GPIOZ IRQ should route once on first enable");
  LibXRHpmTest::g_gpio0.IF[GPIO_DI_GPIOZ].VALUE = (1u << 2u);
  HPMGPIO::CheckInterrupt(GPIO_DI_GPIOZ);
  Expect(gpioz_irq_count == 1u, "GPIOZ callback should fire through the shared map");
  Expect(gpioz_pin.DisableInterrupt() == ErrorCode::OK, "GPIOZ cleanup should succeed");

  constexpr uint32_t analog_irq = 66u;
  HPMGPIO analog_pin(HPM_GPIO0, shared_port, 10u, analog_irq);
  Expect(analog_pin.EnableInterrupt() == ErrorCode::OK,
         "analog transition setup should enable the pin interrupt first");
  const ErrorCode analog_high_z_ans = analog_pin.SetAnalogHighImpedance();
  Expect(analog_high_z_ans == ErrorCode::OK,
         "SetAnalogHighImpedance should disable an active interrupt through shared route state");
  Expect(LibXRHpmTest::g_gpio0.IE[shared_port].VALUE == 0u,
         "analog high-Z transition should clear the pin interrupt enable bit");
  Expect(LibXRHpmTest::g_interrupt_state.disable_calls[analog_irq] == 1u,
         "analog high-Z transition should release the shared port IRQ route");
  Expect(LibXRHpmTest::g_ioc.PAD[IOC_PAD_PA00 + 10u].FUNC_CTL ==
             IOC_PAD_FUNC_CTL_ANALOG_MASK,
         "analog high-Z transition should put the pad into analog mode");
  Expect(analog_pin.EnableInterrupt() == ErrorCode::OK,
         "interrupt should be re-enableable after analog high-Z route cleanup");
  Expect(LibXRHpmTest::g_interrupt_state.enable_calls[analog_irq] == 2u,
         "re-enable after analog high-Z should route the shared IRQ again");
  Expect(analog_pin.DisableInterrupt() == ErrorCode::OK,
         "analog transition test cleanup should succeed");

  uint32_t normal_pin9_irq_count = 0u;
  uint32_t fgpio_pin9_irq_count = 0u;
  HPMGPIO normal_pin9(HPM_GPIO0, shared_port, 9u, shared_irq);
  normal_pin9.RegisterCallback(
      GPIO::Callback::Create(CountInterrupt, &normal_pin9_irq_count));
  Expect(normal_pin9.EnableInterrupt() == ErrorCode::OK,
         "normal pin9 should be enabled before checking FGPIO map isolation");
  const auto fgpio_enable_calls_before = LibXRHpmTest::g_interrupt_state.enable_calls[shared_irq];
  const auto fgpio_disable_calls_before =
      LibXRHpmTest::g_interrupt_state.disable_calls[shared_irq];
  HPMGPIO fgpio_pin(HPM_FGPIO, shared_port, 9u, shared_irq);
  fgpio_pin.RegisterCallback(GPIO::Callback::Create(CountInterrupt, &fgpio_pin9_irq_count));
  Expect(fgpio_pin.SetConfig({GPIO::Direction::FALL_INTERRUPT, GPIO::Pull::NONE}) ==
             ErrorCode::NOT_SUPPORT,
         "FGPIO interrupt configuration should be rejected");
  Expect(fgpio_pin.EnableInterrupt() == ErrorCode::NOT_SUPPORT,
         "FGPIO interrupt enable should be rejected");
  Expect(fgpio_pin.DisableInterrupt() == ErrorCode::NOT_SUPPORT,
         "FGPIO interrupt disable should be rejected");
  Expect(LibXRHpmTest::g_interrupt_state.enable_calls[shared_irq] ==
             fgpio_enable_calls_before,
         "FGPIO interrupt enable must not touch PLIC routing");
  Expect(LibXRHpmTest::g_interrupt_state.disable_calls[shared_irq] ==
             fgpio_disable_calls_before,
         "FGPIO interrupt disable must not touch PLIC routing");
  Expect(LibXRHpmTest::g_fgpio.IE[shared_port].VALUE == 0u,
         "FGPIO interrupt enable bit must remain clear when rejected");
  LibXRHpmTest::g_gpio0.IF[shared_port].VALUE = (1u << 9u);
  HPMGPIO::CheckInterrupt(shared_port);
  Expect(normal_pin9_irq_count == 1u,
         "FGPIO construction must not overwrite normal GPIO interrupt dispatch map");
  Expect(fgpio_pin9_irq_count == 0u, "FGPIO callback must not be dispatched by GPIO0 IRQ");
  Expect(normal_pin9.DisableInterrupt() == ErrorCode::OK,
         "normal pin9 cleanup should succeed");

  std::cout << "test_hpm_gpio_irq_shared passed\n";
  return 0;
}
