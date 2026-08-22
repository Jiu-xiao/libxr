/**
 * @file test_rw_block_waiter_cases.cpp
 * @brief runtime `AsyncBlockWait` 与 `WritePort` BLOCK waiter 场景子测试。 Split test
 * unit for runtime `AsyncBlockWait` and `WritePort` BLOCK waiter scenarios.
 * @details 测试项目：
 *          1. `AsyncBlockWait` 区分 completion 结果与 caller detach。
 *          2. timeout 或底层 semaphore failure 分离 caller 后，迟到 completion 不会
 *             留下陈旧信号。
 *          3. `WritePort` BLOCK 透传最终结果，复用 semaphore 时不受旧信号污染。
 *          Test items:
 *          1. `AsyncBlockWait` distinguishes completion results from caller detachment.
 *          2. A late completion leaves no stale signal after timeout or an underlying
 *             semaphore failure detaches the caller.
 *          3. `WritePort` BLOCK forwards the final result and safely reuses its
 *             semaphore.
 */
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <iterator>

#include "rw_runtime_test_common.hpp"

namespace
{

bool DenyFutexForCurrentThread()
{
  const sock_filter filter[] = {
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
               static_cast<uint32_t>(offsetof(seccomp_data, nr))),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_futex, 0, 1),
      BPF_STMT(BPF_RET | BPF_K,
               SECCOMP_RET_ERRNO | (static_cast<uint32_t>(EIO) & SECCOMP_RET_DATA)),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
  };
  const sock_fprog program = {static_cast<unsigned short>(std::size(filter)),
                              const_cast<sock_filter*>(filter)};

  return prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0 &&
         prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program) == 0;
}

struct AsyncBlockWaitFailureContext
{
  bool filter_installed = false;
  LibXR::ErrorCode result = LibXR::ErrorCode::STATE_ERR;
  bool caller_detached = false;
  bool late_post_claimed = true;
  size_t semaphore_value = 1U;
};

void RunAsyncBlockWaitFailure(int report_fd, LibXR::AsyncBlockWait& wait,
                              LibXR::Semaphore& semaphore)
{
  AsyncBlockWaitFailureContext context;
  context.filter_installed = DenyFutexForCurrentThread();
  if (context.filter_installed)
  {
    context.result = wait.Wait(UINT32_MAX, context.caller_detached);
    context.late_post_claimed = wait.TryPost(false, LibXR::ErrorCode::OK);
    context.semaphore_value = semaphore.Value();
  }

  const ssize_t write_size = write(report_fd, &context, sizeof(context));
  close(report_fd);
  _exit(write_size == static_cast<ssize_t>(sizeof(context)) ? 0 : 1);
}

bool ReadFailureContext(int report_fd, AsyncBlockWaitFailureContext& context)
{
  size_t offset = 0U;
  while (offset < sizeof(context))
  {
    const ssize_t read_size =
        read(report_fd, reinterpret_cast<uint8_t*>(&context) + offset,
             sizeof(context) - offset);
    if (read_size > 0)
    {
      offset += static_cast<size_t>(read_size);
      continue;
    }
    if (read_size < 0 && errno == EINTR)
    {
      continue;
    }
    return false;
  }
  return true;
}

void test_async_block_wait_completion_outcomes()
{
  using namespace LibXR;

  for (const ErrorCode completion_result :
       {ErrorCode::OK, ErrorCode::FAILED, ErrorCode::TIMEOUT})
  {
    Semaphore semaphore;
    AsyncBlockWait wait;
    wait.Start(semaphore);
    ASSERT(wait.TryPost(false, completion_result));

    bool caller_detached = true;
    ASSERT(wait.Wait(0U, caller_detached) == completion_result);
    ASSERT(!caller_detached);
    ASSERT(semaphore.Value() == 0U);
  }
}

void test_async_block_wait_timeout_detaches_caller()
{
  using namespace LibXR;

  Semaphore semaphore;
  AsyncBlockWait wait;
  wait.Start(semaphore);

  bool caller_detached = false;
  ASSERT(wait.Wait(0U, caller_detached) == ErrorCode::TIMEOUT);
  ASSERT(caller_detached);
  ASSERT(!wait.TryPost(false, ErrorCode::OK));
  ASSERT(semaphore.Value() == 0U);

  wait.Start(semaphore);
  ASSERT(wait.TryPost(false, ErrorCode::FAILED));
  caller_detached = true;
  ASSERT(wait.Wait(0U, caller_detached) == ErrorCode::FAILED);
  ASSERT(!caller_detached);
  ASSERT(semaphore.Value() == 0U);
}

