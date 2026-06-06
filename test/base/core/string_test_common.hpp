/**
 * @file string_test_common.hpp
 * @brief 运行时字符串测试共用编译期约束与头文件。 Shared compile-time constraints and includes for runtime-string tests.
 * @details 测试项目：
 *          1. 汇总运行时字符串测试所需头文件和类型工具。
 *          2. 固化 `Reformat` / `Reprintf` 接受类型的编译期约束。
 *          Test items:
 *          1. Gather the headers and type utilities used by runtime-string tests.
 *          2. Lock in the compile-time accepted-type constraints for `Reformat` / `Reprintf`.
 */
#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

#include "libxr_def.hpp"
#include "libxr_string.hpp"
#include "test.hpp"

template <typename T>
constexpr bool accepts_uint32_reformat =
    requires(LibXR::RuntimeStringView<"stamp_{}", std::uint32_t>& view, T value)
{
  view.Reformat(value);
};

template <typename T>
constexpr bool accepts_uint_reprintf =
    requires(LibXR::RuntimeStringView<"stamp_%u", unsigned int>& view, T value)
{
  view.Reprintf(value);
};

static_assert(accepts_uint32_reformat<std::uint32_t>);
static_assert(!accepts_uint32_reformat<std::uint64_t>);
static_assert(accepts_uint_reprintf<unsigned int>);
static_assert(!accepts_uint_reprintf<std::uint64_t>);
static_assert(std::is_move_constructible_v<LibXR::RuntimeStringView<>>);
static_assert(!std::is_move_assignable_v<LibXR::RuntimeStringView<>>);
