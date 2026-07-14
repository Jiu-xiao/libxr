#include <assert.h>
#include <pthread.h>

#include <atomic>
#include <cstdint>

#ifndef BROKEN_MEMORY_ORDER
#define BROKEN_MEMORY_ORDER 0
#endif

namespace
{

std::atomic<uint32_t> payload{0U};
std::atomic<uint32_t> published{0U};

void* Publish(void*)
{
  payload.store(42U, std::memory_order_relaxed);
#if BROKEN_MEMORY_ORDER
  published.store(1U, std::memory_order_relaxed);
#else
  published.store(1U, std::memory_order_release);
#endif
  return nullptr;
}

void* Observe(void*)
{
#if BROKEN_MEMORY_ORDER
  const uint32_t ready = published.load(std::memory_order_relaxed);
#else
  const uint32_t ready = published.load(std::memory_order_acquire);
#endif
  if (ready != 0U)
  {
    assert(payload.load(std::memory_order_relaxed) == 42U);
  }
  return nullptr;
}

}  // namespace

int main()
{
  pthread_t publisher{};
  pthread_t observer{};
  assert(pthread_create(&publisher, nullptr, Publish, nullptr) == 0);
  assert(pthread_create(&observer, nullptr, Observe, nullptr) == 0);
  assert(pthread_join(publisher, nullptr) == 0);
  assert(pthread_join(observer, nullptr) == 0);
  return 0;
}
