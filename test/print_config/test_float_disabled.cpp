/**
 * @file test_float_disabled.cpp
 * @brief 验证浮点总开关关闭时的前端门控。 Verify frontend gates when the float master
 * switch is disabled.
 * @details 测试项目：
 *          1. 子开关定义为开启时，`LIBXR_PRINT_ENABLE_FLOAT=0` 仍会关闭所有浮点族。
 *          2. brace 前端拒绝 fixed、scientific 和 general 浮点展示。
 *          3. printf 前端把 `%f`、`%e`、`%g` 识别为被禁用的说明符。
 *          4. 整数、文本和指针仍可通过，确认不是整体配置失效。
 *          Test items:
 *          1. `LIBXR_PRINT_ENABLE_FLOAT=0` disables every float family even when
 *             the individual float switches are set to one.
 *          2. Brace formatting rejects fixed, scientific, and general float
 * presentations.
 *          3. Printf analysis reports `%f`, `%e`, and `%g` as disabled specifiers.
 *          4. Integer, text, and pointer conversions remain accepted.
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

static_assert(!LibXR::Format<"{}">::Matches<float>());
static_assert(!LibXR::Format<"{:.1f}">::Matches<float>());
static_assert(!LibXR::Format<"{:.1e}">::Matches<double>());
static_assert(!LibXR::Format<"{:g}">::Matches<double>());
static_assert(FormatCompileError<"{:.1f}", float>() == Error::ArgumentTypeMismatch);
static_assert(FormatCompileError<"{:.1e}", double>() == Error::ArgumentTypeMismatch);
static_assert(FormatCompileError<"{:g}", double>() == Error::ArgumentTypeMismatch);

static_assert(PrintfSourceError<"%f">() == Printf::Error::InvalidSpecifier);
static_assert(PrintfSourceError<"%e">() == Printf::Error::InvalidSpecifier);
static_assert(PrintfSourceError<"%g">() == Printf::Error::InvalidSpecifier);
static_assert(PrintfSourceError<"%Lf">() == Printf::Error::InvalidSpecifier);

static_assert(LibXR::Format<"{}">::Matches<int>());
static_assert(LibXR::Format<"{:s}">::Matches<const char*>());
static_assert(LibXR::Format<"{:p}">::Matches<const void*>());
static_assert(PrintfSourceError<"%d %s %p">() == Printf::Error::None);
