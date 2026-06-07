/**
 * @file test_runtime_set.hpp
 * @brief 固定测试运行集合定义与解析 helper。 Fixed test runtime-set definitions and parsing helpers.
 * @details 作用：
 *          1. 只定义三类允许的测试运行集合：`bare_metal`、`rtos`、`full_os`。
 *          2. 提供环境变量 `XR_TEST_SET` 的解析与校验。
 *          Purpose:
 *          1. Define only the three allowed runtime sets: `bare_metal`, `rtos`, and `full_os`.
 *          2. Provide parsing and validation for the `XR_TEST_SET` environment variable.
 */
#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>

enum class TestRuntimeSet
{
  BARE_METAL = 0,
  RTOS = 1,
  FULL_OS = 2,
};

inline const char* RuntimeSetName(TestRuntimeSet set)
{
  switch (set)
  {
    case TestRuntimeSet::BARE_METAL:
      return "bare_metal";
    case TestRuntimeSet::RTOS:
      return "rtos";
    case TestRuntimeSet::FULL_OS:
      return "full_os";
  }
  return "unknown";
}

inline bool ParseRuntimeSet(const char* text, TestRuntimeSet& set, FILE* err = stderr)
{
  if (text == nullptr || text[0] == '\0')
  {
    set = TestRuntimeSet::FULL_OS;
    return true;
  }
  if (std::strcmp(text, "bare_metal") == 0)
  {
    set = TestRuntimeSet::BARE_METAL;
    return true;
  }
  if (std::strcmp(text, "rtos") == 0)
  {
    set = TestRuntimeSet::RTOS;
    return true;
  }
  if (std::strcmp(text, "full_os") == 0)
  {
    set = TestRuntimeSet::FULL_OS;
    return true;
  }
  std::fprintf(err, "unknown XR_TEST_SET: %s\n", text);
  return false;
}

inline bool LoadRuntimeSetFromEnv(TestRuntimeSet& set, FILE* err = stderr)
{
  return ParseRuntimeSet(std::getenv("XR_TEST_SET"), set, err);
}

inline int RequireRuntimeSet(TestRuntimeSet actual, TestRuntimeSet required,
                             const char* binary_name)
{
  if (actual != required)
  {
    std::fprintf(stderr, "%s requires XR_TEST_SET=%s, got %s\n", binary_name,
                 RuntimeSetName(required), RuntimeSetName(actual));
    return 1;
  }
  return 0;
}
