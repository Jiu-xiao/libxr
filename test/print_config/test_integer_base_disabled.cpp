/**
 * @file test_integer_base_disabled.cpp
 * @brief 非十进制整数关闭 profile 的编译期门控测试。 Compile-time gate test for the
 * non-decimal-integer-disabled profile.
 * @details
 * 1. `LIBXR_PRINT_INTEGER_ENABLE_BASE8_16=0` 关闭二/八/十六进制整数格式。
 * 2. 十进制整数必须继续可用。
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

// Brace 前端：十进制可用，非十进制 presentation 被参数门拒绝。
// Brace frontend: decimal works; non-decimal presentations are rejected by the argument
// gate.
static_assert(LibXR::Format<"{}">::Matches<int>());
static_assert(LibXR::Format<"{:d}">::Matches<unsigned int>());
static_assert(!LibXR::Format<"{:b}">::Matches<unsigned int>());
static_assert(!LibXR::Format<"{:o}">::Matches<unsigned int>());
static_assert(!LibXR::Format<"{:x}">::Matches<unsigned int>());
static_assert(FormatCompileError<"{:b}", unsigned int>() == Error::ArgumentTypeMismatch);
static_assert(FormatCompileError<"{:o}", unsigned int>() == Error::ArgumentTypeMismatch);
static_assert(FormatCompileError<"{:x}", unsigned int>() == Error::ArgumentTypeMismatch);

// Printf 前端：对应说明符在 source analysis 阶段不可用。
// Printf frontend: matching specifiers are unavailable during source analysis.
static_assert(PrintfSourceError<"%d">() == Printf::Error::None);
static_assert(PrintfSourceError<"%u">() == Printf::Error::None);
static_assert(PrintfSourceError<"%b">() == Printf::Error::InvalidSpecifier);
static_assert(PrintfSourceError<"%o">() == Printf::Error::InvalidSpecifier);
static_assert(PrintfSourceError<"%x">() == Printf::Error::InvalidSpecifier);
