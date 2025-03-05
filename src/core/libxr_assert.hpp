#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

#include "libxr_cb.hpp"
#include "libxr_def.hpp"

void libxr_fatal_error(const char *file, uint32_t line, bool in_isr);

namespace LibXR {
class Assert {
 public:
  static void RegisterFatalErrorCB(
      const LibXR::Callback<const char *, uint32_t> &cb) {
    libxr_fatal_error_callback_ = cb;
  }

  static void RegisterFatalErrorCB(
      LibXR::Callback<const char *, uint32_t> &&cb) {
    libxr_fatal_error_callback_ = std::move(cb);
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
  template <SizeLimitMode mode>
  static void SizeLimitCheck(size_t limit, size_t size) {
    UNUSED(limit);
    UNUSED(size);
  };
#endif

  static std::optional<LibXR::Callback<const char *, uint32_t>>
      libxr_fatal_error_callback_;
};
}  // namespace LibXR
