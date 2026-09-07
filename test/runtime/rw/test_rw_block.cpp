/**
 * @file test_rw_block.cpp
 * @brief RW 阻塞与超时测试入口 / Entry point for blocking and timeout RW tests.
 */
#include "rw_runtime_test_common.hpp"

void RunRuntimeRwBlockStreamTests();
void RunRuntimeRwBlockTimeoutTests();
void RunRuntimeRwBlockWaiterTests();

/**
 * @brief 运行阻塞流、超时与等待者生命周期测试
 *        / Run BLOCK stream, timeout, and waiter lifetime tests.
 */
void RunRuntimeRwBlockTests()
{
  RunRuntimeRwBlockStreamTests();
  RunRuntimeRwBlockTimeoutTests();
  RunRuntimeRwBlockWaiterTests();
}
