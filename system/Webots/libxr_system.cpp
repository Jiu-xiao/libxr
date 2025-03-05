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
#include "webots/Robot.hpp"

uint64_t _libxr_webots_time_count = 0;
webots::Robot *_libxr_webots_robot_handle = nullptr;
static float time_step = 0.0f;
LibXR::ConditionVar *_libxr_webots_time_notify = nullptr;

void LibXR::PlatformInit(webots::Robot *robot = nullptr) {
  auto write_fun = [](WritePort &port) {
    auto ans = fwrite(port.info_.data.addr_, 1, port.info_.data.size_, stdout);

    UNUSED(ans);

    port.UpdateStatus(false, ErrorCode::OK);

    return ErrorCode::OK;
  };

  LibXR::STDIO::write = new LibXR::WritePort();

  *LibXR::STDIO::write = write_fun;

  auto read_fun = [](ReadPort &port) {
    auto need_read = port.info_.data.size_;

    UNUSED(need_read);

    port.info_.data.size_ = fread(port.info_.data.addr_, sizeof(char),
                                  port.info_.data.size_, stdin);

    port.UpdateStatus(false, ErrorCode::OK);
    return ErrorCode::OK;
  };

  LibXR::STDIO::read = new LibXR::ReadPort();

  *LibXR::STDIO::read = read_fun;

  system("stty -icanon");
  system("stty -echo");

  if (robot == nullptr) {
    _libxr_webots_robot_handle = new webots::Robot();
  } else {
    _libxr_webots_robot_handle = robot;
  }

  time_step = _libxr_webots_robot_handle->getBasicTimeStep();

  if (_libxr_webots_robot_handle == nullptr) {
    printf("webots robot handle is null.\n");
    exit(-1);
  }

  _libxr_webots_time_notify = new LibXR::ConditionVar();

  auto webots_timebase_thread_fun = [](void *) {
    poll(nullptr, 0, 100);

    while (true) {
      poll(nullptr, 0, 1);
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
