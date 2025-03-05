#include <cstdio>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_timebase() {
  LibXR::TimestampMS t1(1000), t2(2005);
  LibXR::TimestampUS t3(1000), t4(2005);

  t1 = LibXR::Timebase::GetMilliseconds();
  t3 = LibXR::Timebase::GetMicroseconds();
  LibXR::Thread::Sleep(100);
  t4 = LibXR::Timebase::GetMicroseconds();
  t2 = LibXR::Timebase::GetMilliseconds();

  ASSERT(std::fabs((t2 - t1).to_millisecond() - 100.0f) < 2);
  ASSERT(std::fabs((t4 - t3).to_microsecond() - 100000.0f) < 2000);
}