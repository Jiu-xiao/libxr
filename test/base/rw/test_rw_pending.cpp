/**
 * @file test_rw_pending.cpp
 * @brief base `rw` pending mode 与边界场景子测试。 Split test unit for base `rw`
 * pending-mode and edge scenarios.
 */
#include <sys/wait.h>
#include <unistd.h>

#include "rw_test_common.hpp"

namespace
{

bool arm_failure_data_pushed = false;

template <typename Function>
void ExpectFatalTermination(Function function)
{
  const pid_t child = fork();
  ASSERT(child >= 0);
  if (child == 0)
  {
    function();
    _exit(0);
  }

  int status = 0;
  ASSERT(waitpid(child, &status, 0) == child);
  ASSERT(WIFSIGNALED(status) || (WIFEXITED(status) && WEXITSTATUS(status) != 0));
}

LibXR::ErrorCode PushThenFailFirstReadArm(LibXR::ReadPort& port, bool in_isr)
{
  if (arm_failure_data_pushed)
  {
    return LibXR::ErrorCode::PENDING;
  }

  arm_failure_data_pushed = true;
  static const uint8_t DATA = 0x5A;
  ASSERT(port.queue_data_->PushBatch(&DATA, 1) == LibXR::ErrorCode::OK);
  port.ProcessPendingReads(in_isr);
  return LibXR::ErrorCode::INIT_ERR;
}

struct ArmFailureContext
{
  LibXR::ReadPort* port = nullptr;
  LibXR::ReadOperation* nested_op = nullptr;
  uint8_t* nested_data = nullptr;
  LibXR::ErrorCode nested_submit = LibXR::ErrorCode::FAILED;
  LibXR::ErrorCode outer_status = LibXR::ErrorCode::FAILED;
  LibXR::ErrorCode nested_status = LibXR::ErrorCode::FAILED;
  uint32_t outer_callbacks = 0;
  uint32_t nested_callbacks = 0;
};

void OnNestedRead(bool, ArmFailureContext* context, LibXR::ErrorCode status)
{
  context->nested_status = status;
  ++context->nested_callbacks;
}

void OnOuterArmFailure(bool, ArmFailureContext* context, LibXR::ErrorCode status)
{
  context->outer_status = status;
  ++context->outer_callbacks;
  context->nested_submit = (*context->port)(LibXR::RawData{context->nested_data, 1},
                                            *context->nested_op, false);
}

}  // namespace

/**
 * @brief 测试入口函数 `test_rw_pending_mode_matrix`。 Test entry function
 * `test_rw_pending_mode_matrix`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared
 * in this file in order. 测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。
 * Validate the module contract through the scenarios assembled in this file.
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
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared
 * in this file in order. 测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。
 * Validate the module contract through the scenarios assembled in this file.
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

void test_rw_read_arm_failure_cannot_clear_reentrant_read()
{
  using namespace LibXR;

  ReadPort r(4);
  r = PushThenFailFirstReadArm;
  arm_failure_data_pushed = false;

  uint8_t outer_data = 0;
  uint8_t nested_data = 0;
  ArmFailureContext context{};
  auto nested_callback = Callback<ErrorCode>::Create(OnNestedRead, &context);
  ReadOperation nested_op(nested_callback);
  context.port = &r;
  context.nested_op = &nested_op;
  context.nested_data = &nested_data;

  auto outer_callback = Callback<ErrorCode>::Create(OnOuterArmFailure, &context);
  ReadOperation outer_op(outer_callback);
  ASSERT(r(RawData{&outer_data, 1}, outer_op) == ErrorCode::INIT_ERR);

  ASSERT(context.outer_callbacks == 1);
  ASSERT(context.outer_status == ErrorCode::INIT_ERR);
  ASSERT(context.nested_submit == ErrorCode::OK);
  ASSERT(context.nested_callbacks == 1);
  ASSERT(context.nested_status == ErrorCode::OK);
  ASSERT(nested_data == 0x5A);
  ASSERT(r.busy_.load(std::memory_order_acquire) == ReadPort::BusyState::IDLE);
}

void test_rw_isr_block_fails_before_capability_checks()
{
  using namespace LibXR;

  uint8_t read_data = 0;
  Semaphore read_sem;
  ReadOperation read_op(read_sem, 1);
  ReadPort unreadable(1);
  ExpectFatalTermination([&]()
                         { UNUSED(unreadable(RawData{&read_data, 1}, read_op, true)); });

  static const uint8_t WRITE_DATA = 0xA5;
  Semaphore write_sem;
  WriteOperation write_op(write_sem, 1);
  WritePort unwritable(1, 1);
  ExpectFatalTermination(
      [&]() { UNUSED(unwritable(ConstRawData{&WRITE_DATA, 1}, write_op, true)); });
}

/**
 * @brief 测试项函数 `RunBaseRwPendingTests`。 Test-item function `RunBaseRwPendingTests`.
 * @details 测试内容：执行当前分组里的 `rw`/`pipe` 子场景。 Execute the grouped
 * `rw`/`pipe` sub-scenarios for this split file.
 *          测试原理：把同类状态机场景收在一组，降低单文件体积并保留聚合入口。 Group
 * related state-machine scenarios together to shrink file size while preserving
 * aggregated entrypoints.
 */
void RunBaseRwPendingTests()
{
  test_rw_pending_mode_matrix();
  test_rw_edge_cases();
  test_rw_read_arm_failure_cannot_clear_reentrant_read();
  test_rw_isr_block_fails_before_capability_checks();
}
