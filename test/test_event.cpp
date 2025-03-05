#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_event() {
  static int event_arg = 0;

  auto event_cb = LibXR::Callback<uint32_t>::Create(
      [](bool in_isr, int *arg, uint32_t event) {
        UNUSED(in_isr);
        *arg = *arg + 1;
        ASSERT(event == 0x1234);
      },
      &event_arg);

  LibXR::Event event, event_bind;

  event.Register(0x1234, event_cb);
  event.Active(0x1234);
  ASSERT(event_arg == 1);
  for (int i = 0; i <= 0x1234; i++) {
    event.Active(i);
  }
  ASSERT(event_arg == 2);
  event.Bind(event_bind, 0x4321, 0x1234);
  event_bind.Active(0x4321);
  ASSERT(event_arg == 3);
}