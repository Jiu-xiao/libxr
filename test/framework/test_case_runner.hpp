/**
 * @file test_case_runner.hpp
 * @brief base/runtime 测试 case 运行 helper。 Case-running helpers for the base/runtime
 * test harness.
 * @details 作用：
 *          1. 定义 `TestCase`、`TEST_STEP` 和单 case 隔离执行 helper。
 *          2. 集中 `fork()` 隔离逻辑，避免主 runner 反复展开样板代码。
 *          Purpose:
 *          1. Define `TestCase`, `TEST_STEP`, and the single-case isolated execution
 * helper.
 *          2. Centralize the `fork()` isolation logic so the main runner avoids repeated
 * boilerplate.
 */
#pragma once

#include <cstdio>
#include <cstdlib>

#include "test_assert.hpp"

#if defined(LIBXR_SYSTEM_POSIX_HOST)
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "libxr.hpp"

inline const char* test_name = nullptr;

#define TEST_STEP(_arg)                                         \
  do                                                            \
  {                                                             \
    test_name = _arg;                                           \
    if (test_name)                                              \
    {                                                           \
      std::fprintf(stderr, "\tTest [%s] Passed.\n", test_name); \
      std::fflush(stderr);                                      \
    }                                                           \
  } while (0)

bool equal(double a, double b);

struct TestCase
{
  const char* name;
  int (*function)();
  bool isolated;
};

template <void (*Fn)()>
int RunVoidEntry()
{
  Fn();
  return 0;
}

/**
 * @brief 辅助函数 `run_test_case`。 Helper function `run_test_case`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform,
 * measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate
 * repeated helper logic locally so the main test body stays focused on the test item
 * itself.
 */
inline void run_test_case(const TestCase& test_case)
{
  test_name = test_case.name;

  if (!test_case.isolated)
  {
    TEST_ASSERT(test_case.function() == 0);
    return;
  }

#if defined(LIBXR_SYSTEM_POSIX_HOST)
  pid_t child = fork();
  TEST_ASSERT(child >= 0);

  if (child == 0)
  {
    const int child_status = test_case.function();
    _exit(child_status);
  }

  int status = 0;
  TEST_ASSERT(waitpid(child, &status, 0) == child);
  TEST_ASSERT(WIFEXITED(status));
  TEST_ASSERT(WEXITSTATUS(status) == 0);
#else
  TEST_ASSERT(false);
#endif
}
