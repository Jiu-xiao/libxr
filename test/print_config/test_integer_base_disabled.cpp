/**
 * @file test_integer_base_disabled.cpp
 * @brief 验证非十进制整数族关闭时的前端门控。 Verify frontend gates when non-decimal
 * integer formats are disabled.
 * @details 测试项目：
 *          1. 十进制整数继续可用。
 *          2. brace 前端拒绝二进制、八进制和十六进制展示。
 *          3. printf 前端把 `%b`、`%o`、`%x` 识别为被禁用的说明符。
 *          Test items:
 *          1. Decimal integer formatting remains enabled.
 *          2. Brace formatting rejects binary, octal, and hexadecimal presentations.
 *          3. Printf analysis reports `%b`, `%o`, and `%x` as disabled specifiers.
 */
#include "print_config_reset.hpp"

#define LIBXR_PRINT_ENABLE_INTEGER 1
#define LIBXR_PRINT_ENABLE_TEXT 1
#define LIBXR_PRINT_ENABLE_POINTER 1
#define LIBXR_PRINT_ENABLE_FLOAT 1
#define LIBXR_PRINT_INTEGER_ENABLE_BASE8_16 0
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

static_assert(LibXR::Format<"{}">::Matches<int>());
static_assert(LibXR::Format<"{:d}">::Matches<unsigned int>());
static_assert(!LibXR::Format<"{:b}">::Matches<unsigned int>());
static_assert(!LibXR::Format<"{:o}">::Matches<unsigned int>());
static_assert(!LibXR::Format<"{:x}">::Matches<unsigned int>());
static_assert(FormatCompileError<"{:b}", unsigned int>() == Error::ArgumentTypeMismatch);
static_assert(FormatCompileError<"{:o}", unsigned int>() == Error::ArgumentTypeMismatch);
static_assert(FormatCompileError<"{:x}", unsigned int>() == Error::ArgumentTypeMismatch);

static_assert(PrintfSourceError<"%d">() == Printf::Error::None);
static_assert(PrintfSourceError<"%u">() == Printf::Error::None);
static_assert(PrintfSourceError<"%b">() == Printf::Error::InvalidSpecifier);
static_assert(PrintfSourceError<"%o">() == Printf::Error::InvalidSpecifier);
static_assert(PrintfSourceError<"%x">() == Printf::Error::InvalidSpecifier);
