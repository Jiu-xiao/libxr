/**
 * @file test_float_fixed_only.cpp
 * @brief 验证仅 fixed 浮点族开启时的前端门控。 Verify frontend gates when only fixed
 * float formatting is enabled.
 * @details 测试项目：
 *          1. brace 与 printf 都接受 fixed 浮点格式。
 *          2. scientific 与 general 浮点族被拒绝。
 *          3. `double` 存储关闭时，brace 的 `double` 参数仍走 F32 打包路径。
 *          Test items:
 *          1. Brace and printf frontends both accept fixed float formatting.
 *          2. Scientific and general float families are rejected.
 *          3. With double-backed storage disabled, brace `double` arguments lower
 *             through the F32 pack path.
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

static_assert(LibXR::Format<"{}">::Matches<float>());
static_assert(LibXR::Format<"{:.2f}">::Matches<float>());
static_assert(LibXR::Format<"{:.2F}">::Matches<double>());
static_assert(!LibXR::Format<"{:.2e}">::Matches<float>());
static_assert(!LibXR::Format<"{:g}">::Matches<double>());
static_assert(!LibXR::Format<"{:.2f}">::Matches<long double>());
static_assert(FormatCompileError<"{:.2e}", float>() == Error::ArgumentTypeMismatch);
static_assert(FormatCompileError<"{:g}", double>() == Error::ArgumentTypeMismatch);
static_assert(FormatCompileError<"{:.2f}", long double>() == Error::ArgumentTypeMismatch);

static_assert(PrintfSourceError<"%f">() == Printf::Error::None);
static_assert(PrintfSourceError<"%F">() == Printf::Error::None);
static_assert(PrintfSourceError<"%e">() == Printf::Error::InvalidSpecifier);
static_assert(PrintfSourceError<"%g">() == Printf::Error::InvalidSpecifier);
static_assert(PrintfSourceError<"%Lf">() == Printf::Error::InvalidSpecifier);

using DoubleFixed = LibXR::Format<"{:.2f}">::Compiled<double>;
static_assert(DoubleFixed::ArgumentList()[0].pack == FormatPackKind::F32);
