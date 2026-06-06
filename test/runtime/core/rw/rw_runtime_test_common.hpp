/**
 * @file rw_runtime_test_common.hpp
 * @brief runtime `rw` / `pipe` 测试聚合 helper 入口。 Aggregation helper entry for runtime `rw` / `pipe` tests.
 * @details 测试项目：
 *          1. 聚合共享的模式、端口状态机和 `Pipe` helper。
 *          2. 对外仅保留 runtime `rw` / `pipe` 分组运行入口声明。
 *          3. 让 runtime 场景文件与 base 场景文件复用同一套底层 helper。
 *          Test items:
 *          1. Aggregate the shared mode, port-state, and `Pipe` helpers.
 *          2. Expose only the runtime `rw` / `pipe` group runner declarations.
 *          3. Let runtime scenarios reuse the same low-level helpers as base scenarios.
 */
#pragma once

#include "../../../common/core/rw/pipe_test_common.hpp"


void RunRuntimeRwBlockTests();
void RunRuntimePipeTests();
