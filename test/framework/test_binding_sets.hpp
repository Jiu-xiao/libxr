/**
 * @file test_binding_sets.hpp
 * @brief binding 测试入口集合定义。 Binding test entry-set definition.
 * @details 作用：
 *          1. 显式列出 binding 测试入口。
 *          2. 提供主 Linux 测试 runner 可直接调用的顺序执行 helper。
 *          Purpose:
 *          1. Explicitly list the binding test entrypoints.
 *          2. Provide a sequential helper directly callable from the main Linux test runner.
 */
#pragma once

#include "test_binding.hpp"
#include "test_case_runner.hpp"

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
  return 0;
}
