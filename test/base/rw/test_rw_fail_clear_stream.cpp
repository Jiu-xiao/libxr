/**
 * @file test_rw_fail_clear_stream.cpp
 * @brief base `FailAndClearAll` 空闲清理与冲突 owner 保护场景。 Split test unit for
 * `FailAndClearAll` idle cleanup and conflicting-owner preservation.
 * @details 测试项目：
 *          1. 空闲写口上的 `FailAndClearAll` 会清空残留队列数据和 info block。
 *          2. 与读完成 owner 冲突时不得覆盖其状态或清空队列。
 *          1. `FailAndClearAll` clears queued data and info blocks on an idle write port.
 *          2. A conflicting read-completion owner keeps its state and queued data.
 */
#include <sys/wait.h>
#include <unistd.h>

#include "rw_test_common.hpp"

namespace
{

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

/**
 * @brief 测试入口函数 `test_rw_write_port_fail_and_clear_all_clears_idle_queue`。 Test
 * entry function `test_rw_write_port_fail_and_clear_all_clears_idle_queue`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared
 * in this file in order. 测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。
 * Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_write_port_fail_and_clear_all_clears_idle_queue()
{
  // 测试内容：空闲写口的残留队列应在失败清理后被完整清空。
  // Test coverage: residual queued bytes on an idle write port should be fully cleared by
  // fail-clear.
  using namespace LibXR;

  WritePort w(2, 16);
  w = PendingWriteFun;

  static const uint8_t TX[] = {0x21, 0x22, 0x23};
  WriteOperation op;
  ASSERT(w(ConstRawData{TX, sizeof(TX)}, op) == ErrorCode::OK);
  ASSERT(w.busy_.load(std::memory_order_acquire) == WritePort::BusyState::IDLE);
  ASSERT(w.Size() == sizeof(TX));
  ASSERT(w.queue_info_->Size() == 1);

  w.FailAndClearAll(ErrorCode::INIT_ERR, false);

  ASSERT(w.busy_.load(std::memory_order_acquire) == WritePort::BusyState::IDLE);
  ASSERT(w.Size() == 0);
  ASSERT(w.queue_info_->Size() == 0);
}

void test_rw_read_port_fail_clear_preserves_processing_owner()
{
  using namespace LibXR;

  ReadPort r(4);
  r = PendingReadFun;
  static const uint8_t QUEUED = 0x5A;
  ASSERT(r.queue_data_->PushBatch(&QUEUED, 1) == ErrorCode::OK);
  r.busy_.store(ReadPort::BusyState::PROCESSING, std::memory_order_release);

#ifdef LIBXR_DEBUG_BUILD
  ExpectFatalTermination([&]() { r.FailAndClearAll(ErrorCode::INIT_ERR, false); });
#else
  r.FailAndClearAll(ErrorCode::INIT_ERR, false);
  ASSERT(r.busy_.load(std::memory_order_acquire) == ReadPort::BusyState::PROCESSING);
  ASSERT(r.Size() == 1);
#endif
}

}  // namespace

/**
 * @brief 测试项函数 `RunBaseRwFailAndClearStreamTests`。 Test-item function
 * `RunBaseRwFailAndClearStreamTests`.
 * @details 测试内容：执行空闲清理与冲突 owner 保护场景。 Execute idle cleanup and
 * conflicting-owner preservation scenarios.
 */
void RunBaseRwFailAndClearStreamTests()
{
  test_rw_write_port_fail_and_clear_all_clears_idle_queue();
  test_rw_read_port_fail_clear_preserves_processing_owner();
}
