#include <array>
#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <pthread.h>
#include <time.h>

#include "linux_timebase.hpp"
#include "libxr.hpp"

extern struct timespec libxr_linux_start_time_spec;

namespace
{

uint64_t NowMonotonicNs()
{
  timespec ts = {};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
         static_cast<uint64_t>(ts.tv_nsec);
}

void InitMinimalLinuxTimebase()
{
  static bool initialized = false;
  static LibXR::LinuxTimebase timebase;
  if (initialized)
  {
    return;
  }

  clock_gettime(CLOCK_MONOTONIC, &libxr_linux_start_time_spec);
  initialized = true;
  UNUSED(timebase);
}

void JoinThreadIfNeeded(LibXR::Thread& thread)
{
#if defined(LIBXR_SYSTEM_Linux) || defined(LIBXR_SYSTEM_Webots)
  pthread_join(thread, nullptr);
#else
  UNUSED(thread);
#endif
}

struct MutexWorkerContext
{
  LibXR::Mutex* mutex = nullptr;
  std::atomic<uint64_t>* counter = nullptr;
  LibXR::Semaphore* done = nullptr;
  uint64_t iterations = 0;
  int failure = 0;
};

void MutexWorker(MutexWorkerContext* ctx)
{
  for (uint64_t i = 0; i < ctx->iterations; ++i)
  {
    if (ctx->mutex->Lock() != ErrorCode::OK)
    {
      ctx->failure = 1;
      break;
    }

    ctx->counter->fetch_add(1, std::memory_order_relaxed);
    ctx->mutex->Unlock();
  }

  ctx->done->Post();
}

struct SemaphoreProducerContext
{
  LibXR::Semaphore* sem = nullptr;
  LibXR::Semaphore* done = nullptr;
  uint64_t iterations = 0;
};

void SemaphoreProducer(SemaphoreProducerContext* ctx)
{
  for (uint64_t i = 0; i < ctx->iterations; ++i)
  {
    ctx->sem->Post();
  }

  ctx->done->Post();
}

struct SemaphoreConsumerContext
{
  LibXR::Semaphore* sem = nullptr;
  LibXR::Semaphore* done = nullptr;
  uint64_t iterations = 0;
  int failure = 0;
};

void SemaphoreConsumer(SemaphoreConsumerContext* ctx)
{
  for (uint64_t i = 0; i < ctx->iterations; ++i)
  {
    if (ctx->sem->Wait(10000) != ErrorCode::OK)
    {
      ctx->failure = 1;
      break;
    }
  }

  ctx->done->Post();
}

int RunMutexStress()
{
  constexpr size_t kThreadNum = 4;
  constexpr uint64_t kIterationsPerThread = 200000;
  constexpr size_t kThreadStackBytes = 8192;

  LibXR::Mutex mutex;
  std::atomic<uint64_t> counter(0);
  LibXR::Semaphore done(0);
  std::array<LibXR::Thread, kThreadNum> threads = {};
  std::array<MutexWorkerContext, kThreadNum> ctx = {};

  for (size_t i = 0; i < kThreadNum; ++i)
  {
    ctx[i].mutex = &mutex;
    ctx[i].counter = &counter;
    ctx[i].done = &done;
    ctx[i].iterations = kIterationsPerThread;
    threads[i].Create<MutexWorkerContext*>(&ctx[i], MutexWorker, "mtx_stress", kThreadStackBytes,
                                           LibXR::Thread::Priority::MEDIUM);
  }

  const uint64_t start_ns = NowMonotonicNs();
  for (size_t i = 0; i < kThreadNum; ++i)
  {
    if (done.Wait(10000) != ErrorCode::OK)
    {
      return 1;
    }
  }
  const uint64_t end_ns = NowMonotonicNs();

  for (auto& thread : threads)
  {
    JoinThreadIfNeeded(thread);
  }

  for (const auto& item : ctx)
  {
    if (item.failure != 0)
    {
      return 2;
    }
  }

  const uint64_t expected = kThreadNum * kIterationsPerThread;
  if (counter.load(std::memory_order_relaxed) != expected)
  {
    return 3;
  }

  std::printf("[BENCH] sync_mutex threads=%zu iterations=%" PRIu64
              " counter=%" PRIu64 " wall_ms=%.3f\n",
              kThreadNum, kIterationsPerThread, counter.load(std::memory_order_relaxed),
              static_cast<double>(end_ns - start_ns) / 1000000.0);
  return 0;
}

int RunSemaphoreStress()
{
  constexpr size_t kProducerNum = 4;
  constexpr size_t kConsumerNum = 1;
  constexpr uint64_t kIterationsPerProducer = 50000;
  constexpr uint64_t kTotalPosts = kProducerNum * kIterationsPerProducer;
  constexpr size_t kThreadStackBytes = 8192;

  LibXR::Semaphore sem(0);
  LibXR::Semaphore producer_done(0);
  LibXR::Semaphore consumer_done(0);
  std::array<LibXR::Thread, kProducerNum> producers = {};
  std::array<LibXR::Thread, kConsumerNum> consumers = {};
  std::array<SemaphoreProducerContext, kProducerNum> producer_ctx = {};
  std::array<SemaphoreConsumerContext, kConsumerNum> consumer_ctx = {};

  for (size_t i = 0; i < kConsumerNum; ++i)
  {
    consumer_ctx[i].sem = &sem;
    consumer_ctx[i].done = &consumer_done;
    consumer_ctx[i].iterations = kTotalPosts / kConsumerNum;
    consumers[i].Create<SemaphoreConsumerContext*>(&consumer_ctx[i], SemaphoreConsumer,
                                                  "sem_cons", kThreadStackBytes,
                                                  LibXR::Thread::Priority::MEDIUM);
  }

  for (size_t i = 0; i < kProducerNum; ++i)
  {
    producer_ctx[i].sem = &sem;
    producer_ctx[i].done = &producer_done;
    producer_ctx[i].iterations = kIterationsPerProducer;
    producers[i].Create<SemaphoreProducerContext*>(&producer_ctx[i], SemaphoreProducer,
                                                  "sem_prod", kThreadStackBytes,
                                                  LibXR::Thread::Priority::MEDIUM);
  }

  const uint64_t start_ns = NowMonotonicNs();
  for (size_t i = 0; i < kProducerNum; ++i)
  {
    if (producer_done.Wait(10000) != ErrorCode::OK)
    {
      return 10;
    }
  }

  for (size_t i = 0; i < kConsumerNum; ++i)
  {
    if (consumer_done.Wait(10000) != ErrorCode::OK)
    {
      return 11;
    }
  }
  const uint64_t end_ns = NowMonotonicNs();

  for (auto& thread : producers)
  {
    JoinThreadIfNeeded(thread);
  }
  for (auto& thread : consumers)
  {
    JoinThreadIfNeeded(thread);
  }

  for (const auto& item : consumer_ctx)
  {
    if (item.failure != 0)
    {
      return 12;
    }
  }

  std::printf("[BENCH] sync_semaphore producers=%zu consumers=%zu posts=%" PRIu64
              " consumed=%" PRIu64 " wall_ms=%.3f\n",
              kProducerNum, kConsumerNum, kTotalPosts, kTotalPosts,
              static_cast<double>(end_ns - start_ns) / 1000000.0);
  return 0;
}

int RunThreadSleepStress()
{
  constexpr uint32_t kStepMs = 2;
  constexpr uint32_t kIterations = 200;

  const uint64_t start_ns = NowMonotonicNs();
  LibXR::MillisecondTimestamp wake = LibXR::Thread::GetTime();
  uint32_t last_tick = wake;

  for (uint32_t i = 0; i < kIterations; ++i)
  {
    LibXR::Thread::SleepUntil(wake, kStepMs);
    const uint32_t now_tick = LibXR::Thread::GetTime();
    if (now_tick < last_tick)
    {
      return 20;
    }
    last_tick = now_tick;
  }

  const uint64_t end_ns = NowMonotonicNs();
  const uint64_t elapsed_ms = (end_ns - start_ns) / 1000000ULL;
  if (elapsed_ms + 20U < static_cast<uint64_t>(kIterations) * kStepMs)
  {
    return 21;
  }

  std::printf("[BENCH] sync_sleep_until iterations=%u step_ms=%u elapsed_ms=%" PRIu64 "\n",
              kIterations, kStepMs, elapsed_ms);
  return 0;
}

}  // namespace

int main()
{
  InitMinimalLinuxTimebase();

  int status = 0;
  status |= RunMutexStress();
  status |= RunSemaphoreStress();
  status |= RunThreadSleepStress();
  return status;
}
