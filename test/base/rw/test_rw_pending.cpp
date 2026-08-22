/**
 * @file test_rw_pending.cpp
 * @brief base `rw` pending mode 与边界场景子测试。 Split test unit for base `rw`
 * pending-mode and edge scenarios.
 */
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <barrier>
#include <cerrno>
#include <chrono>
#include <thread>

#include "rw_test_common.hpp"

extern char** environ;

extern const char kRwIsrReadBlockScenario[] = "--rw-isr-read-block";
extern const char kRwIsrWriteBlockScenario[] = "--rw-isr-write-block";

namespace
{

constexpr int FATAL_EXIT_CODE = 86;

void RegisterFatalExitCallback()
{
  auto fatal_callback = LibXR::Assert::FatalCallback::Create(
      [](bool, void*, const char*, uint32_t) { _exit(FATAL_EXIT_CODE); },
      static_cast<void*>(nullptr));
  LibXR::Assert::RegisterFatalErrorCallback(fatal_callback);
}

}  // namespace

int RunRwIsrReadBlockScenario()
{
  using namespace LibXR;

  RegisterFatalExitCallback();
  uint8_t read_data = 0;
  Semaphore read_sem;
  ReadOperation read_op(read_sem, 1);
  ReadPort unreadable(1);
  UNUSED(unreadable(RawData{&read_data, 1}, read_op, true));
  return 0;
}

int RunRwIsrWriteBlockScenario()
{
  using namespace LibXR;

  RegisterFatalExitCallback();
  static const uint8_t WRITE_DATA = 0xA5;
  Semaphore write_sem;
  WriteOperation write_op(write_sem, 1);
  WritePort unwritable(1, 1);
  UNUSED(unwritable(ConstRawData{&WRITE_DATA, 1}, write_op, true));
  return 0;
}

namespace
{

constexpr size_t READ_PUBLISH_RACE_ITERATIONS = 1024;

void ExpectFatalTermination(const char* scenario)
{
  char executable[] = "/proc/self/exe";
  char* child_argv[] = {executable, const_cast<char*>(scenario), nullptr};
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

  ASSERT(WIFSIGNALED(status) || (WIFEXITED(status) && WEXITSTATUS(status) != 0));
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

  ASSERT(CompleteFrontWrite(w, ErrorCode::OK));
}

void test_rw_read_publish_handles_concurrent_producer()
{
  using namespace LibXR;

  ReadPort port(4);
  std::barrier<> iteration_start(2);
  std::barrier<> iteration_done(2);

  std::thread producer(
      [&]()
      {
        for (size_t iteration = 0; iteration < READ_PUBLISH_RACE_ITERATIONS; ++iteration)
        {
          iteration_start.arrive_and_wait();
          if ((iteration & 1U) != 0U)
          {
            std::this_thread::yield();
          }
          const uint8_t data = static_cast<uint8_t>((iteration % 251U) + 1U);
          {
            auto queue = port.GetReadQueue();
            ASSERT(queue.Push(data) == ErrorCode::OK);
            queue.Publish();
          }
          iteration_done.arrive_and_wait();
        }
      });

  for (size_t iteration = 0; iteration < READ_PUBLISH_RACE_ITERATIONS; ++iteration)
  {
    uint8_t received = 0;
    OperationPollingStatus status;
    ReadOperation operation(status);

    iteration_start.arrive_and_wait();
    if ((iteration & 1U) == 0U)
    {
      std::this_thread::yield();
    }
    const ErrorCode submit_result = port(RawData{&received, 1}, operation, false);
    iteration_done.arrive_and_wait();

    const uint8_t expected = static_cast<uint8_t>((iteration % 251U) + 1U);
    ASSERT(submit_result == ErrorCode::OK);
    ASSERT(status.Load() == OperationPollingStatus::DONE);
    ASSERT(received == expected);
    ASSERT(port.Size() == 0U);
  }

  producer.join();
}

void test_rw_zero_capacity_read_port_is_not_supported()
{
  using namespace LibXR;

  ReadPort port(0);
  uint8_t received = 0;
  OperationPollingStatus status;
  ReadOperation operation(status);

  ASSERT(!port.Readable());
  ASSERT(port(RawData{&received, 1}, operation) == ErrorCode::NOT_SUPPORT);
  ASSERT(status.Load() == OperationPollingStatus::READY);
}

void test_rw_isr_block_fails_before_capability_checks()
{
  ExpectFatalTermination(kRwIsrReadBlockScenario);
  ExpectFatalTermination(kRwIsrWriteBlockScenario);
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
  test_rw_read_publish_handles_concurrent_producer();
  test_rw_zero_capacity_read_port_is_not_supported();
  test_rw_isr_block_fails_before_capability_checks();
}
