#include "libxr_system.hpp"

#include <bits/types/FILE.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdint>

#include "libxr_assert.hpp"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "libxr_type.hpp"
#include "linux_timebase.hpp"
#include "list.hpp"
#include "queue.hpp"
#include "semaphore.hpp"
#include "thread.hpp"
#include "timer.hpp"

struct timeval _libxr_linux_start_time;
struct timespec _libxr_linux_start_time_spec;

static LibXR::LinuxTimebase _libxr_linux_timebase;

void LibXR::PlatformInit() {
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

  gettimeofday(&_libxr_linux_start_time, nullptr);
  clock_gettime(CLOCK_REALTIME, &_libxr_linux_start_time_spec);

  system("stty -icanon");
  system("stty -echo");
}
