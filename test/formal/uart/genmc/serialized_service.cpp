#include "serialized_service.hpp"

#include <assert.h>
#include <pthread.h>

#include <atomic>
#include <cstdint>

namespace
{

constexpr uint32_t REENTRANT_EVENT = 1U << 3U;
constexpr uint32_t PUBLISHER_COUNT = 3U;
constexpr uint32_t EVENT_COUNT = PUBLISHER_COUNT + 1U;

LibXR::SerializedService service;
std::atomic<uint32_t> handled{0U};
std::atomic<uint32_t> handler_depth{0U};
std::atomic<uint32_t> reentrant_published{0U};
std::atomic<uint32_t> handle_counts[EVENT_COUNT]{};
uint32_t publisher_events[PUBLISHER_COUNT] = {1U << 0U, 1U << 1U, 1U << 2U};
uint32_t publisher_indices[PUBLISHER_COUNT] = {0U, 1U, 2U};
uint32_t publisher_payloads[PUBLISHER_COUNT]{};

constexpr uint32_t ExpectedPayload(uint32_t index) { return 0xA5A50000U | index; }

void Handle(uint32_t events)
{
  const uint32_t previous_depth = handler_depth.fetch_add(1U, std::memory_order_acq_rel);
  assert(previous_depth == 0U);

  handled.fetch_or(events, std::memory_order_release);
  for (uint32_t index = 0U; index < PUBLISHER_COUNT; ++index)
  {
    if ((events & publisher_events[index]) != 0U)
    {
      assert(publisher_payloads[index] == ExpectedPayload(index));
      handle_counts[index].fetch_add(1U, std::memory_order_relaxed);
    }
  }
  if ((events & REENTRANT_EVENT) != 0U)
  {
    handle_counts[PUBLISHER_COUNT].fetch_add(1U, std::memory_order_relaxed);
  }
  if (((events & publisher_events[0]) != 0U) &&
      (reentrant_published.exchange(1U, std::memory_order_acq_rel) == 0U))
  {
    (void)service.Invoke(REENTRANT_EVENT,
                         [](uint32_t nested_events) { Handle(nested_events); });
  }

  const uint32_t previous = handler_depth.fetch_sub(1U, std::memory_order_acq_rel);
  assert(previous == 1U);
}

void* Publish(void* argument)
{
  const uint32_t index = *static_cast<uint32_t*>(argument);
  publisher_payloads[index] = ExpectedPayload(index);
  (void)service.Invoke(publisher_events[index], [](uint32_t events) { Handle(events); });
  return nullptr;
}

}  // namespace

int main()
{
  pthread_t publishers[PUBLISHER_COUNT]{};
  for (uint32_t index = 0U; index < PUBLISHER_COUNT; ++index)
  {
    assert(pthread_create(&publishers[index], nullptr, Publish,
                          &publisher_indices[index]) == 0);
  }
  for (uint32_t index = 0U; index < PUBLISHER_COUNT; ++index)
  {
    assert(pthread_join(publishers[index], nullptr) == 0);
  }

  constexpr uint32_t expected = (1U << 0U) | (1U << 1U) | (1U << 2U) | REENTRANT_EVENT;
  assert(handled.load(std::memory_order_acquire) == expected);
  assert(handler_depth.load(std::memory_order_acquire) == 0U);
  for (uint32_t index = 0U; index < EVENT_COUNT; ++index)
  {
    assert(handle_counts[index].load(std::memory_order_relaxed) == 1U);
  }
  return 0;
}
