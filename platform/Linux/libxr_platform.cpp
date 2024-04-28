#include "libxr_platform.hpp"
#include "libxr_assert.hpp"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "libxr_type.hpp"
#include "list.hpp"
#include "queue.hpp"
#include "semaphore.hpp"
#include "thread.hpp"
#include "timer.hpp"
#include <bits/types/FILE.h>
#include <cstdint>
#include <liburing.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

struct timeval _libxr_linux_start_time;
struct timespec _libxr_linux_start_time_spec;

void LibXR::PlatformInit() {

  ErrorCode (*write_fun)(Operation<ErrorCode> & op, ConstRawData data) =
      [](Operation<ErrorCode> &op, ConstRawData data) {
        auto ans = fwrite(data.addr_, 1, data.size_, stdout);

        switch (op.type) {
        case Operation<ErrorCode>::OP_TYPE_BLOCK:
          break;
        case Operation<ErrorCode>::OP_TYPE_CALLBACK:
          op.data.callback.RunFromUser(ans == data.size_ ? NO_ERR : ERR_FAIL);
          break;
        case Operation<ErrorCode>::OP_TYPE_POLLING:
          op.data.status = Operation<ErrorCode>::OP_DONE;
          break;
        }

        return NO_ERR;
      };

  LibXR::STDIO::write = write_fun;

  ErrorCode (*read_fun)(Operation<ErrorCode, RawData &> &,
                        RawData) = [](Operation<ErrorCode, RawData &> &op,
                                      RawData buff) {
    auto need_read = buff.size_;
    buff.size_ = fread(buff.addr_, sizeof(char), buff.size_, stdin);

    switch (op.type) {
    case Operation<ErrorCode, RawData &>::OP_TYPE_BLOCK:
      break;
    case Operation<ErrorCode, RawData &>::OP_TYPE_CALLBACK:
      op.data.callback.RunFromUser(buff.size_ > 0 ? NO_ERR : ERR_FAIL, buff);
      break;
    case Operation<ErrorCode, RawData &>::OP_TYPE_POLLING:
      op.data.status = Operation<ErrorCode, RawData &>::OP_DONE;
      break;
    }
    return NO_ERR;
  };

  LibXR::STDIO::read = read_fun;

  void (*err_fun)(const char *log) = [](const char *log) {
    static char prase_buff[1024];
    printf("Error:%s\r\n", log);
  };

  LibXR::STDIO::error = err_fun;

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
