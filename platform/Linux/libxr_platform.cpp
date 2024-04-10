#include "libxr_platform.hpp"
#include "libxr_rw.hpp"
#include <sys/time.h>

struct timeval _libxr_linux_start_time;

void LibXR::PlatformInit() {
  LibXR::WriteFunction write_fun = [](const RawData &data,
                                      Operation<ErrorCode> &op) {
    auto count = fwrite(data.addr_, sizeof(char), data.size_, stdout);
    if (count == data.size_) {
      return NO_ERR;
    } else {
      return ERR_BUSY;
    }
  };

  LibXR::ReadFunction read_fun = [](RawData &data,
                                    Operation<ErrorCode, const RawData &> &op) {
    data.size_ = fread(data.addr_, sizeof(char), data.size_, stdin);
    return NO_ERR;
  };

  LibXR::STDIO::write = write_fun;
  LibXR::STDIO::read = read_fun;

  gettimeofday(&_libxr_linux_start_time, NULL);
}
