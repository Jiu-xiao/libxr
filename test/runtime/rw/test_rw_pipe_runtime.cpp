/**
 * @file test_rw_pipe_runtime.cpp
 * @brief runtime `rw` / `pipe` 测试聚合入口。 Aggregation entry for split runtime `rw` / `pipe` tests.
 */
#include "rw_runtime_test_common.hpp"

/**
 * @brief 测试入口函数 `test_rw_runtime`。 Test entry function `test_rw_runtime`.
 * @details 测试内容：聚合执行拆分后的 runtime `rw` 子测试组。 Aggregate and execute the split runtime `rw` subtest groups.
 *          测试原理：在拆分 runtime 大文件后，继续保留原 runner 使用的单入口名称。 Preserve the original single-entry name used by the runner after splitting the oversized runtime file.
 */
void test_rw_runtime()
{
  RunRuntimeRwBlockTests();
}

/**
 * @brief 测试入口函数 `test_pipe_runtime`。 Test entry function `test_pipe_runtime`.
 * @details 测试内容：聚合执行拆分后的 runtime `Pipe` 子测试组。 Aggregate and execute the split runtime `Pipe` subtest groups.
 *          测试原理：把 runtime pipe 场景拆分后，继续通过单入口提供给现有 harness。 Keep the existing harness contract after splitting runtime pipe scenarios into separate files.
 */
void test_pipe_runtime()
{
  RunRuntimePipeTests();
}
