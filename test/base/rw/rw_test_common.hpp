/**
 * @file rw_test_common.hpp
 * @brief base `rw` / `pipe` 测试聚合 helper 入口。 Aggregation helper entry for base `rw` / `pipe` tests.
 * @details 测试项目：
 *          1. 聚合共享的模式、端口状态机和 `Pipe` helper。
 *          2. 对外仅保留 base `rw` / `pipe` 分组运行入口声明。
 *          3. 让具体测试文件只依赖一个薄包装头，避免重复 include 路径噪音。
 *          Test items:
 *          1. Aggregate the shared mode, port-state, and `Pipe` helpers.
 *          2. Expose only the base `rw` / `pipe` group runner declarations.
 *          3. Keep scenario files depending on one thin wrapper instead of long include chains.
 */
#pragma once

#include "../../common/core/rw/pipe_test_common.hpp"


void RunBaseRwReadQueueTests();
void RunBaseRwPendingTests();
void RunBaseRwBlockTests();
void RunBaseRwFailAndClearTests();
void RunBasePipeBasicTests();
void RunBasePipeStreamTests();
void RunBasePipeStressTests();
