#include "serialized_service.hpp"

#include <assert.h>
#include <pthread.h>

#include <atomic>
#include <cstdint>

namespace
{

constexpr uint32_t REENTRANT_EVENT = 1U << 3U;

LibXR::SerializedService service;
std::atomic<uint32_t> handled{0U};
std::atomic<uint32_t> handler_depth{0U};
std::atomic<uint32_t> reentrant_published{0U};
uint32_t publisher_events[] = {1U << 0U, 1U << 1U, 1U << 2U};

void Handle(uint32_t events)
{
  const uint32_t previous_depth = handler_depth.fetch_add(1U, std::memory_order_acq_rel);
  assert(previous_depth == 0U);

  handled.fetch_or(events, std::memory_order_release);
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
  const uint32_t event = *static_cast<uint32_t*>(argument);
  (void)service.Invoke(event, [](uint32_t events) { Handle(events); });
  return nullptr;
}

}  // namespace

int main()
{
  pthread_t publishers[3]{};
  for (uint32_t index = 0U; index < 3U; ++index)
  {
    assert(pthread_create(&publishers[index], nullptr, Publish,
                          &publisher_events[index]) == 0);
  }
  for (uint32_t index = 0U; index < 3U; ++index)
  {
    assert(pthread_join(publishers[index], nullptr) == 0);
  }

  constexpr uint32_t expected = (1U << 0U) | (1U << 1U) | (1U << 2U) | REENTRANT_EVENT;
  assert(handled.load(std::memory_order_acquire) == expected);
  assert(handler_depth.load(std::memory_order_acquire) == 0U);
  return 0;
}
