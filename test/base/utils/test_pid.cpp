/**
 * @file test_pid.cpp
 * @brief PID 测试聚合入口。 Aggregation entry for split PID tests.
 */
#include "pid_test_common.hpp"

/**
 * @brief 测试入口函数 `test_pid`。 Test entry function `test_pid`.
 * @details 测试内容：聚合执行拆分后的 PID 子测试组。 Aggregate and execute the split PID subtest groups.
 *          测试原理：在保持原 runner 入口不变的前提下，把 PID 的大文件按语义拆分。 Preserve the original runner entry while splitting the oversized PID test by semantic groups.
 */
void test_pid()
{
  RunPidResponseTests();
  RunPidStateTests();
  RunPidCycleTests();
}