void test_async_block_wait_semaphore_failure_detaches_caller()
{
  using namespace LibXR;

  Semaphore semaphore;
  AsyncBlockWait wait;
  wait.Start(semaphore);

  int report_pipe[2];
  REQUIRE(pipe(report_pipe) == 0);
  const pid_t child = fork();
  REQUIRE(child >= 0);
  if (child == 0)
  {
    close(report_pipe[0]);
    RunAsyncBlockWaitFailure(report_pipe[1], wait, semaphore);
  }

  close(report_pipe[1]);
  AsyncBlockWaitFailureContext context;
  const bool report_read = ReadFailureContext(report_pipe[0], context);
  close(report_pipe[0]);

  int child_status = 0;
  pid_t wait_result;
  do
  {
    wait_result = waitpid(child, &child_status, 0);
  } while (wait_result < 0 && errno == EINTR);

  REQUIRE(report_read);
  REQUIRE(wait_result == child);
  REQUIRE(WIFEXITED(child_status));
  REQUIRE(WEXITSTATUS(child_status) == 0);
  REQUIRE(context.filter_installed);
  ASSERT(context.result == ErrorCode::FAILED);
  ASSERT(context.caller_detached);
  ASSERT(!context.late_post_claimed);
  ASSERT(context.semaphore_value == 0U);
}

/**
 * @brief 测试入口函数 `test_rw_write_port_block_pending_result_propagates`。 Test entry
 * function `test_rw_write_port_block_pending_result_propagates`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared
 * in this file in order. 测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。
 * Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_write_port_block_pending_result_propagates()
{
  // 测试内容：阻塞写在完成线程返回失败时，应把该错误直接传给调用者。
  // Test coverage: a blocking write should forward the exact failure returned by the
  // completion thread.
  using namespace LibXR;

  WritePort w(2, 16);
  w = PendingWriteFun;

  static const uint8_t TX[] = {0x5A};
  Semaphore sem;
  WriteOperation op(sem, 100);
  Semaphore done;
  Thread finisher;
  StartWriteFinisher(finisher, w, done, ErrorCode::FAILED, "wr_finish");

  auto ec = w(ConstRawData{TX, sizeof(TX)}, op);
  ASSERT(ec == ErrorCode::FAILED);
  ExpectWaitOk(done, SHORT_WAIT_MS);
  JoinThreadIfNeeded(finisher);
}

/**
 * @brief 测试入口函数 `test_rw_write_port_block_reused_waiter_discards_stale_signal`。
 * Test entry function `test_rw_write_port_block_reused_waiter_discards_stale_signal`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared
 * in this file in order. 测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。
 * Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_write_port_block_reused_waiter_discards_stale_signal()
{
  // 测试内容：复用同一阻塞等待者时，上一轮完成令牌必须被清干净，不能串到下一轮。
  // Test coverage: reusing the same blocking waiter must drain the previous completion
  // token before the next wait cycle.
  using namespace LibXR;

  WritePort w(2, 16);
  w = PendingWriteFun;

  static const uint8_t TX1[] = {0x6B};
  static const uint8_t TX2[] = {0x7C};
  Semaphore sem;
  WriteOperation op(sem, 100);
  Semaphore done1;
  Thread finisher1;
  StartWriteFinisher(finisher1, w, done1, ErrorCode::FAILED, "wr_stale1");

  auto ec = w(ConstRawData{TX1, sizeof(TX1)}, op);
  ASSERT(ec == ErrorCode::FAILED);
  ExpectWaitOk(done1, SHORT_WAIT_MS);
  JoinThreadIfNeeded(finisher1);
  ASSERT(sem.Value() == 0);

  Semaphore done2;
  Thread finisher2;
  StartWriteFinisher(finisher2, w, done2, ErrorCode::OK, "wr_stale2");

  ec = w(ConstRawData{TX2, sizeof(TX2)}, op);
  ASSERT(ec == ErrorCode::OK);
  ExpectWaitOk(done2, SHORT_WAIT_MS);
  JoinThreadIfNeeded(finisher2);
  ASSERT(sem.Value() == 0);
}

}  // namespace

/**
 * @brief 测试项函数 `RunRuntimeRwBlockWaiterTests`。 Test-item function
 * `RunRuntimeRwBlockWaiterTests`.
 * @details 测试内容：执行 runtime `AsyncBlockWait` 与 `WritePort` BLOCK waiter 子场景。
 * Execute runtime `AsyncBlockWait` and `WritePort` BLOCK waiter sub-scenarios.
 * 测试原理：集中验证 completion 与 detach 交接、底层等待失败、最终结果透传及 semaphore
 * 复用。 Group waiter lifecycle checks around completion-versus-detach handoff,
 * underlying wait failure, final-result propagation, and semaphore reuse.
 */
void RunRuntimeRwBlockWaiterTests()
{
  test_async_block_wait_completion_outcomes();
  test_async_block_wait_timeout_detaches_caller();
  test_async_block_wait_semaphore_failure_detaches_caller();
  test_rw_write_port_block_pending_result_propagates();
  test_rw_write_port_block_reused_waiter_discards_stale_signal();
}
