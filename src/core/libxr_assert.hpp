#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "libxr_def.hpp"
#include "libxr_rw.hpp"

namespace LibXR {
class Assert {
public:
  static void
  RegisterFatalErrorCB(const LibXR::Callback<const char *, uint32_t> &cb) {
    libxr_fatal_error_callback = cb;
  }

  static void
  RegisterFatalErrorCB(LibXR::Callback<const char *, uint32_t> &&cb) {
    libxr_fatal_error_callback = std::move(cb);
  }

  static void FatalError(const char *file, uint32_t line, bool in_isr) {
    volatile bool stop = false;
    while (!stop) {
      if (LibXR::STDIO::write && LibXR::STDIO::write->Writable()) {
        printf("Fatal error at %s:%d\r\n", file, int(line));
      }

      if (libxr_fatal_error_callback) {
        libxr_fatal_error_callback->Run(in_isr, file, line);
      }
    }
  }

#ifdef LIBXR_DEBUG_BUILD
  template <SizeLimitMode mode>
  static void SizeLimitCheck(size_t limit, size_t size) {
    if constexpr (mode == SizeLimitMode::EQUAL) {
      ASSERT(limit == size);
    } else if constexpr (mode == SizeLimitMode::MORE) {
      ASSERT(limit <= size);
    } else if constexpr (mode == SizeLimitMode::LESS) {
      ASSERT(limit >= size);
    }
  }
#else
  static void SizeLimitCheck(size_t limit, size_t size, SizeLimitMode mode) {
    UNUSED(limit);
    UNUSED(size);
    UNUSED(mode);
  };
#endif

private:
  static std::optional<LibXR::Callback<const char *, uint32_t>>
      libxr_fatal_error_callback;
};
} // namespace LibXR