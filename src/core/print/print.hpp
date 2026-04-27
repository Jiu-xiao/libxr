#pragma once

#include <utility>

#include "printf.hpp"
#include "writer.hpp"

namespace LibXR::Print
{
/**
 * @brief Writes a compiled format into a sink.
 * @brief 将编译后的格式写入输出端。
 */
[[nodiscard]] inline ErrorCode Write(auto& sink, const auto& format, auto&&... args)
{
  return Writer::Write(sink, format, std::forward<decltype(args)>(args)...);
}
}  // namespace LibXR::Print
