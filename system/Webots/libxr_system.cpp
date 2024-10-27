#include "libxr_system.hpp"

#include <bits/types/FILE.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdint>

#include "condition_var.hpp"
#include "libxr_assert.hpp"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "libxr_type.hpp"
#include "list.hpp"
#include "queue.hpp"
#include "semaphore.hpp"
#include "thread.hpp"
#include "timer.hpp"

uint64_t _libxr_webots_time_count = 0;
webots::Robot *_libxr_webots_robot_handle = NULL;
static float time_step = 0.0f;
LibXR::ConditionVar *_libxr_webots_time_notify = NULL;

void LibXR::PlatformInit() {
  auto write_fun = [](WriteOperation &op, ConstRawData data, WritePort &port) {
    auto ans = fwrite(data.addr_, 1, data.size_, stdout);

    UNUSED(op);
    UNUSED(ans);

    port.UpdateStatus(false, ErrorCode::OK);

    return ErrorCode::OK;
  };

  LibXR::STDIO::write = new LibXR::WritePort();

  *LibXR::STDIO::write = write_fun;

  auto read_fun = [](Operation<ErrorCode, RawData &> &op, RawData buff,
                     ReadPort &port) {
    auto need_read = buff.size_;

    UNUSED(op);
    UNUSED(need_read);

    buff.size_ = fread(buff.addr_, sizeof(char), buff.size_, stdin);

    port.UpdateStatus(false, ErrorCode::OK);
    return ErrorCode::OK;
  };

  LibXR::STDIO::read = new LibXR::ReadPort();

  *LibXR::STDIO::read = read_fun;

  system("stty -icanon");
  system("stty -echo");

  _libxr_webots_robot_handle = new webots::Robot();

  time_step = _libxr_webots_robot_handle->getBasicTimeStep();

  if (time_step >= 2.0f) {
    printf(
        "webots basic time step should be less than 2ms, but now it is "
        "%.3f ms.\n",
        time_step);
    exit(-1);
  }

  _libxr_webots_time_notify = new LibXR::ConditionVar();

  auto webots_timebase_thread_fun = [](void *) {
    while (true) {
      poll(NULL, 0, 1);
      _libxr_webots_robot_handle->step(time_step);
      _libxr_webots_time_count++;
      _libxr_webots_time_notify->Broadcast();
    }
  };

  LibXR::Thread webots_timebase_thread;
  webots_timebase_thread.Create<void *>((void *)(0), webots_timebase_thread_fun,
                                        "webots_timebase_thread", 1024,
                                        Thread::Priority::REALTIME);
}
