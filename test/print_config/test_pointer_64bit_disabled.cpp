/**
 * @file test_pointer_64bit_disabled.cpp
 * @brief 验证指针与 64 位整数关闭时的前端门控。 Verify frontend gates when pointer and
 * 64-bit integer support are disabled.
 * @details 测试项目：
 *          1. brace 与 printf 都拒绝指针格式。
 *          2. brace 与 printf 都拒绝需要 64 位存储的整数格式。
 *          3. 32 位整数格式继续可用，确认没有误关整数总开关。
 *          Test items:
 *          1. Brace and printf frontends both reject pointer formatting.
 *          2. Brace and printf frontends both reject integer formats requiring
 *             64-bit packed storage.
 *          3. 32-bit integer formats remain accepted.
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

static_assert(PrintfSourceError<"%p">() == Printf::Error::InvalidSpecifier);
static_assert(PrintfSourceError<"%llu">() == Printf::Error::InvalidLength);
static_assert(PrintfSourceError<"%llx">() == Printf::Error::InvalidLength);

static_assert(LibXR::Format<"{}">::Matches<int32_t>());
static_assert(LibXR::Format<"{:x}">::Matches<uint32_t>());
static_assert(PrintfSourceError<"%d %u %x">() == Printf::Error::None);
