/**
 * @file test_assert.hpp
 * @brief 始终生效的测试断言 / Always-active test assertions.
 *
 * 检查失败时输出位置和表达式并终止进程，不受产品断言开关影响。
 * Print the location and expression and abort on failure, independently of product
 * assertion switches.
 */

#pragma once

#include <cstdio>
#include <cstdlib>

/// 测试结果检查，不受产品断言开关影响 / Test check independent of product assertions.
#define TEST_ASSERT(condition)                                                          \
  do                                                                                    \
  {                                                                                     \
    if (!(condition))                                                                   \
    {                                                                                   \
      std::fprintf(stderr, "%s:%d: test failed: %s\n", __FILE__, __LINE__, #condition); \
      std::abort();                                                                     \
    }                                                                                   \
  } while (0)
