/**
 * @file test_pointer_64bit_disabled.cpp
 * @brief 编译检查：关闭指针和 64 位整数打印后的格式限制。 /
 * Compile check: format limits with pointer and 64-bit integer printing disabled.
 *
 * 检查对应参数和长度修饰符被拒绝，32 位整数仍可用。
 * Checks rejected arguments and length modifiers while retaining 32-bit integers.
 */

#include "print_config_reset.hpp"

#define LIBXR_PRINT_ENABLE_INTEGER 1
#define LIBXR_PRINT_ENABLE_TEXT 1
#define LIBXR_PRINT_ENABLE_POINTER 0
#define LIBXR_PRINT_ENABLE_FLOAT 1
#define LIBXR_PRINT_INTEGER_ENABLE_BASE8_16 1
#define LIBXR_PRINT_INTEGER_ENABLE_64BIT 0
#define LIBXR_PRINT_FLOAT_ENABLE_FIXED 1
#define LIBXR_PRINT_FLOAT_ENABLE_DOUBLE 1
#define LIBXR_PRINT_FLOAT_ENABLE_SCIENTIFIC 1
#define LIBXR_PRINT_FLOAT_ENABLE_GENERAL 1
#define LIBXR_PRINT_FLOAT_ENABLE_LONG_DOUBLE 1
#define LIBXR_PRINT_ENABLE_WIDTH 1
#define LIBXR_PRINT_ENABLE_PRECISION 1
#define LIBXR_PRINT_ENABLE_ALTERNATE 1
#define LIBXR_PRINT_ENABLE_EXPLICIT_ARGUMENT_INDEXING 1

#include <cstdint>

#include "print_config_probe.hpp"

using LibXR::Print::Printf;
using LibXR::Print::Detail::FormatFrontend::Error;
using namespace LibXRPrintConfigTest;

static_assert(sizeof(unsigned long long) > sizeof(uint32_t));

// Brace 前端：指针和需要 64 位打包的整数参数都不匹配。
// Brace frontend: pointers and integer arguments requiring 64-bit packing do not match.
static_assert(!LibXR::Format<"{}">::Matches<const void*>());
static_assert(!LibXR::Format<"{:p}">::Matches<const void*>());
static_assert(!LibXR::Format<"{}">::Matches<unsigned long long>());
static_assert(!LibXR::Format<"{:x}">::Matches<unsigned long long>());
static_assert(FormatCompileError<"{}", const void*>() == Error::ArgumentTypeMismatch);
static_assert(FormatCompileError<"{:p}", const void*>() == Error::ArgumentTypeMismatch);
static_assert(FormatCompileError<"{}", unsigned long long>() ==
              Error::ArgumentTypeMismatch);
static_assert(FormatCompileError<"{:x}", unsigned long long>() ==
              Error::ArgumentTypeMismatch);

// Printf 前端：指针是禁用说明符，`ll` 长度修饰符是禁用长度族。
// Printf frontend: pointer is a disabled specifier; `ll` is a disabled length family.
static_assert(PrintfSourceError<"%p">() == Printf::Error::InvalidSpecifier);
static_assert(PrintfSourceError<"%llu">() == Printf::Error::InvalidLength);
static_assert(PrintfSourceError<"%llx">() == Printf::Error::InvalidLength);

// 32 位整数仍可用，确认不是整数总开关被关闭。
// 32-bit integers remain accepted, proving the integer master gate is still enabled.
static_assert(LibXR::Format<"{}">::Matches<int32_t>());
static_assert(LibXR::Format<"{:x}">::Matches<uint32_t>());
static_assert(PrintfSourceError<"%d %u %x">() == Printf::Error::None);
