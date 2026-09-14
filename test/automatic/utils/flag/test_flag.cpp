/**
 * @file test_flag.cpp
 * @brief Flag 状态操作测试 / Flag state-operation tests.
 *
 * 检查普通与原子标志的旧值返回，以及 ScopedRestore 离开作用域后的恢复。
 * Check prior-value returns for plain and atomic flags, and restoration when
 * ScopedRestore leaves scope.
 */

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"
#include "test_assert.hpp"

void test_flag()
{
  LibXR::Flag::Plain plain;
  TEST_ASSERT(!plain.IsSet());
  TEST_ASSERT(!plain.TestAndSet());
  TEST_ASSERT(plain.IsSet());
  TEST_ASSERT(plain.TestAndSet());
  TEST_ASSERT(plain.TestAndClear());
  TEST_ASSERT(!plain.IsSet());
  TEST_ASSERT(!plain.TestAndClear());
  TEST_ASSERT(!plain.Exchange(true));
  TEST_ASSERT(plain.IsSet());
  TEST_ASSERT(plain.Exchange(false));
  TEST_ASSERT(!plain.IsSet());

  {
    LibXR::Flag::ScopedRestore restore_outer(plain, true);
    TEST_ASSERT(plain.IsSet());
    {
      LibXR::Flag::ScopedRestore restore_inner(plain, false);
      TEST_ASSERT(!plain.IsSet());
    }
    TEST_ASSERT(plain.IsSet());
  }
  TEST_ASSERT(!plain.IsSet());

  LibXR::Flag::Atomic atomic;
  TEST_ASSERT(!atomic.IsSet());
  TEST_ASSERT(!atomic.TestAndSet());
  TEST_ASSERT(atomic.IsSet());
  TEST_ASSERT(atomic.TestAndSet());
  TEST_ASSERT(atomic.Exchange(false));
  TEST_ASSERT(!atomic.IsSet());
  TEST_ASSERT(!atomic.Exchange(true));
  TEST_ASSERT(atomic.IsSet());
  atomic.Clear();
  TEST_ASSERT(!atomic.IsSet());

  {
    LibXR::Flag::ScopedRestore restore(atomic, true);
    TEST_ASSERT(atomic.IsSet());
  }
  TEST_ASSERT(!atomic.IsSet());
}
