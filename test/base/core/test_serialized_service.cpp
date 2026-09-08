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
        // The producer publishes this plain payload only through the service event.
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
