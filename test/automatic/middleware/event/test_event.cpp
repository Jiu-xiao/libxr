/**
 * @file test_event.cpp
 * @brief Event 分发与绑定测试 / Event dispatch and binding tests.
 *
 * 检查事件编号匹配、绑定转发，以及普通调用和回调调用传递的 ISR 标记。
 * Check event ID matching, bound-event forwarding and ISR flags from direct and callback
 * calls.
 */

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"
#include "test_assert.hpp"

void test_event()
{
  static int event_arg = 0;
  static bool last_in_isr = false;
  static int high_event_arg = 0;

  auto event_cb = LibXR::Event::Callback::Create(
      [](bool in_isr, int* arg, uint32_t event)
      {
        last_in_isr = in_isr;
        *arg = *arg + 1;
        TEST_ASSERT(event == 0x1234);
      },
      &event_arg);

  auto high_event_cb = LibXR::Event::Callback::Create(
      [](bool in_isr, int* arg, uint32_t event)
      {
        last_in_isr = in_isr;
        *arg = *arg + 1;
        TEST_ASSERT(event == 0xF0001234);
      },
      &high_event_arg);

  LibXR::Event event, event_bind;

  // Direct activation must report non-ISR context.
  event.Register(0x1234, event_cb);
  event.Active(0x1234);
  TEST_ASSERT(event_arg == 1);
  TEST_ASSERT(last_in_isr == false);

  for (int i = 0; i <= 0x1234; i++)
  {
    event.Active(i);
  }
  TEST_ASSERT(event_arg == 2);
  TEST_ASSERT(last_in_isr == false);

  // Callback-safe activation must preserve the explicit in_isr flag.
  event.ActiveFromCallback(event.GetList(0x1234), 0x1234, false);
  TEST_ASSERT(event_arg == 3);
  TEST_ASSERT(last_in_isr == false);

  event.ActiveFromCallback(event.GetList(0x1234), 0x1234, true);
  TEST_ASSERT(event_arg == 4);
  TEST_ASSERT(last_in_isr == true);

  // Default callback-safe behavior remains ISR=true for legacy callers.
  event.ActiveFromCallback(event.GetList(0x1234), 0x1234);
  TEST_ASSERT(event_arg == 5);
  TEST_ASSERT(last_in_isr == true);

  // Bound events must keep the source callback context unchanged.
  event.Bind(event_bind, 0x4321, 0x1234);
  event_bind.Active(0x4321);
  TEST_ASSERT(event_arg == 6);
  TEST_ASSERT(last_in_isr == false);

  event_bind.ActiveFromCallback(event_bind.GetList(0x4321), 0x4321, false);
  TEST_ASSERT(event_arg == 7);
  TEST_ASSERT(last_in_isr == false);

  event_bind.ActiveFromCallback(event_bind.GetList(0x4321), 0x4321, true);
  TEST_ASSERT(event_arg == 8);
  TEST_ASSERT(last_in_isr == true);

  // High-value event IDs must still compare and dispatch correctly.
  event.Register(0xF0001234, high_event_cb);
  event.Active(0xF0001234);
  TEST_ASSERT(high_event_arg == 1);
  TEST_ASSERT(last_in_isr == false);
}
