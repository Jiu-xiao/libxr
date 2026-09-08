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
