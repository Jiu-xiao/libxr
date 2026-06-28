/**
 * @file test_float_precision_limit.cpp
 * @brief 验证浮点精度上限配置的前端门控。 Verify frontend gates for the float precision
 * limit.
 * @details 测试项目：
 *          1. brace 与 printf 都接受不超过上限的显式浮点精度。
 *          2. brace 与 printf 都拒绝超过 `LIBXR_PRINT_FLOAT_MAX_PRECISION` 的精度。
 *          3. `LIBXR_PRINT_FLOAT_MAX_INTEGER_DIGITS` 只确认配置值传递，不在这里测试
 *             writer 运行时边界。
 *          Test items:
 *          1. Brace and printf frontends accept explicit float precision at or
 *             below the configured limit.
 *          2. Brace and printf frontends reject precision above
 *             `LIBXR_PRINT_FLOAT_MAX_PRECISION`.
 *          3. `LIBXR_PRINT_FLOAT_MAX_INTEGER_DIGITS` is checked only as config
 *             propagation; writer runtime boundaries stay outside this matrix.
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

static_assert(PrintfSourceError<"%.0f">() == Printf::Error::None);
static_assert(PrintfSourceError<"%.2f">() == Printf::Error::None);
static_assert(PrintfSourceError<"%.2e">() == Printf::Error::None);
static_assert(PrintfSourceError<"%.2g">() == Printf::Error::None);
static_assert(PrintfSourceError<"%.3f">() == Printf::Error::FloatPrecisionLimitExceeded);
static_assert(PrintfSourceError<"%.3e">() == Printf::Error::FloatPrecisionLimitExceeded);
static_assert(PrintfSourceError<"%.3g">() == Printf::Error::FloatPrecisionLimitExceeded);
