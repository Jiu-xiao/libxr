/**
 * @file test_rw_pipe.cpp
 * @brief base `rw` / `pipe` 测试聚合入口。 Aggregation entry for split base `rw` / `pipe` tests.
 */
#include "rw_test_common.hpp"

/**
 * @brief 测试入口函数 `test_rw`。 Test entry function `test_rw`.
 * @details 测试内容：聚合执行拆分后的 base `rw` 子测试组。 Aggregate and execute the split base `rw` subtest groups.
 *          测试原理：把端口状态机验证拆成多个主题文件后，仍通过单一入口保持 runner 契约。 Preserve the runner contract after splitting port-state-machine verification into themed files.
 */
void test_rw()
{
  RunBaseRwReadQueueTests();
  RunBaseRwPendingTests();
  RunBaseRwBlockTests();
  RunBaseRwFailAndClearTests();
}

/**
 * @brief 测试入口函数 `test_pipe`。 Test entry function `test_pipe`.
 * @details 测试内容：聚合执行拆分后的 base `Pipe` 子测试组。 Aggregate and execute the split base `Pipe` subtest groups.
 *          测试原理：把基础传输、stream 语义和压力场景拆分后，继续通过单入口提供给现有 harness。 Keep the existing harness contract after splitting basic transport, stream semantics, and stress scenarios into separate files.
 */
void test_pipe()
{
  RunBasePipeBasicTests();
  RunBasePipeStreamTests();
  RunBasePipeStressTests();
}
