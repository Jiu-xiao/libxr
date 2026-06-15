/**
 * @file print_literal_test_common.hpp
 * @brief `print` 前端字面量解析测试共用约束。 Shared literal-resolution constraints for `print` frontend tests.
 * @details 测试项目：
 *          1. 固化 `Format` 参数匹配的编译期约束。
 *          2. 固化 logger 字面量前端自动判定的编译期约束。
 *          Test items:
 *          1. Lock in compile-time argument-matching constraints for `Format`.
 *          2. Lock in compile-time auto-frontend resolution constraints for logger literals.
 */
#pragma once

#include <limits>

#include "libxr.hpp"

static_assert(LibXR::Format<"abc">::ArgumentCount() == 0);
static_assert(LibXR::Format<"{1} {0}">::ArgumentCount() == 2);
static_assert(LibXR::Format<"{:d} {}">::template Matches<int, const char*>());
static_assert(LibXR::Format<"{}">::template Matches<int>());
static_assert(!LibXR::Format<"{}">::template Matches<int, int>());
static_assert(!LibXR::Format<"abc">::template Matches<int>());
static_assert(!LibXR::Format<"{:d} {}">::template Matches<const char*, int>());

using LoggerFrontend = LibXR::Detail::LoggerLiteral::Frontend;
using LoggerResolution = LibXR::Detail::LoggerLiteral::Resolution;

static_assert(LibXR::Detail::LoggerLiteral::ResolveFrontend<LoggerFrontend::Auto,
                                                            "logger {}", int>() ==
              LoggerResolution::Format);
static_assert(LibXR::Detail::LoggerLiteral::ResolveFrontend<LoggerFrontend::Auto,
                                                            "logger %d", int>() ==
              LoggerResolution::Printf);
static_assert(LibXR::Detail::LoggerLiteral::ResolveFrontend<LoggerFrontend::Auto,
                                                            "value=%u", unsigned>() ==
              LoggerResolution::Printf);
static_assert(
    LibXR::Detail::LoggerLiteral::ResolveFrontend<LoggerFrontend::Auto, "{{}}">() ==
    LoggerResolution::Format);
static_assert(
    LibXR::Detail::LoggerLiteral::ResolveFrontend<LoggerFrontend::Auto, "%%">() ==
    LoggerResolution::Printf);
static_assert(
    LibXR::Detail::LoggerLiteral::ResolveFrontend<LoggerFrontend::Auto, "plain text">() ==
    LoggerResolution::Format);
static_assert(LibXR::Detail::LoggerLiteral::ResolveFrontend<LoggerFrontend::Auto,
                                                            "plain text", int>() ==
              LoggerResolution::None);
static_assert(LibXR::Detail::LoggerLiteral::ResolveFrontend<LoggerFrontend::Auto,
                                                            "logger {}", int, int>() ==
              LoggerResolution::None);
#if LIBXR_PRINT_INTEGER_ENABLE_64BIT
static_assert(LibXR::Detail::LoggerLiteral::ResolveFrontend<
                  LoggerFrontend::Auto, "frame=%llu", unsigned long long>() ==
              LoggerResolution::Printf);
#else
static_assert(LibXR::Detail::LoggerLiteral::ResolveFrontend<
                  LoggerFrontend::Auto, "frame=%llu", unsigned long long>() ==
              LoggerResolution::None);
#endif
static_assert(
    LibXR::Detail::LoggerLiteral::ResolveFrontend<LoggerFrontend::Auto, "{} %d", int>() ==
    LoggerResolution::Ambiguous);
static_assert(LibXR::Detail::LoggerLiteral::ResolveFrontend<LoggerFrontend::Auto, "%s {}",
                                                            const char*>() ==
              LoggerResolution::Ambiguous);
static_assert(LibXR::Detail::LoggerLiteral::ResolveFrontend<LoggerFrontend::Auto,
                                                            "{0}%1$d", int>() ==
              LoggerResolution::Ambiguous);
static_assert(
    LibXR::Detail::LoggerLiteral::ResolveFrontend<LoggerFrontend::Auto, "{%">() ==
    LoggerResolution::None);
static_assert(
    LibXR::Detail::LoggerLiteral::ResolveFrontend<LoggerFrontend::Auto, "{%d}", int>() ==
    LoggerResolution::Printf);
static_assert(LibXR::Detail::LoggerLiteral::ResolveFrontend<LoggerFrontend::Auto,
                                                            "{123abc} %d", int>() ==
              LoggerResolution::Printf);
