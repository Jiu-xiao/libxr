#include "libxr_system.hpp"

#include <bits/types/FILE.h>
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

struct timeval libxr_linux_start_time;

struct timespec libxr_linux_start_time_spec;  // NOLINT

static LibXR::LinuxTimebase libxr_linux_timebase;

void LibXR::PlatformInit()
{
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

  LibXR::STDIO::write_ =
      new LibXR::WritePort(32, static_cast<size_t>(4 * LIBXR_PRINTF_BUFFER_SIZE));

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

  LibXR::STDIO::read_ =
      new LibXR::ReadPort(32, static_cast<size_t>(4 * LIBXR_PRINTF_BUFFER_SIZE));

  *LibXR::STDIO::read_ = read_fun;

  gettimeofday(&libxr_linux_start_time, nullptr);
  UNUSED(clock_gettime(CLOCK_REALTIME, &libxr_linux_start_time_spec));

  struct termios tty;
  tcgetattr(STDIN_FILENO, &tty);           // 获取当前终端属性
  tty.c_lflag &= ~(ICANON | ECHO);         // 禁用规范模式和回显
  tcsetattr(STDIN_FILENO, TCSANOW, &tty);  // 立即生效
}
