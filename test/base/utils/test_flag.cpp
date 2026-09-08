/**
 * @file test_flag.cpp
 * @brief 普通/原子 flag 与 scoped restore 测试。 Plain/atomic flag and scoped-restore
 * tests.
 *
 * 测试项目 / Test items:
 * 1. Plain flag 的状态迁移。 Plain flag state transitions: verify
 * set/clear/test-and-set/test-and-clear/exchange semantics.
 * 2. Atomic flag 的同构语义。 Atomic flag state transitions: verify the atomic variant
 * exposes the same visible contract.
 * 3. 嵌套 scoped restore 的恢复行为。 Scoped restore: verify nested scoped guards restore
 * the previous flag value on scope exit.
 *
 * 测试原理 / Test principles:
 * 1. 同时检查返回的旧状态和值最终状态，因为这个 API 的契约覆盖两者。 Check the returned
 * previous-state values as well as the final flag state, because the API contract is
 * about both.
 */
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"
#include "test_assert.hpp"

/**
 * @brief 测试入口函数 `test_flag`。 Test entry function `test_flag`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared
 * in this file in order. 测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。
 * Validate the module contract through the scenarios assembled in this file.
 */
void test_flag()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
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
