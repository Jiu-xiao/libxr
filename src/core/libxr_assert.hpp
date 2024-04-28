#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "libxr_type.hpp"

namespace LibXR {
class Assert {
public:
  static void
  RegisterFatalErrorCB(const LibXR::Callback<const char *, uint32_t> &cb) {
    libxr_fatal_error_callback = &cb;
  }

  static void FatalError(const char *file, uint32_t line) {
    while (1) {
      if (LibXR::STDIO::write) {
        printf("Fatal error at %s:%d\r\n", file, line);
      }

      if (libxr_fatal_error_callback) {
        libxr_fatal_error_callback->RunFromUser(file, line);
      }
    }
  }

private:
  static const LibXR::Callback<const char *, uint32_t>
      *libxr_fatal_error_callback;
};
} // namespace LibXR