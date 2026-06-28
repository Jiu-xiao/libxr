/**
 * @file test_syntax_switches_disabled.cpp
 * @brief 宽度、精度、备用格式和显式索引关闭 profile 的编译期门控测试。 Compile-time gate
 * test for the width/precision/alternate/explicit-index-disabled profile.
 * @details 这些开关关闭的是源字符串语法，不是参数类型；因此本文件直接检查 brace 和 printf
 *          source analysis 的错误类别，同时保留无这些语法的基础格式作为 sanity check。
 * These switches disable source syntax rather than argument types, so this file checks
 * brace and printf source-analysis errors directly while keeping basic formats without
 * these syntax features as sanity checks.
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

// 基础格式不依赖宽度、精度、alternate 或显式索引，仍应可用。
// Basic formats do not require width, precision, alternate form, or explicit indexing.
static_assert(LibXR::Format<"{}">::Matches<int>());
static_assert(LibXR::Format<"{:d}">::Matches<int>());
static_assert(LibXR::Format<"{:f}">::Matches<float>());
static_assert(PrintfSourceError<"%d %f %%">() == Printf::Error::None);

// Brace source analysis：被关闭的语法在参数匹配前就应返回对应错误。
// Brace source analysis: disabled syntax should fail before argument matching.
static_assert(FormatSourceError<"{:5d}">() == Error::InvalidSpecifier);
static_assert(FormatSourceError<"{:.2f}">() == Error::InvalidSpecifier);
static_assert(FormatSourceError<"{:#x}">() == Error::InvalidSpecifier);
static_assert(FormatSourceError<"{0}">() == Error::ManualIndexingDisabled);
static_assert(FormatSourceError<"{1:d}">() == Error::ManualIndexingDisabled);

// Printf source analysis：宽度、精度、alternate 和 positional 各自返回禁用错误。
// Printf source analysis: width, precision, alternate, and positional syntax each fail.
static_assert(PrintfSourceError<"%5d">() == Printf::Error::InvalidSpecifier);
static_assert(PrintfSourceError<"%.2f">() == Printf::Error::InvalidSpecifier);
static_assert(PrintfSourceError<"%#x">() == Printf::Error::InvalidSpecifier);
static_assert(PrintfSourceError<"%1$d">() == Printf::Error::PositionalArgumentDisabled);
