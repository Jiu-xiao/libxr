#include "libxr_system.hpp"

#include <poll.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <termios.h>
#include <unistd.h>

#include <atomic>
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

// The doorbell is level-like. Each carrier services one bounded front-plus-next
// snapshot; the pending bit coalesces concurrent and follow-up carriers.
static LibXR::Semaphore* const stdo_sem = new LibXR::Semaphore;
static std::atomic<bool> stdo_wake_pending{false};
static constexpr size_t host_stdio_queue_bytes = 4096;

void NotifyStdoWorker()
{
  if (!stdo_wake_pending.exchange(true, std::memory_order_release))
  {
    stdo_sem->Post();
  }
}

namespace LibXR::Detail
{
bool ServiceStdoOnce(WritePort& write_port, int output_fd)
{
  size_t accepted = 0U;
  bool fatal_error = false;
  {
    auto queue = write_port.GetWriteQueue();
    const size_t offered = queue.front_size + queue.next_size;
    if (offered == 0U)
    {
      return false;
    }

    accepted = queue.PopWithWriter(
        offered,
        [output_fd, &fatal_error](const uint8_t* first, size_t first_size,
                                  const uint8_t* second, size_t second_size) -> size_t
        {
          iovec spans[2] = {{const_cast<uint8_t*>(first), first_size},
                            {const_cast<uint8_t*>(second), second_size}};
          const int span_count = second_size == 0U ? 1 : 2;
          while (true)
          {
            const ssize_t written = writev(output_fd, spans, span_count);
            if (written > 0)
            {
              const size_t accepted_bytes = static_cast<size_t>(written);
              REQUIRE(accepted_bytes <= first_size + second_size);
              return accepted_bytes;
            }
            if (written == 0)
            {
              fatal_error = true;
              return 0U;
            }
            if (errno == EINTR)
            {
              continue;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK)
            {
              fatal_error = true;
              return 0U;
            }

            pollfd output_poll{output_fd, POLLOUT, 0};
            int poll_result = 0;
            do
            {
              poll_result = poll(&output_poll, 1U, -1);
            } while (poll_result < 0 && errno == EINTR);

            if (poll_result <= 0 || (output_poll.revents & POLLOUT) == 0)
            {
              fatal_error = true;
              return 0U;
            }
          }
        });
    if (accepted == 0U && fatal_error)
    {
      REQUIRE(queue.FailFront(ErrorCode::FAILED));
    }
  }

  // Settlement may run completion callbacks that publish more than one request while
  // their doorbells coalesce into a single wake. Keep the next worker turn level-like
  // without consuming beyond this turn's front-plus-next snapshot.
  auto remaining = write_port.GetWriteQueue();
  return remaining.front_size != 0U;
}
}  // namespace LibXR::Detail

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
        auto queue = read_port->GetReadQueue();
        const auto push_ans = queue.PushBatch(read_buff, static_cast<size_t>(size));
        if (push_ans == LibXR::ErrorCode::OK)
        {
          queue.Publish();
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
  while (true)
  {
    while (stdo_sem->Wait() != LibXR::ErrorCode::OK)
    {
    }

    (void)stdo_wake_pending.exchange(false, std::memory_order_acquire);
    if (LibXR::Detail::ServiceStdoOnce(*write_port, STDOUT_FILENO))
    {
      NotifyStdoWorker();
    }
  }
}

void LibXR::PlatformInit(uint32_t timer_pri, uint32_t timer_stack_depth)
{
  LibXR::Timer::priority_ = static_cast<LibXR::Thread::Priority>(timer_pri);
  LibXR::Timer::stack_depth_ = timer_stack_depth;
  auto write_fun = [](WritePort&, bool) { NotifyStdoWorker(); };
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
