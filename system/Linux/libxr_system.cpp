#include "libxr_system.hpp"

#include <bits/types/FILE.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include <cstddef>

#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "libxr_type.hpp"
#include "linux_timebase.hpp"
#include "logger.hpp"
#include "thread.hpp"

struct timeval libxr_linux_start_time;

struct timespec libxr_linux_start_time_spec;  // NOLINT

static LibXR::LinuxTimebase libxr_linux_timebase;

static LibXR::Semaphore stdo_sem;

void StdiThread(LibXR::ReadPort *read_port)
{
  static uint8_t read_buff[static_cast<size_t>(4 * LIBXR_PRINTF_BUFFER_SIZE)];

  while (true)
  {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);

    int ret = select(STDIN_FILENO + 1, &rfds, NULL, NULL, NULL);

    if (ret > 0 && FD_ISSET(STDIN_FILENO, &rfds))
    {
      int ready = 0;
      if (ioctl(STDIN_FILENO, FIONREAD, &ready) != -1 && ready > 0)
      {
        auto size = fread(read_buff, sizeof(char), ready, stdin);
        if (size < 1)
        {
          continue;
        }
        read_port->queue_data_->PushBatch(read_buff, size);
        read_port->ProcessPendingReads(false);
      }
    }
  }
}

void StdoThread(LibXR::WritePort *write_port)
{
  LibXR::WriteInfoBlock info;
  static uint8_t write_buff[static_cast<size_t>(4 * LIBXR_PRINTF_BUFFER_SIZE)];

  while (true)
  {
    if (stdo_sem.Wait() == ErrorCode::OK)
    {
      auto ans = write_port->queue_info_->Pop(info);
      if (ans != ErrorCode::OK)
      {
        continue;
      }

      ans = write_port->queue_data_->PopBatch(write_buff, info.data.size_);
      if (ans != ErrorCode::OK)
      {
        continue;
      }

      auto write_size = fwrite(write_buff, sizeof(char), info.data.size_, stdout);
      fflush(stdout);
      write_port->Finish(
          false, write_size == info.data.size_ ? ErrorCode::OK : ErrorCode::FAILED, info,
          write_size);
    }
  }
}

void LibXR::PlatformInit()
{
  auto write_fun = [](WritePort &port)
  {
    UNUSED(port);
    stdo_sem.Post();
    return ErrorCode::FAILED;
  };

  LibXR::STDIO::write_ =
      new LibXR::WritePort(32, static_cast<size_t>(4 * LIBXR_PRINTF_BUFFER_SIZE));

  *LibXR::STDIO::write_ = write_fun;

  auto read_fun = [](ReadPort &port)
  {
    UNUSED(port);
    return ErrorCode::FAILED;
  };

  LibXR::STDIO::read_ =
      new LibXR::ReadPort(static_cast<size_t>(4 * LIBXR_PRINTF_BUFFER_SIZE));

  *LibXR::STDIO::read_ = read_fun;

  gettimeofday(&libxr_linux_start_time, nullptr);
  UNUSED(clock_gettime(CLOCK_REALTIME, &libxr_linux_start_time_spec));

  struct termios tty;
  tcgetattr(STDIN_FILENO, &tty);           // 获取当前终端属性
  tty.c_lflag &= ~(ICANON | ECHO);         // 禁用规范模式和回显
  tcsetattr(STDIN_FILENO, TCSANOW, &tty);  // 立即生效

  LibXR::Thread stdi_thread, stdo_thread;
  stdi_thread.Create<LibXR::ReadPort *>(LibXR::STDIO::read_, StdiThread, "STDIO.read_",
                                        1024, LibXR::Thread::Priority::MEDIUM);

  stdo_thread.Create<LibXR::WritePort *>(LibXR::STDIO::write_, StdoThread, "STDIO.write_",
                                         1024, LibXR::Thread::Priority::MEDIUM);
}
