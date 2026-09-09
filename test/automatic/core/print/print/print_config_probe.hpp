/**
 * @file print_config_probe.hpp
 * @brief 打印配置编译检查使用的错误查询函数。 /
 * Error-query helpers for print configuration compile checks.
 *
 * 读取 printf 和花括号格式的解析及参数编译结果。
 * Exposes printf and brace-format parsing and argument compilation results.
 */

#pragma once

#include "print.hpp"

namespace LibXRPrintConfigTest
{
template <LibXR::Print::Text Source>
[[nodiscard]] consteval LibXR::Print::Printf::Error PrintfSourceError()
{
  return LibXR::Print::Detail::PrintfCompile::Analyze<Source>().error;
}

template <LibXR::Print::Text Source>
[[nodiscard]] consteval LibXR::Print::Detail::FormatFrontend::Error FormatSourceError()
{
  return LibXR::Print::Detail::FormatFrontend::Analyze<Source>().error;
}

template <LibXR::Print::Text Source, typename... Args>
[[nodiscard]] consteval LibXR::Print::Detail::FormatFrontend::Error FormatCompileError()
{
  using Frontend = LibXR::Print::Detail::FormatFrontend::Compiler<Source, Args...>;
  return LibXR::Print::FormatCompiler<Frontend>::Compile().compile_error;
}
}  // namespace LibXRPrintConfigTest
