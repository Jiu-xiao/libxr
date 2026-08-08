/**
 * @file test_rw_fail_clear_stream.cpp
 * @brief base `FailAndClearAll` 静止清理与冲突 owner 防护场景。 Split test unit for
 * quiescent `FailAndClearAll` cleanup and conflicting-owner protection.
 * @details 测试项目：
 *          1. 空闲写口上的 `FailAndClearAll` 会清空残留队列数据和 info block。
 *          2. 违反静止前提并遇到读 owner 时，普通构建必须退回且不得破坏 owner/队列。
 *          1. `FailAndClearAll` clears queued data and info blocks on an idle write port.
 *          2. If the quiescent precondition is violated and a read owner is present,
 *             normal builds back off without modifying the owner or queue.
 */
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>

#include "rw_test_common.hpp"

extern char** environ;

extern const char kRwFailClearOwnerDevAssertScenario[] =
    "--rw-fail-clear-owner-dev-assert";

int RunRwFailClearOwnerDevAssertScenario()
{
#ifdef LIBXR_DEV_ASSERT_BUILD
  static constexpr int FATAL_EXIT_CODE = 86;
  auto fatal_callback = LibXR::Assert::FatalCallback::Create(
      [](bool, void*, const char*, uint32_t) { _exit(FATAL_EXIT_CODE); },
      static_cast<void*>(nullptr));
  LibXR::Assert::RegisterFatalErrorCallback(fatal_callback);

  LibXR::ReadPort port(4);
  port.busy_.store(LibXR::ReadPort::BusyState::CLAIMED, std::memory_order_release);
  port.FailAndClearAll(LibXR::ErrorCode::INIT_ERR, false);
  return 0;
#else
  return 2;
#endif
}

namespace
{

void ExpectFailClearOwnerDevAssert()
{
  static constexpr int EXPECTED_EXIT_CODE = 86;
  char executable[] = "/proc/self/exe";
  char* child_argv[] = {executable, const_cast<char*>(kRwFailClearOwnerDevAssertScenario),
                        nullptr};
  pid_t child = -1;
  ASSERT(posix_spawn(&child, executable, nullptr, nullptr, child_argv, environ) == 0);

  int status = 0;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (true)
  {
    const pid_t wait_result = waitpid(child, &status, WNOHANG);
    if (wait_result == child)
    {
      break;
    }
    if (wait_result < 0 && errno == EINTR)
    {
      continue;
    }
    ASSERT(wait_result == 0);

    if (std::chrono::steady_clock::now() >= deadline)
    {
      const int kill_result = kill(child, SIGKILL);
      ASSERT(kill_result == 0 || errno == ESRCH);
      pid_t reap_result = -1;
      do
      {
        reap_result = waitpid(child, &status, 0);
      } while (reap_result < 0 && errno == EINTR);
      ASSERT(reap_result == child);
      ASSERT(false);
    }
    LibXR::Thread::Sleep(1U);
  }

  ASSERT(WIFEXITED(status));
  ASSERT(WEXITSTATUS(status) == EXPECTED_EXIT_CODE);
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

void test_rw_read_port_fail_clear_preserves_conflicting_owner()
{
  using namespace LibXR;

  ReadPort r(4);
  static const uint8_t QUEUED = 0x5A;
  ASSERT(r.queue_data_->PushBatch(&QUEUED, 1) == ErrorCode::OK);
  r.busy_.store(ReadPort::BusyState::CLAIMED, std::memory_order_release);

#ifdef LIBXR_DEV_ASSERT_BUILD
  ExpectFailClearOwnerDevAssert();
#else
  r.FailAndClearAll(ErrorCode::INIT_ERR, false);
  ASSERT(r.busy_.load(std::memory_order_acquire) == ReadPort::BusyState::CLAIMED);
  ASSERT(r.Size() == 1);
#endif
}

}  // namespace

/**
 * @brief 测试项函数 `RunBaseRwFailAndClearStreamTests`。 Test-item function
 * `RunBaseRwFailAndClearStreamTests`.
 * @details 测试内容：执行静止空闲清理与冲突 owner 防护场景。 Execute quiescent idle
 * cleanup and conflicting-owner protection.
 */
void RunBaseRwFailAndClearStreamTests()
{
  test_rw_write_port_fail_and_clear_all_clears_idle_queue();
  test_rw_read_port_fail_clear_preserves_conflicting_owner();
}
