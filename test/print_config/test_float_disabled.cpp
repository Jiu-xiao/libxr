/**
 * @file test_float_disabled.cpp
 * @brief 浮点总开关关闭 profile 的编译期门控测试。 Compile-time gate test for the
 * float-master-disabled profile.
 * @details
 * 1. `LIBXR_PRINT_ENABLE_FLOAT=0` 关闭所有浮点格式。
 * 2. fixed/scientific/general 子开关显式设为 1 也不能绕过总开关。
 * 3. 整数、文本和指针保留为非目标开关 sanity check。
 */
#include "print_config_reset.hpp"

#define LIBXR_PRINT_ENABLE_INTEGER 1
#define LIBXR_PRINT_ENABLE_TEXT 1
#define LIBXR_PRINT_ENABLE_POINTER 1
#define LIBXR_PRINT_ENABLE_FLOAT 0
#define LIBXR_PRINT_INTEGER_ENABLE_BASE8_16 1
#define LIBXR_PRINT_INTEGER_ENABLE_64BIT 1
#define LIBXR_PRINT_FLOAT_ENABLE_FIXED 1
#define LIBXR_PRINT_FLOAT_ENABLE_DOUBLE 1
#define LIBXR_PRINT_FLOAT_ENABLE_SCIENTIFIC 1
#define LIBXR_PRINT_FLOAT_ENABLE_GENERAL 1
#define LIBXR_PRINT_FLOAT_ENABLE_LONG_DOUBLE 1
#define LIBXR_PRINT_ENABLE_WIDTH 1
#define LIBXR_PRINT_ENABLE_PRECISION 1
#define LIBXR_PRINT_ENABLE_ALTERNATE 1
#define LIBXR_PRINT_ENABLE_EXPLICIT_ARGUMENT_INDEXING 1

#include "print_config_probe.hpp"

using LibXR::Print::Printf;
using LibXR::Print::Config::enable_float;
using LibXR::Print::Config::enable_float_fixed;
using LibXR::Print::Config::enable_float_general;
using LibXR::Print::Config::enable_float_scientific;
using LibXR::Print::Detail::FormatFrontend::Error;
using namespace LibXRPrintConfigTest;

static_assert(!enable_float);
static_assert(!enable_float_fixed);
static_assert(!enable_float_scientific);
static_assert(!enable_float_general);

// Brace 前端：所有浮点 presentation 都被总开关挡住。
// Brace frontend: every float presentation is blocked by the master switch.
static_assert(!LibXR::Format<"{}">::Matches<float>());
static_assert(!LibXR::Format<"{:.1f}">::Matches<float>());
static_assert(!LibXR::Format<"{:.1e}">::Matches<double>());
static_assert(!LibXR::Format<"{:g}">::Matches<double>());
static_assert(FormatCompileError<"{:.1f}", float>() == Error::ArgumentTypeMismatch);
static_assert(FormatCompileError<"{:.1e}", double>() == Error::ArgumentTypeMismatch);
static_assert(FormatCompileError<"{:g}", double>() == Error::ArgumentTypeMismatch);

// Printf 前端：浮点说明符在 source analysis 阶段即不可用。
// Printf frontend: float specifiers are unavailable during source analysis.
static_assert(PrintfSourceError<"%f">() == Printf::Error::InvalidSpecifier);
static_assert(PrintfSourceError<"%e">() == Printf::Error::InvalidSpecifier);
static_assert(PrintfSourceError<"%g">() == Printf::Error::InvalidSpecifier);
static_assert(PrintfSourceError<"%Lf">() == Printf::Error::InvalidSpecifier);

// 非浮点格式仍可用，确认不是探针或 include 配置整体坏掉。
// Non-float formats remain usable, proving the profile and probes are otherwise live.
static_assert(LibXR::Format<"{}">::Matches<int>());
static_assert(LibXR::Format<"{:s}">::Matches<const char*>());
static_assert(LibXR::Format<"{:p}">::Matches<const void*>());
static_assert(PrintfSourceError<"%d %s %p">() == Printf::Error::None);
