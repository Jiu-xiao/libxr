/**
 * @file test_rw_fail_clear_async.cpp
 * @brief base `FailAndClearAll` 异步完成场景子测试。 Split test unit for base `FailAndClearAll` asynchronous completion scenarios.
 * @details 测试项目：
 *          1. `ReadPort::FailAndClearAll` 会完成异步挂起读并回传指定错误。
 *          2. `WritePort::FailAndClearAll` 会完成异步挂起写并回传指定错误。
 *          Test items:
 *          1. `ReadPort::FailAndClearAll` completes asynchronous pending reads with the requested error.
 *          2. `WritePort::FailAndClearAll` completes asynchronous pending writes with the requested error.
 */
#include "rw_test_common.hpp"

namespace
{

/**
 * @brief 测试入口函数 `test_rw_read_port_fail_and_clear_all_completes_async_pending`。 Test entry function `test_rw_read_port_fail_and_clear_all_completes_async_pending`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_read_port_fail_and_clear_all_completes_async_pending()
{
  // 测试内容：所有异步模式下的挂起读都应收到 `INIT_ERR` 完成通知。
  // Test coverage: every asynchronous mode should receive an `INIT_ERR` completion for pending reads.
  for (auto mode : LibXRTest::ASYNC_MODES)
  {
    VerifyPendingReadFailAndClearMode(mode, LibXR::ErrorCode::INIT_ERR);
  }
}

/**
 * @brief 测试入口函数 `test_rw_write_port_fail_and_clear_all_completes_async_pending`。 Test entry function `test_rw_write_port_fail_and_clear_all_completes_async_pending`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_write_port_fail_and_clear_all_completes_async_pending()
{
  // 测试内容：所有异步模式下的挂起写都应收到 `INIT_ERR` 完成通知。
  // Test coverage: every asynchronous mode should receive an `INIT_ERR` completion for pending writes.
  for (auto mode : LibXRTest::ASYNC_MODES)
  {
    VerifyPendingWriteFailAndClearMode(mode, LibXR::ErrorCode::INIT_ERR);
  }
}

}  // namespace

/**
 * @brief 测试项函数 `RunBaseRwFailAndClearAsyncTests`。 Test-item function `RunBaseRwFailAndClearAsyncTests`.
 * @details 测试内容：执行 `FailAndClearAll` 异步完成子场景。 Execute `FailAndClearAll` asynchronous completion sub-scenarios.
 *          测试原理：把异步完成路径单独成组，减少和阻塞/stream 语义的耦合。 Group asynchronous completions separately from blocking and stream semantics.
 */
void RunBaseRwFailAndClearAsyncTests()
{
  test_rw_read_port_fail_and_clear_all_completes_async_pending();
  test_rw_write_port_fail_and_clear_all_completes_async_pending();
}
