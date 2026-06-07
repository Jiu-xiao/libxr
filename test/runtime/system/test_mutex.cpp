/**
 * @file test_mutex.cpp
 * @brief runtime mutex 加锁、try-lock 与 waiter handoff 测试。 Runtime mutex lock, try-lock and waiter handoff tests.
 *
 * 测试项目 / Test items:
 * 1. 立即加锁与 `TryLock()` busy 返回。 Immediate lock semantics: verify `Lock()` succeeds and `TryLock()` reports `BUSY` while held.
 * 2. 阻塞 waiter 的所有权交接。 Waiter handoff: verify a blocked runtime thread acquires the mutex only after the owner unlocks it.
 * 3. 交接后的再次复用。 Post-handoff reuse: verify `TryLock()` succeeds again after the waiter completes.
 *
 * 测试原理 / Test principles:
 * 1. 使用真实 waiter 线程和完成信号量，让所有权交接在 runtime 同步路径上被验证。 Use a real waiter thread and a completion semaphore so ownership transfer is checked on the runtime synchronization path itself.
 */
#include <atomic>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

namespace
{
struct MutexAcquireContext
{
  LibXR::Mutex* mutex;
  std::atomic<bool>* acquired;
  LibXR::Semaphore* done;
};

/**
 * @brief 辅助函数 `AcquireMutex`。 Helper function `AcquireMutex`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
void AcquireMutex(MutexAcquireContext* ctx)
{
  ASSERT(ctx->mutex->Lock() == LibXR::ErrorCode::OK);
  ctx->acquired->store(true, std::memory_order_release);
  ctx->mutex->Unlock();
  ctx->done->Post();
}

}  // namespace

/**
 * @brief 测试入口函数 `test_mutex`。 Test entry function `test_mutex`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_mutex()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  LibXR::Mutex mutex;
  LibXR::Semaphore done(0);
  std::atomic<bool> acquired(false);
  LibXR::Thread waiter;

  ASSERT(mutex.Lock() == LibXR::ErrorCode::OK);
  ASSERT(mutex.TryLock() == LibXR::ErrorCode::BUSY);

  MutexAcquireContext ctx = {&mutex, &acquired, &done};
  waiter.Create<MutexAcquireContext*>(&ctx, AcquireMutex, "mutex_waiter", 1024,
                                     LibXR::Thread::Priority::MEDIUM);

  LibXR::Thread::Sleep(20);
  ASSERT(!acquired.load(std::memory_order_acquire));

  mutex.Unlock();

  ASSERT(done.Wait(500) == LibXR::ErrorCode::OK);
  LibXR::Thread::Sleep(1);
  ASSERT(acquired.load(std::memory_order_acquire));

  ASSERT(mutex.TryLock() == LibXR::ErrorCode::OK);
  mutex.Unlock();
}
