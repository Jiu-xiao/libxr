/**
 * @file print_config_probe.hpp
 * @brief `print` 配置矩阵的编译期探针。 Compile-time probes for the `print` config
 * matrix.
 * @details
 * 1. source analysis 检查被关闭的语法。
 * 2. compile backend 检查源串合法但参数类型被配置门拒绝的情况。
 * 3. 不执行 writer，不混入默认 profile 的运行时文本对照测试。
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
