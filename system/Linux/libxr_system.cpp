#include "libxr_system.hpp"

#include <bits/types/FILE.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "libxr_type.hpp"
#include "linux_timebase.hpp"

struct timeval libxr_linux_start_time;

struct timespec libxr_linux_start_time_spec;

static LibXR::LinuxTimebase libxr_linux_timebase;

void LibXR::PlatformInit() {
  auto write_fun = [](WritePort &port) {
    auto ans = fwrite(port.info_.data.addr_, 1, port.info_.data.size_, stdout);

    UNUSED(ans);

    port.UpdateStatus(false, ErrorCode::OK);

    return ErrorCode::OK;
  };

  LibXR::STDIO::write_ = new LibXR::WritePort();

  *LibXR::STDIO::write_ = write_fun;

  auto read_fun = [](ReadPort &port) {
    auto need_read = port.info_.data.size_;

    UNUSED(need_read);

    port.info_.data.size_ = fread(port.info_.data.addr_, sizeof(char),
                                  port.info_.data.size_, stdin);

    port.UpdateStatus(false, ErrorCode::OK);
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
