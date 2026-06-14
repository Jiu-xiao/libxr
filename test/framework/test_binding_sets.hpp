/**
 * @file test_binding_sets.hpp
 * @brief binding 测试固定运行集合定义。 Fixed runtime-set definitions for binding tests.
 * @details 作用：
 *          1. 显式列出 binding 测试入口。
 *          2. 强制 binding 只允许在 `full_os` 集合下运行。
 *          Purpose:
 *          1. Explicitly list the binding test entrypoints.
 *          2. Enforce that binding tests run only under the `full_os` set.
 */
#pragma once

#include "test_binding.hpp"
#include "test_case_runner.hpp"
#include "test_runtime_set.hpp"

inline constexpr TestCase kBindingTests[] = {
    {"print_binding", &RunVoidEntry<test_print_binding>, false},
    {"database_binding_sequential", &RunVoidEntry<test_database_binding_sequential>, false},
    {"database_binding_raw", &RunVoidEntry<test_database_binding_raw>, false},
};

inline int RunBindingTestSet()
{
  for (const auto& test_case : kBindingTests)
  {
    run_test_case(test_case);
  }
  return ErrorCodeToExitStatus(LibXR::ErrorCode::OK);
}
