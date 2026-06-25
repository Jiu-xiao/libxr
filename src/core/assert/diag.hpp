#pragma once

#include <cstdint>
#include <utility>

#include "libxr_cb.hpp"

namespace LibXR::Assert
{
/**
 * @brief Fatal 错误回调类型，参数为文件名与行号 / Fatal error callback type taking file name and line number
 */
using FatalCallback = LibXR::Callback<const char*, uint32_t>;

// Process-global fatal callback storage.
// 进程级 fatal 回调存根。
inline FatalCallback fatal_error_callback_;  // NOLINT

/**
 * @brief 注册 fatal 错误回调 / Register the fatal error callback
 * @param cb 用户提供的 fatal 错误回调 / User-provided fatal error callback
 */
inline void RegisterFatalErrorCallback(FatalCallback cb)
{
  fatal_error_callback_ = std::move(cb);
}

/**
 * @brief 返回当前注册的 fatal 错误回调对象 / Return the currently registered fatal error callback object
 * @return 当前注册的 fatal 错误回调对象 / Returns the currently registered fatal error callback object
 */
[[nodiscard]] inline FatalCallback& FatalErrorCallback() { return fatal_error_callback_; }
}  // namespace LibXR::Assert
