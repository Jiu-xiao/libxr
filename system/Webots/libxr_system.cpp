#include "libxr_system.hpp"

#include <bits/types/FILE.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "libxr_type.hpp"
#include "thread.hpp"
#include "webots/Robot.hpp"

uint64_t _libxr_webots_time_count = 0;
webots::Robot *_libxr_webots_robot_handle = nullptr;  // NOLINT
static float time_step = 0.0f;
condition_var_handle *_libxr_webots_time_notify = nullptr;

void LibXR::PlatformInit(webots::Robot *robot = nullptr)
{  // NOLINT
  auto write_fun = [](WritePort &port)
  {
    static uint8_t write_buff[1024];
    WritePort::WriteInfo info;
    while (true)
    {
      if (port.queue_info_->Pop(info) != ErrorCode::OK)
      {
        return ErrorCode::OK;
      }

      port.queue_data_->PopBatch(write_buff, info.size);
      auto ans = fwrite(write_buff, 1, info.size, stdout);
      UNUSED(ans);
      port.queue_info_->Pop(info);

      port.UpdateStatus(false, ErrorCode::OK, info.op, info.size);
    }

    return ErrorCode::OK;
  };

  LibXR::STDIO::write_ = new LibXR::WritePort(32, 32 * LIBXR_PRINTF_BUFFER_SIZE);

  *LibXR::STDIO::write_ = write_fun;

  auto read_fun = [](ReadPort &port)
  {
    ReadInfoBlock info;
    port.queue_block_->Pop(info);

    auto need_read = info.data_.size_;

    port.read_size_ = fread(info.data_.addr_, sizeof(char), need_read, stdin);
    port.UpdateStatus(false, ErrorCode::OK, info, need_read);
    return ErrorCode::OK;
  };

  LibXR::STDIO::read_ = new LibXR::ReadPort(32, 32 * LIBXR_PRINTF_BUFFER_SIZE);

  *LibXR::STDIO::read_ = read_fun;

  struct termios tty;
  tcgetattr(STDIN_FILENO, &tty);           // 获取当前终端属性
  tty.c_lflag &= ~(ICANON | ECHO);         // 禁用规范模式和回显
  tcsetattr(STDIN_FILENO, TCSANOW, &tty);  // 立即生效

  if (robot == nullptr)
  {
    _libxr_webots_robot_handle = new webots::Robot();
  }
  else
  {
    _libxr_webots_robot_handle = robot;
  }

  time_step = _libxr_webots_robot_handle->getBasicTimeStep();

  if (_libxr_webots_robot_handle == nullptr)
  {
    printf("webots robot handle is null.\n");
    exit(-1);
  }

  _libxr_webots_time_notify = new condition_var_handle;
  pthread_mutex_init(_libxr_webots_time_notify->mutex, nullptr);
  pthread_cond_init(_libxr_webots_time_notify->cond, nullptr);

  auto webots_timebase_thread_fun = [](void *)
  {
    poll(nullptr, 0, 100);

    while (true)
    {
      poll(nullptr, 0, 1);
      _libxr_webots_robot_handle->step(time_step);
      _libxr_webots_time_count++;
      pthread_mutex_lock(_libxr_webots_time_notify->mutex);
      pthread_cond_broadcast(_libxr_webots_time_notify->cond);
      pthread_mutex_unlock(_libxr_webots_time_notify->mutex);
    }
  };

  LibXR::Thread webots_timebase_thread;
  webots_timebase_thread.Create<void *>(
      reinterpret_cast<void *>(0), webots_timebase_thread_fun, "webots_timebase_thread",
      1024, Thread::Priority::REALTIME);
}
