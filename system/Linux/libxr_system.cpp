#include "libxr_system.hpp"

#include <bits/types/FILE.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstddef>

#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "libxr_type.hpp"
#include "linux_timebase.hpp"

struct timeval libxr_linux_start_time;

struct timespec libxr_linux_start_time_spec;  // NOLINT

static LibXR::LinuxTimebase libxr_linux_timebase;

void LibXR::PlatformInit() {
  auto write_fun = [](WritePort &port) {
    static uint8_t write_buff[1024];
    size_t size = 0;
    port.queue_data_->PopBlock(write_buff, &size);
    auto ans = fwrite(write_buff, 1, size, stdout);

    UNUSED(ans);

    WriteOperation op;
    port.queue_info_->Pop(op);

    port.UpdateStatus(false, ErrorCode::OK, op, size);

    return ErrorCode::OK;
  };

  LibXR::STDIO::write_ = new LibXR::WritePort();

  *LibXR::STDIO::write_ = write_fun;

  auto read_fun = [](ReadPort &port) {
    ReadInfoBlock info;
    port.queue_block_->Pop(info);

    auto need_read = info.data_.size_;

    port.read_size_ = fread(info.data_.addr_, sizeof(char), need_read, stdin);
    port.UpdateStatus(false, ErrorCode::OK, info, need_read);
    return ErrorCode::OK;
  };

  LibXR::STDIO::read_ = new LibXR::ReadPort();

  *LibXR::STDIO::read_ = read_fun;

  gettimeofday(&libxr_linux_start_time, nullptr);
  UNUSED(clock_gettime(CLOCK_REALTIME, &libxr_linux_start_time_spec));

  int res = 0;
  res = system("stty -icanon");
  res = system("stty -echo");
  UNUSED(res);
}
