#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_event()
{
  static int event_arg = 0;
  static bool last_in_isr = false;

  auto event_cb = LibXR::Event::Callback::Create(
      [](bool in_isr, int* arg, uint32_t event)
      {
        last_in_isr = in_isr;
        *arg = *arg + 1;
        ASSERT(event == 0x1234);
      },
      &event_arg);

  LibXR::Event event, event_bind;

  // Direct activation must report non-ISR context.
  event.Register(0x1234, event_cb);
  event.Active(0x1234);
  ASSERT(event_arg == 1);
  ASSERT(last_in_isr == false);

  for (int i = 0; i <= 0x1234; i++)
  {
    event.Active(i);
  }
  ASSERT(event_arg == 2);
  ASSERT(last_in_isr == false);

  // Callback-safe activation must preserve the explicit in_isr flag.
  event.ActiveFromCallback(event.GetList(0x1234), 0x1234, false);
  ASSERT(event_arg == 3);
  ASSERT(last_in_isr == false);

  event.ActiveFromCallback(event.GetList(0x1234), 0x1234, true);
  ASSERT(event_arg == 4);
  ASSERT(last_in_isr == true);

  // Default callback-safe behavior remains ISR=true for legacy callers.
  event.ActiveFromCallback(event.GetList(0x1234), 0x1234);
  ASSERT(event_arg == 5);
  ASSERT(last_in_isr == true);

  // Bound events must keep the source callback context unchanged.
  event.Bind(event_bind, 0x4321, 0x1234);
  event_bind.Active(0x4321);
  ASSERT(event_arg == 6);
  ASSERT(last_in_isr == false);

  event_bind.ActiveFromCallback(event_bind.GetList(0x4321), 0x4321, false);
  ASSERT(event_arg == 7);
  ASSERT(last_in_isr == false);

  event_bind.ActiveFromCallback(event_bind.GetList(0x4321), 0x4321, true);
  ASSERT(event_arg == 8);
  ASSERT(last_in_isr == true);
}
