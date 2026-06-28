/**
 * @file test_float_fixed_only.cpp
 * @brief 仅 fixed 浮点开启 profile 的编译期门控测试。 Compile-time gate test for the
 * fixed-only float profile.
 * @details
 * 1. `%f` / `{:f}` 可用。
 * 2. scientific、general 和 long double 被拒绝。
 * 3. `LIBXR_PRINT_FLOAT_ENABLE_DOUBLE=0` 时 brace `double` 降到 F32 打包。
 */
#include "print_config_reset.hpp"

#define LIBXR_PRINT_ENABLE_INTEGER 1
#define LIBXR_PRINT_ENABLE_TEXT 1
#define LIBXR_PRINT_ENABLE_POINTER 1
#define LIBXR_PRINT_ENABLE_FLOAT 1
#define LIBXR_PRINT_INTEGER_ENABLE_BASE8_16 1
#define LIBXR_PRINT_INTEGER_ENABLE_64BIT 1
#define LIBXR_PRINT_FLOAT_ENABLE_FIXED 1
#define LIBXR_PRINT_FLOAT_ENABLE_DOUBLE 0
#define LIBXR_PRINT_FLOAT_ENABLE_SCIENTIFIC 0
#define LIBXR_PRINT_FLOAT_ENABLE_GENERAL 0
#define LIBXR_PRINT_FLOAT_ENABLE_LONG_DOUBLE 0
#define LIBXR_PRINT_ENABLE_WIDTH 1
#define LIBXR_PRINT_ENABLE_PRECISION 1
#define LIBXR_PRINT_ENABLE_ALTERNATE 1
#define LIBXR_PRINT_ENABLE_EXPLICIT_ARGUMENT_INDEXING 1

#include "print_config_probe.hpp"

using LibXR::Print::FormatPackKind;
using LibXR::Print::Printf;
using LibXR::Print::Config::enable_float_double;
using LibXR::Print::Detail::FormatFrontend::Error;
using namespace LibXRPrintConfigTest;

static_assert(!enable_float_double);

// Brace 前端：fixed 可用，scientific/general/long double 被 profile 拒绝。
// Brace frontend: fixed is accepted; scientific/general/long double are rejected.
static_assert(LibXR::Format<"{}">::Matches<float>());
static_assert(LibXR::Format<"{:.2f}">::Matches<float>());
static_assert(LibXR::Format<"{:.2F}">::Matches<double>());
static_assert(!LibXR::Format<"{:.2e}">::Matches<float>());
static_assert(!LibXR::Format<"{:g}">::Matches<double>());
static_assert(!LibXR::Format<"{:.2f}">::Matches<long double>());
static_assert(FormatCompileError<"{:.2e}", float>() == Error::ArgumentTypeMismatch);
static_assert(FormatCompileError<"{:g}", double>() == Error::ArgumentTypeMismatch);
static_assert(FormatCompileError<"{:.2f}", long double>() == Error::ArgumentTypeMismatch);

// Printf 前端：fixed 说明符存在，其它浮点族在 source analysis 阶段不可用。
// Printf frontend: fixed specifiers exist; other float families are unavailable.
static_assert(PrintfSourceError<"%f">() == Printf::Error::None);
static_assert(PrintfSourceError<"%F">() == Printf::Error::None);
static_assert(PrintfSourceError<"%e">() == Printf::Error::InvalidSpecifier);
static_assert(PrintfSourceError<"%g">() == Printf::Error::InvalidSpecifier);
static_assert(PrintfSourceError<"%Lf">() == Printf::Error::InvalidSpecifier);

// double 存储关闭时，brace `double` 参数仍可用，但按 F32 打包。
// With double storage disabled, brace `double` remains accepted but packs as F32.
using DoubleFixed = LibXR::Format<"{:.2f}">::Compiled<double>;
static_assert(DoubleFixed::ArgumentList()[0].pack == FormatPackKind::F32);
