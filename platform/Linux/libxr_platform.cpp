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
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

struct timeval _libxr_linux_start_time;
struct timespec _libxr_linux_start_time_spec;

void LibXR::PlatformInit() {

  auto write_fun = [](WriteOperation &op, ConstRawData data) {
    auto ans = fwrite(data.addr_, 1, data.size_, stdout);

    switch (op.type) {
    case WriteOperation::OperationType::BLOCK:
      break;
    case WriteOperation::OperationType::CALLBACK:
      op.data.callback.RunFromUser(ans == data.size_ ? ErrorCode::OK
                                                     : ErrorCode::FAILED);
      break;
    case WriteOperation::OperationType::POLLING:
      op.data.status = WriteOperation::OperationPollingStatus::DONE;
      break;
    }

    return ErrorCode::OK;
  };

  LibXR::STDIO::write = write_fun;

  auto read_fun = [](Operation<ErrorCode, RawData &> &op, RawData buff) {
    auto need_read = buff.size_;
    buff.size_ = fread(buff.addr_, sizeof(char), buff.size_, stdin);

    switch (op.type) {
    case Operation<ErrorCode, RawData &>::OperationType::BLOCK:
      break;
    case Operation<ErrorCode, RawData &>::OperationType::CALLBACK:
      op.data.callback.RunFromUser(
          buff.size_ > 0 ? ErrorCode::OK : ErrorCode::FAILED, buff);
      break;
    case Operation<ErrorCode, RawData &>::OperationType::POLLING:
      op.data.status =
          Operation<ErrorCode, RawData &>::OperationPollingStatus::DONE;
      break;
    }
    return ErrorCode::OK;
  };

  LibXR::STDIO::read = read_fun;

  auto err_fun = [](const char *log) {
    static char prase_buff[4096];
    printf("Error:%s\r\n", log);
  };

  LibXR::STDIO::error = err_fun;

  gettimeofday(&_libxr_linux_start_time, NULL);
  clock_gettime(CLOCK_REALTIME, &_libxr_linux_start_time_spec);

  static Semaphore sem(0);

  auto thread_fun = [](Thread::Priority priority) {
    sem.Post();
    TimestampMS time = Thread::GetTime();
    while (true) {
      Timer::Refresh(priority);
      Thread::SleepUntil(time, 1);
    }
  };

  for (int i = 0; i < static_cast<size_t>(Thread::Priority::NUMBER); i++) {
    LibXR::Timer::list_[i] = new LibXR::List;
    auto thread_handle = new Thread;
    thread_handle->Create<Thread::Priority>((Thread::Priority)i, thread_fun,
                                            "libxr_timer_task", 512,
                                            Thread::Priority::HIGH);
    sem.Wait();
  }
}
