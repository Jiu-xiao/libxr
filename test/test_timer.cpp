#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"
#include "timer.hpp"

void test_timer()
{
  int timer_arg = 0;

  auto handle =
      LibXR::Timer::CreateTask<int *>([](int *arg) { *arg = *arg + 1; }, &timer_arg, 10);

  LibXR::Timer::Add(handle);
  LibXR::Timer::Start(handle);

  LibXR::Thread::Sleep(205);
  LibXR::Timer::Stop(handle);
  for (int i = 0; i < 10; i++)
  {
    timer_arg = 0;
    LibXR::Timer::Start(handle);
    LibXR::Thread::Sleep(205);
    LibXR::Timer::Stop(handle);
    if (timer_arg == 20)
    {
      break;
    }
  }

  ASSERT(timer_arg == 20);

  pthread_cancel(LibXR::Timer::thread_handle_);
}