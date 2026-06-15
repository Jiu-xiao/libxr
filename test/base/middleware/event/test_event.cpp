/**
 * @file test_event.cpp
 * @brief `Event` 注册、绑定与回调上下文透传测试。 `Event` registration, binding and callback-context propagation tests.
 *
 * 测试项目 / Test items:
 * 1. 普通 `Active()` 分发。 Direct activation: verify normal `Active()` dispatch reaches the registered callback in non-ISR context.
 * 2. `ActiveFromCallback()` 的 ISR 标志透传。 Callback-safe activation: verify `ActiveFromCallback()` preserves the explicit or legacy-default ISR flag.
 * 3. 事件绑定后的上下文保持。 Bound-event forwarding: verify event binding preserves the source callback context when one event triggers another.
 * 4. 高值 event id 分发。 High-value event IDs: verify large event IDs still compare and dispatch correctly.
 *
 * 测试原理 / Test principles:
 * 1. 直接检查回调观察到的 `in_isr`，因为这是这个模块的高风险语义。 Check the callback's observed `in_isr` flag directly, because context propagation is the high-risk semantic in this subsystem.
 * 2. 同时驱动直接分发和绑定转发路径，覆盖事件树和回调链两层行为。 Exercise both direct lookup and bound forwarding paths so the event tree and callback chain are both covered.
 */
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

/**
 * @brief 测试入口函数 `test_event`。 Test entry function `test_event`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_event()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  static int event_arg = 0;
  static bool last_in_isr = false;
  static int high_event_arg = 0;

  auto event_cb = LibXR::Event::Callback::Create(
      [](bool in_isr, int* arg, uint32_t event)
      {
        last_in_isr = in_isr;
        *arg = *arg + 1;
        ASSERT(event == 0x1234);
      },
      &event_arg);

  auto high_event_cb = LibXR::Event::Callback::Create(
      [](bool in_isr, int* arg, uint32_t event)
      {
        last_in_isr = in_isr;
        *arg = *arg + 1;
        ASSERT(event == 0xF0001234);
      },
      &high_event_arg);

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

  // High-value event IDs must still compare and dispatch correctly.
  event.Register(0xF0001234, high_event_cb);
  event.Active(0xF0001234);
  ASSERT(high_event_arg == 1);
  ASSERT(last_in_isr == false);
}
