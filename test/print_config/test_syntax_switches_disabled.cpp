/**
 * @file test_syntax_switches_disabled.cpp
 * @brief 验证宽度、精度、备用格式和显式索引语法关闭时的前端门控。 Verify frontend gates
 * for disabled width, precision, alternate, and explicit-index syntax.
 * @details 测试项目：
 *          1. 无宽度/精度/备用格式/显式索引的基础格式继续可用。
 *          2. brace 前端 source analysis 拒绝被关闭的语法。
 *          3. printf 前端 source analysis 拒绝被关闭的语法。
 *          Test items:
 *          1. Basic formats without width, precision, alternate form, or
 *             explicit indexing remain accepted.
 *          2. Brace source analysis rejects disabled syntax.
 *          3. Printf source analysis rejects disabled syntax.
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
#define LIBXR_PRINT_ENABLE_WIDTH 0
#define LIBXR_PRINT_ENABLE_PRECISION 0
#define LIBXR_PRINT_ENABLE_ALTERNATE 0
#define LIBXR_PRINT_ENABLE_EXPLICIT_ARGUMENT_INDEXING 0

#include "print_config_probe.hpp"

using LibXR::Print::Printf;
using LibXR::Print::Detail::FormatFrontend::Error;
using namespace LibXRPrintConfigTest;

static_assert(LibXR::Format<"{}">::Matches<int>());
static_assert(LibXR::Format<"{:d}">::Matches<int>());
static_assert(LibXR::Format<"{:f}">::Matches<float>());
static_assert(PrintfSourceError<"%d %f %%">() == Printf::Error::None);

static_assert(FormatSourceError<"{:5d}">() == Error::InvalidSpecifier);
static_assert(FormatSourceError<"{:.2f}">() == Error::InvalidSpecifier);
static_assert(FormatSourceError<"{:#x}">() == Error::InvalidSpecifier);
static_assert(FormatSourceError<"{0}">() == Error::ManualIndexingDisabled);
static_assert(FormatSourceError<"{1:d}">() == Error::ManualIndexingDisabled);

static_assert(PrintfSourceError<"%5d">() == Printf::Error::InvalidSpecifier);
static_assert(PrintfSourceError<"%.2f">() == Printf::Error::InvalidSpecifier);
static_assert(PrintfSourceError<"%#x">() == Printf::Error::InvalidSpecifier);
static_assert(PrintfSourceError<"%1$d">() == Printf::Error::PositionalArgumentDisabled);
