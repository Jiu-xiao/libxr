/**
 * @file test_float_precision_limit.cpp
 * @brief 浮点精度上限 profile 的编译期门控测试。 Compile-time gate test for the float
 * precision-limit profile.
 * @details
 * 1. 显式精度小于等于 `LIBXR_PRINT_FLOAT_MAX_PRECISION` 时允许编译。
 * 2. 超过上限时返回精度上限错误。
 * 3. `LIBXR_PRINT_FLOAT_MAX_INTEGER_DIGITS` 这里只确认配置值传递。
 * 4. writer 运行时边界不在本矩阵源里测试。
 */
#include "print_config_reset.hpp"

#define LIBXR_PRINT_ENABLE_INTEGER 1
#define LIBXR_PRINT_ENABLE_TEXT 1
#define LIBXR_PRINT_ENABLE_POINTER 1
#define LIBXR_PRINT_ENABLE_FLOAT 1
#define LIBXR_PRINT_INTEGER_ENABLE_BASE8_16 1
#define LIBXR_PRINT_INTEGER_ENABLE_64BIT 1
#define LIBXR_PRINT_FLOAT_ENABLE_FIXED 1
#define LIBXR_PRINT_FLOAT_ENABLE_DOUBLE 1
#define LIBXR_PRINT_FLOAT_ENABLE_SCIENTIFIC 1
#define LIBXR_PRINT_FLOAT_ENABLE_GENERAL 1
#define LIBXR_PRINT_FLOAT_ENABLE_LONG_DOUBLE 1
#define LIBXR_PRINT_FLOAT_MAX_PRECISION 2
#define LIBXR_PRINT_FLOAT_MAX_INTEGER_DIGITS 5
#define LIBXR_PRINT_ENABLE_WIDTH 1
#define LIBXR_PRINT_ENABLE_PRECISION 1
#define LIBXR_PRINT_ENABLE_ALTERNATE 1
#define LIBXR_PRINT_ENABLE_EXPLICIT_ARGUMENT_INDEXING 1

#include "print_config_probe.hpp"

using LibXR::Print::Printf;
using LibXR::Print::Config::max_float_integer_digits;
using LibXR::Print::Config::max_float_precision;
using LibXR::Print::Detail::FormatFrontend::Error;
using namespace LibXRPrintConfigTest;

static_assert(max_float_precision == 2);
static_assert(max_float_integer_digits == 5);

// Brace 前端：0..2 位精度可用，3 位精度被配置上限拒绝。
// Brace frontend: precision 0..2 is accepted; precision 3 exceeds the configured limit.
static_assert(LibXR::Format<"{:.0f}">::Matches<float>());
static_assert(LibXR::Format<"{:.2f}">::Matches<float>());
static_assert(LibXR::Format<"{:.2e}">::Matches<double>());
static_assert(LibXR::Format<"{:.2g}">::Matches<double>());
static_assert(!LibXR::Format<"{:.3f}">::Matches<float>());
static_assert(!LibXR::Format<"{:.3e}">::Matches<double>());
static_assert(!LibXR::Format<"{:.3g}">::Matches<double>());
static_assert(FormatCompileError<"{:.3f}", float>() ==
              Error::FloatPrecisionLimitExceeded);
static_assert(FormatCompileError<"{:.3e}", double>() ==
              Error::FloatPrecisionLimitExceeded);
static_assert(FormatCompileError<"{:.3g}", double>() ==
              Error::FloatPrecisionLimitExceeded);

// Printf 前端：同一上限应在 printf source analysis 中得到相同错误类别。
// Printf frontend: the same limit should surface as the corresponding source-analysis
// error.
static_assert(PrintfSourceError<"%.0f">() == Printf::Error::None);
static_assert(PrintfSourceError<"%.2f">() == Printf::Error::None);
static_assert(PrintfSourceError<"%.2e">() == Printf::Error::None);
static_assert(PrintfSourceError<"%.2g">() == Printf::Error::None);
static_assert(PrintfSourceError<"%.3f">() == Printf::Error::FloatPrecisionLimitExceeded);
static_assert(PrintfSourceError<"%.3e">() == Printf::Error::FloatPrecisionLimitExceeded);
static_assert(PrintfSourceError<"%.3g">() == Printf::Error::FloatPrecisionLimitExceeded);
