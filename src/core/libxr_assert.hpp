#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

#include "libxr_cb.hpp"
#include "libxr_def.hpp"

/**
 * @brief 触发致命错误并停止程序执行。
 *        Triggers a fatal error and halts execution.
 *
 * 该函数用于指示系统发生严重故障，并立即终止程序执行。
 * 它可用于普通运行环境和 ISR（中断服务例程）环境中。
 * This function is used to indicate a critical failure in the system.
 * It can be called from both normal and ISR (Interrupt Service Routine) contexts.
 *
 * @param file 发生错误的源文件名。 The file where the error occurred.
 * @param line 发生错误的行号。 The line number where the error occurred.
 * @param in_isr 指示错误是否发生在 ISR 上下文中。 Indicates whether the error occurred in
 * an ISR context.
 */
void libxr_fatal_error(const char *file, uint32_t line, bool in_isr);

namespace LibXR
{
/**
 * @class Assert
 * @brief 运行时错误检查的断言工具类。
 *        Provides assertion mechanisms for runtime error checking.
 *
 * `Assert` 类提供了注册和处理致命错误的机制，支持回调函数，并可执行
 * 运行时的大小限制检查（仅在调试模式下启用）。
 * The `Assert` class is designed to register and handle fatal errors
 * through callback functions and provides size limit verification.
 */
class Assert
{
 public:
  /**
   * @brief 注册致命错误的回调函数。
   *        Registers a fatal error callback.
   *
   * 该回调函数会在发生致命错误时被触发，用于执行相应的错误处理逻辑。
   * This callback will be triggered when a fatal error occurs.
   *
   * @param cb 要注册的回调函数。 The callback function to be registered.
   */
  static void RegisterFatalErrorCB(const LibXR::Callback<const char *, uint32_t> &cb)
  {
    libxr_fatal_error_callback_ = cb;
  }

  /**
   * @brief 通过移动语义注册致命错误的回调函数。
   *        Registers a fatal error callback with move semantics.
   *
   * 该重载版本允许通过移动回调对象，提高回调函数注册的效率。
   * This overload allows for efficient callback assignment by moving the callback object.
   *
   * @param cb 要注册的回调函数（通过移动语义传递）。 The callback function to be
   * registered.
   */
  static void RegisterFatalErrorCB(LibXR::Callback<const char *, uint32_t> &&cb)
  {
    libxr_fatal_error_callback_ = std::move(cb);
  }

#ifdef LIBXR_DEBUG_BUILD
  /**
   * @brief 执行大小限制检查（仅在调试模式下启用）。
   *        Performs a size limit check based on the specified mode.
   *
   * 该函数用于检查指定大小是否符合给定的限制模式。支持以下三种模式：
   * - `EQUAL`：大小必须等于限制值。
   * - `MORE`：大小必须大于等于限制值。
   * - `LESS`：大小必须小于等于限制值。
   * This function ensures that a given size adheres to a specified limit,
   * with different validation modes:
   * - `EQUAL`: The size must be equal to the limit.
   * - `MORE`: The size must be greater than or equal to the limit.
   * - `LESS`: The size must be less than or equal to the limit.
   *
   * @tparam mode 大小检查模式（`EQUAL`, `MORE`, `LESS`）。 The size constraint mode.
   * @param limit 参考限制值。 The reference limit value.
   * @param size 要检查的实际大小。 The actual size to be checked.
   */
  template <SizeLimitMode mode>
  static void SizeLimitCheck(size_t limit, size_t size)
  {
    if constexpr (mode == SizeLimitMode::EQUAL)
    {
      ASSERT(limit == size);
    }
    else if constexpr (mode == SizeLimitMode::MORE)
    {
      ASSERT(limit <= size);
    }
    else if constexpr (mode == SizeLimitMode::LESS)
    {
      ASSERT(limit >= size);
    }
  }
#else
  /**
   * @brief 在非调试模式下的占位大小检查函数（无实际作用）。
   *        Dummy size limit check for non-debug builds.
   *
   * 在发布模式下，该函数不会执行任何操作，以避免不必要的开销。
   * In release mode, this function does nothing to avoid unnecessary checks.
   *
   * @tparam mode 大小检查模式（`EQUAL`, `MORE`, `LESS`）。 The size constraint mode.
   * @param limit 参考限制值。 The reference limit value.
   * @param size 要检查的实际大小。 The actual size to be checked.
   */
  template <SizeLimitMode mode>
  static void SizeLimitCheck(size_t limit, size_t size)
  {
    UNUSED(limit);
    UNUSED(size);
  };
#endif

  /**
   * @brief 存储已注册的致命错误回调函数。
   *        Stores the registered fatal error callback.
   *
   * 该可选回调函数用于处理致命错误，如果未注册，则为空。
   * This optional callback is used to handle fatal errors if registered.
   */
  static std::optional<LibXR::Callback<const char *, uint32_t>>
      libxr_fatal_error_callback_;
};
}  // namespace LibXR
