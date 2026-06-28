/**
 * @file print_config_probe.hpp
 * @brief `print` 配置矩阵的编译期探针。 Compile-time probes for the `print` config
 * matrix.
 * @details 测试项目：
 *          1. 用内部前端分析接口检查禁用语法产生的错误类别。
 *          2. 用共享编译后端检查源串合法但参数/功能门不匹配的错误类别。
 *          3. 不执行 writer，不把配置矩阵与运行时文本对照测试混在一起。
 *          Test items:
 *          1. Inspect disabled-syntax errors through the internal frontend
 *             analysis interfaces.
 *          2. Inspect source-valid argument/gate failures through the shared
 *             compile backend.
 *          3. Avoid running the writer so this matrix stays separate from
 *             runtime text-comparison tests.
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
