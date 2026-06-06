/**
 * @file test_flag.cpp
 * @brief Plain/atomic flag and scoped-restore tests.
 *
 * Test items:
 * 1. Plain flag state transitions: verify set/clear/test-and-set/test-and-clear/exchange semantics.
 * 2. Atomic flag state transitions: verify the atomic variant exposes the same visible contract.
 * 3. Scoped restore: verify nested scoped guards restore the previous flag value on scope exit.
 *
 * Test principle:
 * 1. Check the returned previous-state values as well as the final flag state, because the API contract is about both.
 */
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_flag()
{
  LibXR::Flag::Plain plain;
  ASSERT(!plain.IsSet());
  ASSERT(!plain.TestAndSet());
  ASSERT(plain.IsSet());
  ASSERT(plain.TestAndSet());
  ASSERT(plain.TestAndClear());
  ASSERT(!plain.IsSet());
  ASSERT(!plain.TestAndClear());
  ASSERT(!plain.Exchange(true));
  ASSERT(plain.IsSet());
  ASSERT(plain.Exchange(false));
  ASSERT(!plain.IsSet());

  {
    LibXR::Flag::ScopedRestore restore_outer(plain, true);
    ASSERT(plain.IsSet());
    {
      LibXR::Flag::ScopedRestore restore_inner(plain, false);
      ASSERT(!plain.IsSet());
    }
    ASSERT(plain.IsSet());
  }
  ASSERT(!plain.IsSet());

  LibXR::Flag::Atomic atomic;
  ASSERT(!atomic.IsSet());
  ASSERT(!atomic.TestAndSet());
  ASSERT(atomic.IsSet());
  ASSERT(atomic.TestAndSet());
  ASSERT(atomic.Exchange(false));
  ASSERT(!atomic.IsSet());
  ASSERT(!atomic.Exchange(true));
  ASSERT(atomic.IsSet());
  atomic.Clear();
  ASSERT(!atomic.IsSet());

  {
    LibXR::Flag::ScopedRestore restore(atomic, true);
    ASSERT(atomic.IsSet());
  }
  ASSERT(!atomic.IsSet());
}
