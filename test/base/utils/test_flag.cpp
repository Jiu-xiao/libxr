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
