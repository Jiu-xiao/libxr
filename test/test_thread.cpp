#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

#if defined(LIBXR_SYSTEM_POSIX_HOST)
#include <pthread.h>
#endif

#include <time.h>

namespace
{

void JoinThreadIfNeeded(LibXR::Thread& thread)
{
#if defined(LIBXR_SYSTEM_POSIX_HOST)
  pthread_join(thread, nullptr);
#else
  UNUSED(thread);
#endif
}

uint64_t NowMonotonicMs()
{
  struct timespec ts = {};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000ULL +
         static_cast<uint64_t>(ts.tv_nsec) / 1000000ULL;
}

}  // namespace

void test_thread()
{
  LibXR::Thread thread;
  LibXR::Semaphore sem(0);

  ASSERT(sem.Wait(0) == LibXR::ErrorCode::TIMEOUT);

  thread.Create<LibXR::Semaphore*>(
      &sem,
      [](LibXR::Semaphore* sem)
      {
        sem->Post();
        return;
      },
      "test_task", 512, LibXR::Thread::Priority::REALTIME);

  ASSERT(sem.Wait(200) == LibXR::ErrorCode::OK);
  JoinThreadIfNeeded(thread);

  const uint64_t sleep_start_ms = NowMonotonicMs();
  LibXR::Thread::Sleep(20);
  const uint64_t sleep_elapsed_ms = NowMonotonicMs() - sleep_start_ms;
  ASSERT(sleep_elapsed_ms >= 15);

  LibXR::MillisecondTimestamp wakeup = LibXR::Thread::GetTime();
  const uint32_t periodic_start_ms = wakeup;
  LibXR::Thread::SleepUntil(wakeup, 10);
  const uint32_t first_wakeup_ms = LibXR::Thread::GetTime();
  LibXR::Thread::SleepUntil(wakeup, 10);
  const uint32_t second_wakeup_ms = LibXR::Thread::GetTime();
  ASSERT(first_wakeup_ms - periodic_start_ms >= 8);
  ASSERT(second_wakeup_ms - periodic_start_ms >= 18);
  ASSERT(second_wakeup_ms >= first_wakeup_ms);
}
