/**
 * @file test_integer_text_disabled.cpp
 * @brief 验证整数与文本总开关关闭时的前端门控。 Verify frontend gates when integer and
 * text support are disabled.
 * @details 测试项目：
 *          1. brace 前端拒绝整数、字符和字符串参数。
 *          2. printf 前端把 `%d`、`%c`、`%s` 识别为被禁用的说明符。
 *          3. 同一配置下指针和定点浮点仍可通过，确认测试只覆盖目标开关。
 *          Test items:
 *          1. Brace formatting rejects integer, character, and string arguments.
 *          2. Printf analysis reports `%d`, `%c`, and `%s` as disabled specifiers.
 *          3. Pointer and fixed-float conversions still pass in this profile, so
 *             the assertions stay scoped to the targeted switches.
 */
#include "print_config_reset.hpp"

#define LIBXR_PRINT_ENABLE_INTEGER 0
#define LIBXR_PRINT_ENABLE_TEXT 0
#define LIBXR_PRINT_ENABLE_POINTER 1
#define LIBXR_PRINT_ENABLE_FLOAT 1
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
using LibXR::Print::Detail::FormatFrontend::Error;
using namespace LibXRPrintConfigTest;

static_assert(!LibXR::Format<"{}">::Matches<int>());
static_assert(!LibXR::Format<"{:c}">::Matches<char>());
static_assert(!LibXR::Format<"{:s}">::Matches<const char*>());
static_assert(FormatCompileError<"{}", int>() == Error::ArgumentTypeMismatch);
static_assert(FormatCompileError<"{:c}", char>() == Error::ArgumentTypeMismatch);
static_assert(FormatCompileError<"{:s}", const char*>() == Error::ArgumentTypeMismatch);

static_assert(PrintfSourceError<"%d">() == Printf::Error::InvalidSpecifier);
static_assert(PrintfSourceError<"%c">() == Printf::Error::InvalidSpecifier);
static_assert(PrintfSourceError<"%s">() == Printf::Error::InvalidSpecifier);

static_assert(LibXR::Format<"{:p}">::Matches<const void*>());
static_assert(LibXR::Format<"{:.1f}">::Matches<float>());
static_assert(PrintfSourceError<"%p">() == Printf::Error::None);
static_assert(PrintfSourceError<"%.1f">() == Printf::Error::None);
