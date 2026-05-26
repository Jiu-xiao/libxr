#pragma once

#include <cstdint>
#include <utility>

#include "libxr_cb.hpp"

namespace LibXR::Assert
{
using FatalCallback = LibXR::Callback<const char*, uint32_t>;

inline FatalCallback fatal_error_callback_;  // NOLINT

inline void RegisterFatalErrorCallback(FatalCallback cb)
{
  fatal_error_callback_ = std::move(cb);
}

[[nodiscard]] inline FatalCallback& FatalErrorCallback() { return fatal_error_callback_; }
}  // namespace LibXR::Assert
