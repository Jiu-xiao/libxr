#include "libxr_platform.hpp"
#include "libxr_assert.hpp"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "libxr_type.hpp"
#include "list.hpp"
#include "semaphore.hpp"
#include "thread.hpp"
#include "timer.hpp"
#include <sys/time.h>

struct timeval _libxr_linux_start_time;
struct timespec _libxr_linux_start_time_spec;

void LibXR::PlatformInit() {
  LibXR::WriteFunction write_fun = [](ConstRawData &data,
                                      Operation<ErrorCode> &op) {
    auto count = fwrite(data.addr_, sizeof(char), data.size_, stdout);
    if (count == data.size_) {
      return NO_ERR;
    } else {
      return ERR_BUSY;
    }
  };

  LibXR::ReadFunction read_fun = [](RawData &data,
                                    Operation<ErrorCode, const RawData &> &op) {
    data.size_ = fread(data.addr_, sizeof(char), data.size_, stdin);
    return NO_ERR;
  };

  gettimeofday(&_libxr_linux_start_time, NULL);
  clock_gettime(CLOCK_REALTIME, &_libxr_linux_start_time_spec);

  static Semaphore sem(0);

  void (*thread_fun)(Thread::Priority) = [](Thread::Priority priority) {
    sem.Post();
    TimestampMS time = Thread::GetTime();
    while (true) {
      Timer::Refresh(priority);
      Thread::SleepUntil(time, 1);
    }
  };

  for (int i = 0; i < Thread::Priority::PRIORITY_NUMBER; i++) {
    LibXR::Timer::list_[i] = new LibXR::List;
    auto thread_handle = new Thread;
    thread_handle->Create((Thread::Priority)i, thread_fun, "libxr_timer_task",
                          512, Thread::PRIORITY_HIGH);
    sem.Wait();
  }
}
