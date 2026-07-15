/**
 * @file test_serialized_service.cpp
 * @brief SerializedService event, ownership, and memory-order boundaries.
 */
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include "libxr.hpp"
#include "test.hpp"

namespace
{

constexpr uint32_t EVENT_WRITE = 1U << 0U;
constexpr uint32_t EVENT_COMPLETE = 1U << 1U;
constexpr uint32_t EVENT_ERROR = 1U << 2U;
constexpr uint32_t EVENT_KICK = 1U << 3U;
constexpr uint32_t EVENT_HIGHEST = 1U << 30U;
constexpr uint32_t OWNER_RESERVED = 1U << 31U;
constexpr uint32_t WAIT_TIMEOUT_MS = 1000U;

template <typename Predicate>
bool WaitUntil(Predicate&& predicate)
{
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(WAIT_TIMEOUT_MS);
  while (!predicate())
  {
    if (std::chrono::steady_clock::now() >= deadline)
    {
      return false;
    }
    std::this_thread::yield();
  }
  return true;
}

struct ReentrantHandler
{
  LibXR::SerializedService& service;
  uint32_t calls = 0U;
  uint32_t depth = 0U;
  uint32_t max_depth = 0U;
  uint32_t observed = 0U;

  void operator()(uint32_t events) noexcept
  {
    ASSERT(events != 0U);
    ++calls;
    ++depth;
    max_depth = depth > max_depth ? depth : max_depth;
    observed |= events;
    if ((events & EVENT_WRITE) != 0U)
    {
      ASSERT(!service.Invoke(EVENT_COMPLETE, *this));
    }
    --depth;
  }
};

void TestBasicReentryAndZeroMask()
{
  LibXR::SerializedService service;
  ReentrantHandler handler{service};

  ASSERT(service.Invoke(EVENT_WRITE, handler));
  ASSERT(handler.calls == 2U);
  ASSERT(handler.max_depth == 1U);
  ASSERT(handler.observed == (EVENT_WRITE | EVENT_COMPLETE));
  ASSERT(!service.Invoke(0U, handler));
  ASSERT(!service.Invoke(OWNER_RESERVED, handler));
  ASSERT(!service.Invoke(OWNER_RESERVED | EVENT_WRITE, handler));
  ASSERT(handler.calls == 2U);
}

void TestHighestEventBit()
{
  LibXR::SerializedService service;
  uint32_t observed = 0U;
  ASSERT(service.Invoke(EVENT_HIGHEST,
                        [&](uint32_t events) noexcept { observed = events; }));
  ASSERT(observed == EVENT_HIGHEST);
}

void TestEveryEventCombinationIsPreserved()
{
  constexpr uint32_t all_events = EVENT_WRITE | EVENT_COMPLETE | EVENT_ERROR;
  for (uint32_t events = 1U; events <= all_events; ++events)
  {
    LibXR::SerializedService service;
    uint32_t calls = 0U;
    uint32_t observed = 0U;
    ASSERT(service.Invoke(events,
                          [&](uint32_t snapshot) noexcept
                          {
                            ASSERT(snapshot != 0U);
                            ++calls;
                            observed = snapshot;
                          }));
    ASSERT(calls == 1U);
    ASSERT(observed == events);
  }
}

void TestCoalescingAndLoserHandlerIsolation()
{
  LibXR::SerializedService service;
  std::atomic<uint32_t> entered{0U};
  std::atomic<uint32_t> release{0U};
  std::atomic<uint32_t> done{0U};
  std::atomic<uint32_t> owner_calls{0U};
  std::atomic<uint32_t> loser_calls{0U};
  std::atomic<uint32_t> second_snapshot{0U};

  std::thread owner(
      [&]
      {
        ASSERT(service.Invoke(
            EVENT_KICK,
            [&](uint32_t events) noexcept
            {
              ASSERT(events != 0U);
              const uint32_t call = owner_calls.fetch_add(1U, std::memory_order_acq_rel);
              if (call == 0U)
              {
                entered.store(1U, std::memory_order_release);
                ASSERT(WaitUntil(
                    [&] { return release.load(std::memory_order_acquire) != 0U; }));
              }
              else
              {
                second_snapshot.store(events, std::memory_order_release);
              }
            }));
        done.store(1U, std::memory_order_release);
      });

  ASSERT(WaitUntil([&] { return entered.load(std::memory_order_acquire) != 0U; }));
  for (uint32_t repeat = 0U; repeat < 16U; ++repeat)
  {
    for (uint32_t events = 1U; events <= 7U; ++events)
    {
      ASSERT(!service.Invoke(events, [&](uint32_t) noexcept
                             { loser_calls.fetch_add(1U, std::memory_order_relaxed); }));
    }
  }
  release.store(1U, std::memory_order_release);
  ASSERT(WaitUntil([&] { return done.load(std::memory_order_acquire) != 0U; }));
  owner.join();

  ASSERT(owner_calls.load(std::memory_order_acquire) == 2U);
  ASSERT(loser_calls.load(std::memory_order_acquire) == 0U);
  ASSERT(second_snapshot.load(std::memory_order_acquire) == 7U);
}

void TestPublicationVisibilityThroughPendingEvent()
{
  LibXR::SerializedService service;
  std::atomic<uint32_t> entered{0U};
  std::atomic<uint32_t> release{0U};
  std::atomic<uint32_t> done{0U};
  std::atomic<uint32_t> observed{0U};
  uint32_t published_value = 0U;

  std::thread owner(
      [&]
      {
        ASSERT(service.Invoke(
            EVENT_KICK,
            [&](uint32_t events) noexcept
            {
              ASSERT(events != 0U);
              if ((events & EVENT_KICK) != 0U)
              {
                entered.store(1U, std::memory_order_release);
                ASSERT(WaitUntil(
                    [&] { return release.load(std::memory_order_acquire) != 0U; }));
              }
              if ((events & EVENT_WRITE) != 0U)
              {
                observed.store(published_value, std::memory_order_release);
              }
            }));
        done.store(1U, std::memory_order_release);
      });

  ASSERT(WaitUntil([&] { return entered.load(std::memory_order_acquire) != 0U; }));
  published_value = 0xA5A55A5AU;
  ASSERT(!service.Invoke(EVENT_WRITE, [](uint32_t) noexcept { ASSERT(false); }));
  release.store(1U, std::memory_order_release);
  ASSERT(WaitUntil([&] { return done.load(std::memory_order_acquire) != 0U; }));
  owner.join();
  ASSERT(observed.load(std::memory_order_acquire) == 0xA5A55A5AU);
}

void TestConcurrentReleaseReacquireStress()
{
  LibXR::SerializedService service;
  std::atomic<uint32_t> ready{0U};
  std::atomic<uint32_t> start{0U};
  std::atomic<uint32_t> requested{0U};
  std::atomic<uint32_t> processed{0U};
  std::atomic<uint32_t> active_handlers{0U};
  std::atomic<uint32_t> failures{0U};

  auto handler = [&](uint32_t events) noexcept
  {
    if (events == 0U)
    {
      failures.fetch_add(1U, std::memory_order_relaxed);
    }
    if (active_handlers.fetch_add(1U, std::memory_order_acq_rel) != 0U)
    {
      failures.fetch_add(1U, std::memory_order_relaxed);
    }
    processed.store(requested.load(std::memory_order_acquire), std::memory_order_release);
    active_handlers.fetch_sub(1U, std::memory_order_acq_rel);
  };

  constexpr uint32_t thread_count = 8U;
  constexpr uint32_t invocations_per_thread = 2000U;
  std::vector<std::thread> threads;
  threads.reserve(thread_count);
  for (uint32_t thread = 0U; thread < thread_count; ++thread)
  {
    threads.emplace_back(
        [&, thread]
        {
          ready.fetch_add(1U, std::memory_order_release);
          if (!WaitUntil([&] { return start.load(std::memory_order_acquire) != 0U; }))
          {
            failures.fetch_add(1U, std::memory_order_relaxed);
            return;
          }
          for (uint32_t i = 0U; i < invocations_per_thread; ++i)
          {
            requested.fetch_add(1U, std::memory_order_release);
            (void)service.Invoke(1U << ((i + thread) % 3U), handler);
          }
        });
  }

  ASSERT(
      WaitUntil([&] { return ready.load(std::memory_order_acquire) == thread_count; }));
  start.store(1U, std::memory_order_release);
  for (auto& thread : threads)
  {
    thread.join();
  }
  (void)service.Invoke(EVENT_KICK, handler);

  ASSERT(failures.load(std::memory_order_acquire) == 0U);
  ASSERT(active_handlers.load(std::memory_order_acquire) == 0U);
  ASSERT(processed.load(std::memory_order_acquire) ==
         requested.load(std::memory_order_acquire));
}

}  // namespace

void test_serialized_service()
{
  TestBasicReentryAndZeroMask();
  TestHighestEventBit();
  TestEveryEventCombinationIsPreserved();
  TestCoalescingAndLoserHandlerIsolation();
  TestPublicationVisibilityThroughPendingEvent();
  TestConcurrentReleaseReacquireStress();
}
