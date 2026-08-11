/**
 * @file test_rw_fail_clear.cpp
 * @brief Base FailAndClearAll scenario entrypoint.
 */
#include "rw_test_common.hpp"

void RunBaseRwFailAndClearAsyncTests();
void RunBaseRwFailAndClearIdleTests();

void RunBaseRwFailAndClearTests()
{
  RunBaseRwFailAndClearAsyncTests();
  RunBaseRwFailAndClearIdleTests();
}
