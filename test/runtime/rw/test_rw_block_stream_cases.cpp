/**
 * @file test_rw_block_stream_cases.cpp
 * @brief runtime `rw` 阻塞 `Stream` 场景子测试。 Split test unit for runtime blocking `Stream` scenarios.
 * @details 测试项目：
 *          1. `Stream::Commit` 在挂起完成后会回传最终失败码。
 *          2. `Stream` 超时后会解除等待者，后续完成信号不会残留。
 *          3. `Stream` 析构自动提交会沿用同一阻塞完成路径。
 *          Test items:
 *          1. `Stream::Commit` propagates the final error after pending completion.
 *          2. Timeout detaches the waiter and leaves no stale completion signal behind.
 *          3. Destructor auto-commit reuses the same blocking completion path.
 */
#include "rw_runtime_test_common.hpp"

namespace
{

/**
 * @brief 测试入口函数 `test_rw_stream_block_pending_result_propagates`。 Test entry function `test_rw_stream_block_pending_result_propagates`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_stream_block_pending_result_propagates()
{
  // 测试内容：阻塞 `Commit` 应把异步完成阶段给出的最终错误码原样返回。
  // Test coverage: blocking `Commit` should return the exact final error produced by the asynchronous completion path.
  VerifyStreamBlockPendingCompletion(LibXR::ErrorCode::FAILED, LibXR::ErrorCode::FAILED,
                                     StreamSubmitMode::COMMIT);
}

/**
 * @brief 测试入口函数 `test_rw_stream_block_timeout_detaches_waiter`。 Test entry function `test_rw_stream_block_timeout_detaches_waiter`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_stream_block_timeout_detaches_waiter()
{
  // 测试内容：超时返回后，旧等待者不应留下额外信号或 busy 状态。
  // Test coverage: after a timeout, the old waiter must not leave extra signals or busy state behind.
  VerifyStreamBlockTimeout();
}

/**
 * @brief 测试入口函数 `test_rw_stream_block_destructor_autocommit`。 Test entry function `test_rw_stream_block_destructor_autocommit`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_stream_block_destructor_autocommit()
{
  // 测试内容：析构自动提交也应等待挂起完成并回到空闲态。
  // Test coverage: destructor auto-commit should also wait for pending completion and return to idle.
  VerifyStreamBlockPendingCompletion(LibXR::ErrorCode::OK, LibXR::ErrorCode::OK,
                                     StreamSubmitMode::DESTRUCT);
}

}  // namespace

/**
 * @brief 测试项函数 `RunRuntimeRwBlockStreamTests`。 Test-item function `RunRuntimeRwBlockStreamTests`.
 * @details 测试内容：执行 runtime `rw` 阻塞 `Stream` 子场景。 Execute runtime blocking `Stream` sub-scenarios.
 *          测试原理：把 `Stream` 的阻塞提交路径单独成组，避免和普通端口等待者路径混排。 Group blocking `Stream` submission paths separately from ordinary port waiter paths.
 */
void RunRuntimeRwBlockStreamTests()
{
  test_rw_stream_block_pending_result_propagates();
  test_rw_stream_block_timeout_detaches_waiter();
  test_rw_stream_block_destructor_autocommit();
}
