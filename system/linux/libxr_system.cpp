#include "libxr_system.hpp"

#include <sys/select.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>

#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "libxr_type.hpp"
#include "linux_timebase.hpp"
#include "logger.hpp"
#include "thread.hpp"
#include "timer.hpp"

struct timespec libxr_linux_start_time_spec;  // NOLINT

static LibXR::LinuxTimebase libxr_linux_timebase;

// The permanent STDIO write worker needs its wake semaphore for the whole process
// lifetime.
static LibXR::Semaphore* const stdo_sem = new LibXR::Semaphore;
static constexpr size_t host_stdio_queue_bytes = 4096;

void StdiThread(LibXR::ReadPort* read_port)
{
  static uint8_t read_buff[host_stdio_queue_bytes];

  if (!isatty(STDIN_FILENO))
  {
    XR_LOG_WARN("STDIO.read_: stdin is not a TTY, parking thread forever");
    while (true)
    {
      LibXR::Thread::Sleep(UINT32_MAX);
    }
  }

  while (true)
  {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);

    const int ret = select(STDIN_FILENO + 1, &rfds, NULL, NULL, NULL);

    if (ret > 0 && FD_ISSET(STDIN_FILENO, &rfds))
    {
      const ssize_t size = read(STDIN_FILENO, read_buff, sizeof(read_buff));
      if (size > 0)
      {
        const auto push_ans =
            read_port->queue_data_->PushBatch(read_buff, static_cast<size_t>(size));
        if (push_ans == LibXR::ErrorCode::OK)
        {
          read_port->ProcessPendingReads(false);
        }
        continue;
      }

      if (size < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
      {
        continue;
      }

      return;
    }
  }
}

void StdoThread(LibXR::WritePort* write_port)
{
  LibXR::WriteInfoBlock info;
  static uint8_t write_buff[host_stdio_queue_bytes];

  while (true)
  {
    if (stdo_sem->Wait() == LibXR::ErrorCode::OK)
    {
      auto ans = write_port->queue_info_->Pop(info);
      if (ans != LibXR::ErrorCode::OK)
      {
        continue;
      }

      ans = write_port->queue_data_->PopBatch(write_buff, info.data.size_);
      if (ans != LibXR::ErrorCode::OK)
      {
        continue;
      }

      auto write_size = fwrite(write_buff, sizeof(char), info.data.size_, stdout);
      auto fflush_ans = fflush(stdout);

      UNUSED(write_size);
      UNUSED(fflush_ans);

      write_port->Finish(
          false,
          write_size == info.data.size_ ? LibXR::ErrorCode::OK : LibXR::ErrorCode::FAILED,
          info);
    }
  }
}

void LibXR::PlatformInit(uint32_t timer_pri, uint32_t timer_stack_depth)
{
  LibXR::Timer::priority_ = static_cast<LibXR::Thread::Priority>(timer_pri);
  LibXR::Timer::stack_depth_ = timer_stack_depth;
  auto write_fun = [](WritePort& port, bool)
  {
    UNUSED(port);
    stdo_sem->Post();
    return LibXR::ErrorCode::PENDING;
  };

  LibXR::STDIO::write_ = new LibXR::WritePort(32, host_stdio_queue_bytes);

  *LibXR::STDIO::write_ = write_fun;

  LibXR::STDIO::read_ = new LibXR::ReadPort(host_stdio_queue_bytes);

  struct termios tty;
  tcgetattr(STDIN_FILENO, &tty);           // 获取当前终端属性
  tty.c_lflag &= ~(ICANON | ECHO);         // 禁用规范模式和回显
  tcsetattr(STDIN_FILENO, TCSANOW, &tty);  // 立即生效

  LibXR::Thread stdi_thread, stdo_thread;
  stdi_thread.Create<LibXR::ReadPort*>(LibXR::STDIO::read_, StdiThread, "STDIO.read_",
                                       1024, LibXR::Thread::Priority::MEDIUM);

  stdo_thread.Create<LibXR::WritePort*>(LibXR::STDIO::write_, StdoThread, "STDIO.write_",
                                        1024, LibXR::Thread::Priority::MEDIUM);
}
