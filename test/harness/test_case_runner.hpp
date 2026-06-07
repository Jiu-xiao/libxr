/**
 * @file test_case_runner.hpp
 * @brief base/runtime 测试 case 运行 helper。 Case-running helpers for the base/runtime test harness.
 * @details 作用：
 *          1. 定义 `TestCase`、`TEST_STEP` 和单 case 隔离执行 helper。
 *          2. 集中 `fork()` 隔离逻辑，避免主 runner 反复展开样板代码。
 *          Purpose:
 *          1. Define `TestCase`, `TEST_STEP`, and the single-case isolated execution helper.
 *          2. Centralize the `fork()` isolation logic so the main runner avoids repeated boilerplate.
 */
#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>

#include "libxr.hpp"
#include "logger.hpp"

inline const char* test_name = nullptr;

#define TEST_STEP(_arg)                                \
  do                                                   \
  {                                                    \
    test_name = _arg;                                  \
    if (test_name)                                     \
    {                                                  \
      XR_LOG_PASS("\tTest [%s] Passed.\n", test_name); \
    }                                                  \
  } while (0)

bool equal(double a, double b);

struct TestCase
{
  const char* name;
  void (*function)();
  bool isolated;
};

/**
 * @brief 辅助函数 `run_test_case`。 Helper function `run_test_case`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
inline void run_test_case(const TestCase& test_case)
{
  test_name = test_case.name;

  if (!test_case.isolated)
  {
    test_case.function();
    return;
  }

  pid_t child = fork();
  ASSERT(child >= 0);

  if (child == 0)
  {
    test_case.function();
    _exit(0);
  }

  int status = 0;
  ASSERT(waitpid(child, &status, 0) == child);
  ASSERT(WIFEXITED(status));
  ASSERT(WEXITSTATUS(status) == 0);
}
