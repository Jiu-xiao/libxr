/**
 * @file test_rw_pending.cpp
 * @brief base `rw` pending mode 与边界场景子测试。 Split test unit for base `rw` pending-mode and edge scenarios.
 */
#include "rw_test_common.hpp"

/**
 * @brief 测试入口函数 `test_rw_pending_mode_matrix`。 Test entry function `test_rw_pending_mode_matrix`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_pending_mode_matrix()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  for (auto mode : ASYNC_MODES)
  {
    VerifyPendingReadMode(mode);
    VerifyPendingWriteMode(mode, LibXR::ErrorCode::FAILED);
  }
}

/**
 * @brief 测试入口函数 `test_rw_edge_cases`。 Test entry function `test_rw_edge_cases`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_edge_cases()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  using namespace LibXR;

  for (auto mode : ASYNC_MODES)
  {
    VerifyZeroWriteMode(mode);
    VerifyZeroReadMode(mode);
  }

  WritePort w(1, 4);
  w = PendingWriteFun;
  const uint8_t tx2[] = {5};
  WriteOperation op1;
  WriteOperation op2;
  std::vector<uint8_t> tx1(w.EmptySize(), 0x3C);

  ASSERT(!tx1.empty());
  ASSERT(w(ConstRawData{tx1.data(), tx1.size()}, op1) == ErrorCode::OK);
  auto second_result = w(ConstRawData{tx2, sizeof(tx2)}, op2);
  ASSERT(second_result == ErrorCode::FULL);

  WriteInfoBlock completed{};
  ASSERT(w.queue_info_->Pop(completed) == ErrorCode::OK);
  w.Finish(false, ErrorCode::OK, completed);
}

/**
 * @brief 测试项函数 `RunBaseRwPendingTests`。 Test-item function `RunBaseRwPendingTests`.
 * @details 测试内容：执行当前分组里的 `rw`/`pipe` 子场景。 Execute the grouped `rw`/`pipe` sub-scenarios for this split file.
 *          测试原理：把同类状态机场景收在一组，降低单文件体积并保留聚合入口。 Group related state-machine scenarios together to shrink file size while preserving aggregated entrypoints.
 */
void RunBaseRwPendingTests()
{
  test_rw_pending_mode_matrix();
  test_rw_edge_cases();
}
