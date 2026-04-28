#pragma once

#include <cstddef>
#include <cstring>
#include <limits>
#include <string_view>
#include <utility>

#include "format_surface.hpp"
#include "printf.hpp"
#include "writer.hpp"

namespace LibXR::Print
{
/**
 * @brief Public return-value contract for the print convenience surface.
 * @brief print 便捷接口的公开返回值约定。
 *
 * Sink-writing helpers such as Write(), FormatTo(), and PrintfTo() return
 * ErrorCode only and report success or failure without exposing a length.
 * 像 Write()、FormatTo()、PrintfTo() 这样的 sink 写入接口只返回
 * ErrorCode，只表达成败，不暴露长度。
 *
 * Bounded-buffer helpers such as FormatIntoBuffer() and PrintfIntoBuffer()
 * return the full formatted size excluding the trailing zero byte, following
 * snprintf-style length semantics even when truncation occurs.
 * 像 FormatIntoBuffer()、PrintfIntoBuffer() 这样的有界缓冲区接口返回
 * 完整格式化长度，不含结尾零字节；即使发生截断，也遵循 snprintf 风格的
 * 完整长度语义。
 *
 * SNPrintf() keeps the same full-size rule but narrows the result to int and
 * returns -1 when the full size no longer fits in int.
 * SNPrintf() 保持同样的完整长度语义，但会把结果收窄为 int；若完整长度
 * 已无法放入 int，则返回 -1。
 */

/**
 * @brief Writes a compiled format into a sink.
 * @brief 将编译后的格式写入输出端。
 * @return Returns the sink write status only. / 仅返回 sink 写入状态。
 */
[[nodiscard]] inline ErrorCode Write(auto& sink, const auto& format, auto&&... args)
{
  using Sink = std::remove_cvref_t<decltype(sink)>;
  using Built = std::remove_cvref_t<decltype(format)>;
  return Writer::template RunArgumentOrder<Sink, Built, Built::ArgumentOrder()>(
      sink, format, std::forward<decltype(args)>(args)...);
}

/**
 * @brief Public convenience wrapper that writes one compiled format into one sink.
 * @brief 面向用户的便捷包装：将一份编译格式写入一个输出端。
 * @return Returns the sink write status only. / 仅返回 sink 写入状态。
 */
[[nodiscard]] inline ErrorCode FormatTo(auto& sink, const auto& format, auto&&... args)
{
  return Write(sink, format, std::forward<decltype(args)>(args)...);
}

/**
 * @brief Writes one brace-style literal directly into one sink.
 * @brief 将一条 brace 风格字面量直接写入一个输出端。
 * @return Returns the sink write status only. / 仅返回 sink 写入状态。
 */
template <Text Source, typename Sink, typename... Args>
[[nodiscard]] inline ErrorCode FormatTo(Sink& sink, Args&&... args)
{
  constexpr LibXR::Format<Source> format{};
  return FormatTo(sink, format, std::forward<Args>(args)...);
}

/**
 * @brief Writes one printf-style literal directly into one sink.
 * @brief 将一条 printf 风格字面量直接写入一个输出端。
 * @return Returns the sink write status only. / 仅返回 sink 写入状态。
 */
template <Text Source, typename Sink, typename... Args>
[[nodiscard]] inline ErrorCode PrintfTo(Sink& sink, Args&&... args)
{
  constexpr auto format = Printf::Build<Source>();
  return FormatTo(sink, format, std::forward<Args>(args)...);
}

/**
 * @brief Formats one compiled format into one bounded char buffer and keeps the
 *        result NUL-terminated when capacity is nonzero.
 * @brief 将一份编译格式写入一个有界 char 缓冲区；当容量非零时，结果始终带结尾零字节。
 *
 * The returned size follows snprintf semantics: it is the full character count
 * that would have been produced without truncation, excluding the trailing zero
 * byte. Truncation is not an error; the retained payload length can be derived
 * from capacity.
 * 返回值采用 snprintf 语义：它表示未截断时本应产生的完整字符数，
 * 不含结尾零字节。截断不算错误；真正保留到 buffer 里的长度可由
 * capacity 推导。
 *
 * When capacity is nonzero, the destination buffer is always kept
 * NUL-terminated.
 * 当 capacity 非零时，目标缓冲区始终保持 NUL 结尾。
 */
[[nodiscard]] inline size_t FormatIntoBuffer(char* buffer, size_t capacity, const auto& format,
                                             auto&&... args)
{
  struct BufferSink
  {
    char* buffer = nullptr;
    size_t retain_limit = 0;
    size_t retained_size = 0;
    size_t total_size = 0;

    [[nodiscard]] ErrorCode Write(std::string_view chunk)
    {
      total_size += chunk.size();

      if (buffer == nullptr || retained_size >= retain_limit)
      {
        return ErrorCode::OK;
      }

      size_t writable = retain_limit - retained_size;
      size_t copy_size = chunk.size() < writable ? chunk.size() : writable;
      if (copy_size > 0)
      {
        std::memcpy(buffer + retained_size, chunk.data(), copy_size);
        retained_size += copy_size;
      }
      return ErrorCode::OK;
    }
  };

  BufferSink sink{
      .buffer = buffer,
      .retain_limit = capacity > 0 ? capacity - 1 : 0,
  };
  auto ec = Write(sink, format, std::forward<decltype(args)>(args)...);
  if (ec != ErrorCode::OK)
  {
    if (capacity > 0 && buffer != nullptr)
    {
      buffer[0] = '\0';
    }
    return 0;
  }

  if (capacity > 0 && buffer != nullptr)
  {
    buffer[sink.retained_size] = '\0';
  }

  return sink.total_size;
}

/**
 * @brief Formats one brace-style literal directly into one bounded char buffer.
 * @brief 将一条 brace 风格字面量直接写入一个有界 char 缓冲区。
 * @return Returns the full formatted size excluding the trailing zero byte.
 *         返回完整格式化长度，不含结尾零字节。
 */
template <Text Source, typename... Args>
[[nodiscard]] inline size_t FormatIntoBuffer(char* buffer, size_t capacity, Args&&... args)
{
  constexpr LibXR::Format<Source> format{};
  return FormatIntoBuffer(buffer, capacity, format, std::forward<Args>(args)...);
}

/**
 * @brief Formats one printf-style literal directly into one bounded char buffer.
 * @brief 将一条 printf 风格字面量直接写入一个有界 char 缓冲区。
 * @return Returns the full formatted size excluding the trailing zero byte.
 *         返回完整格式化长度，不含结尾零字节。
 */
template <Text Source, typename... Args>
[[nodiscard]] inline size_t PrintfIntoBuffer(char* buffer, size_t capacity, Args&&... args)
{
  constexpr auto format = Printf::Build<Source>();
  return FormatIntoBuffer(buffer, capacity, format, std::forward<Args>(args)...);
}

/**
 * @brief snprintf-style wrapper built on top of the compiled-format path.
 * @brief 基于编译格式路径实现的 snprintf 风格包装。
 *
 * Returns the full formatted size excluding the trailing zero byte, or -1 when
 * that size no longer fits in int.
 * 返回完整格式化长度（不含结尾零字节）；若该长度已无法放入 int，则返回 -1。
 * Truncation is not an error and still returns the full size.
 * 截断不算错误，返回值仍然是完整长度。
 */
[[nodiscard]] inline int SNPrintf(char* buffer, size_t capacity, const auto& format,
                                  auto&&... args)
{
  auto total_size =
      FormatIntoBuffer(buffer, capacity, format, std::forward<decltype(args)>(args)...);
  if (total_size > static_cast<size_t>(std::numeric_limits<int>::max()))
  {
    return -1;
  }
  return static_cast<int>(total_size);
}

/**
 * @brief snprintf-style wrapper for one printf-style literal.
 * @brief 面向一条 printf 风格字面量的 snprintf 风格包装。
 * @return Returns the full formatted size excluding the trailing zero byte, or
 *         -1 when that size no longer fits in int.
 *         返回完整格式化长度，不含结尾零字节；若该长度已无法放入 int，
 *         则返回 -1。
 */
template <Text Source, typename... Args>
[[nodiscard]] inline int SNPrintf(char* buffer, size_t capacity, Args&&... args)
{
  constexpr auto format = Printf::Build<Source>();
  return SNPrintf(buffer, capacity, format, std::forward<Args>(args)...);
}

}  // namespace LibXR::Print
