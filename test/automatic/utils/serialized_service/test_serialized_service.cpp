/**
 * @file test_serialized_service.cpp
 * @brief 检查事件合并、递归请求和并发调用的串行处理。 /
 * Tests event coalescing, recursive requests and serialized concurrent calls.
 *
 * 检查竞争调用的临时回调不会被保留，以及事件发布后的数据可见性。
 * Checks transient callable lifetime and payload visibility after event publication.
 */

#include <array>
#include <atomic>
#include <cstdint>
#include <semaphore>
#include <thread>
#include <type_traits>

#include "serialized_service.hpp"
#include "test.hpp"
#include "test_assert.hpp"

namespace
{
constexpr uint32_t FIRST = 1U;
constexpr uint32_t SECOND = 2U;
constexpr uint32_t THIRD = 4U;

static_assert(sizeof(LibXR::SerializedService) == sizeof(std::atomic<uint32_t>));
static_assert(!std::is_copy_constructible_v<LibXR::SerializedService>);

void TestDeferredAndRecursiveEvents()
{
  // 先发布的事件由下一次 Invoke 处理；回调中再次请求执行也不能递归进入回调。
  // The next Invoke handles queued events; invoking again inside the callback must not
  // recurse.
  LibXR::SerializedService service;
  service.Publish(0U);
  service.Publish(SECOND);
  service.Publish(SECOND);
  unsigned calls = 0U;
  unsigned depth = 0U;
  TEST_ASSERT(service.Invoke(FIRST, true,
                             [&](uint32_t events, bool in_isr)
                             {
                               TEST_ASSERT(++depth == 1U);
                               TEST_ASSERT(in_isr);
                               if (calls++ == 0U)
                               {
                                 TEST_ASSERT(events == (FIRST | SECOND));
                                 TEST_ASSERT(!service.Invoke(THIRD, false,
                                                             [](uint32_t, bool)
                                                             { TEST_ASSERT(false); }));
                                 service.Publish(THIRD);
                               }
                               else
                               {
                                 TEST_ASSERT(events == THIRD);
                               }
                               --depth;
                             }));
  TEST_ASSERT(calls == 2U && depth == 0U);
  TEST_ASSERT(service.Invoke(FIRST, false, [](uint32_t events, bool in_isr)
                             { TEST_ASSERT(events == FIRST && !in_isr); }));
}

void TestCompetingCallableLifetime()
{
  // 暂停正在执行的回调，另一线程提交事件后销毁临时捕获对象；后续仍由原回调处理。
  // Pause the active callback, submit from a temporary callable, then let the original
  // callback handle it.
  LibXR::SerializedService service;
  std::binary_semaphore entered(0);
  std::binary_semaphore resume(0);
  unsigned owner_calls = 0U;
  int payload = 0;
  std::thread owner(
      [&]
      {
        TEST_ASSERT(service.Invoke(FIRST, false,
                                   [&](uint32_t events, bool in_isr)
                                   {
                                     TEST_ASSERT(events != 0U);
                                     TEST_ASSERT(!in_isr);
                                     if (owner_calls++ == 0U)
                                     {
                                       TEST_ASSERT(events == FIRST);
                                       entered.release();
                                       resume.acquire();
                                     }
                                     else
                                     {
                                       TEST_ASSERT(events == (SECOND | THIRD));
                                       TEST_ASSERT(payload == 42);
                                     }
                                   }));
      });
  // 确认第一个回调已停住，再提交竞争调用，使临时对象的销毁早于后续处理。
  // Wait until the first callback is paused, then submit so the temporary dies before
  // later handling.
  entered.acquire();
  payload = 42;
  {
    int transient_capture = 7;
    TEST_ASSERT(
        !service.Invoke(SECOND, true, [&](uint32_t, bool) { transient_capture = 8; }));
    TEST_ASSERT(transient_capture == 7);
  }
  service.Publish(THIRD);
  resume.release();
  owner.join();
  TEST_ASSERT(owner_calls == 2U);
}

void TestConcurrentReleaseAndPayloadPublication()
{
  // 每个生产者等上一条数据被处理后再写下一条；处理回调之间不能重叠。
  // Each producer waits for its prior value to be handled; handler executions must not
  // overlap.
  constexpr unsigned PRODUCERS = 4U;
  constexpr uint32_t ITERATIONS = 5000U;
  LibXR::SerializedService service;
  std::array<uint32_t, PRODUCERS> payload{};
  std::array<uint32_t, PRODUCERS> consumed{};
  std::array<std::binary_semaphore, PRODUCERS> acknowledged{
      std::binary_semaphore{0}, std::binary_semaphore{0}, std::binary_semaphore{0},
      std::binary_semaphore{0}};
  std::array<std::thread, PRODUCERS> threads;
  std::atomic<unsigned> active{0U};
  auto handler = [&](uint32_t events, bool)
  {
    TEST_ASSERT(events != 0U);
    TEST_ASSERT(active.fetch_add(1U, std::memory_order_relaxed) == 0U);
    for (unsigned i = 0U; i < PRODUCERS; ++i)
    {
      if ((events & (1U << i)) != 0U)
      {
        // 普通数据通过 service 事件发布；这里没有额外的锁替它保证可见性。
        // The service event publishes this plain payload; no extra lock supplies
        // visibility.
        TEST_ASSERT(payload[i] == consumed[i] + 1U);
        consumed[i] = payload[i];
        acknowledged[i].release();
      }
    }
    TEST_ASSERT(active.fetch_sub(1U, std::memory_order_relaxed) == 1U);
  };
  for (unsigned i = 0U; i < PRODUCERS; ++i)
  {
    threads[i] = std::thread(
        [&, i]
        {
          for (uint32_t sequence = 1U; sequence <= ITERATIONS; ++sequence)
          {
            payload[i] = sequence;
            service.Invoke(1U << i, false, handler);
            acknowledged[i].acquire();
          }
        });
  }
  for (auto& thread : threads)
  {
    thread.join();
  }
  for (uint32_t count : consumed)
  {
    TEST_ASSERT(count == ITERATIONS);
  }
  TEST_ASSERT(active.load(std::memory_order_relaxed) == 0U);
}
}  // namespace

void test_serialized_service()
{
  TestDeferredAndRecursiveEvents();
  TestCompetingCallableLifetime();
  TestConcurrentReleaseAndPayloadPublication();
}
