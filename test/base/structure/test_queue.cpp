/**
 * @file test_queue.cpp
 * @brief 队列家族的无锁、阻塞与批量操作测试。 Queue-family tests for lock-free, blocking and batch queue operations.
 *
 * 测试项目 / Test items:
 * 1. `LockFreeQueue` 线程间 push/pop 顺序。 `LockFreeQueue` threaded push/pop: verify producer/consumer transfer order and empty detection.
 * 2. `LockQueue` 的阻塞等待与超时。 `LockQueue` blocking waits: verify timed pop waits for producer posts and reports timeout when no data arrives.
 * 3. 普通 `Queue` 的批量 push/pop/peek。 Plain `Queue` batch helpers: verify `PushBatch()`, `PopBatch()` and `PeekBatch()` preserve wraparound ordering.
 *
 * 测试原理 / Test principles:
 * 1. 按调用方真实使用方式分别驱动不同队列家族，因为它们的差异主要在同步语义。 Exercise each queue family through the API style callers actually use, because these types differ mainly in synchronization semantics.
 * 2. 显式验证 wraparound 后的顺序，避免只验证容量不验证 FIFO。 Validate ordering after wraparound, since queue correctness is not just capacity but FIFO preservation under rollover.
 */
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

/**
 * @brief 测试入口函数 `test_queue`。 Test entry function `test_queue`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_queue()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  LibXR::Thread thread1, thread2;
  static auto lock_free_queue = LibXR::LockFreeQueue<float>(3);

  thread1.Create<LibXR::LockFreeQueue<float>*>(
      &lock_free_queue,
      [](LibXR::LockFreeQueue<float>* queue)
      {
        queue->Push(1.2f);
        queue->Push(3.8f);
        LibXR::Thread::Sleep(150);
        queue->Push(100.f);
        queue->Push(0.0f);
        queue->Push(2.1f);
        return;
      },
      "test_task", 512, LibXR::Thread::Priority::REALTIME);
  float tmp = 0.0f;

  LibXR::Thread::Sleep(100);
  lock_free_queue.Pop(tmp);
  ASSERT(tmp == 1.2f);
  lock_free_queue.Pop(tmp);
  ASSERT(tmp == 3.8f);
  LibXR::Thread::Sleep(100);
  lock_free_queue.Pop(tmp);
  ASSERT(tmp == 100.f);
  lock_free_queue.Pop(tmp);
  ASSERT(tmp == 0.0f);
  lock_free_queue.Pop(tmp);
  ASSERT(tmp == 2.1f);

  auto ret = lock_free_queue.Pop(tmp);
  ASSERT(ret == LibXR::ErrorCode::EMPTY);
  ASSERT(tmp == 2.1f);

  static auto queue = LibXR::LockQueue<float>(3);

  thread2.Create<LibXR::LockQueue<float>*>(
      &queue,
      [](LibXR::LockQueue<float>* queue)
      {
        LibXR::Thread::Sleep(100);
        queue->Push(1.2f);
        LibXR::Thread::Sleep(10);
        queue->Push(3.8f);
        LibXR::Thread::Sleep(10);
        queue->Push(100.f);
        LibXR::Thread::Sleep(10);
        queue->Push(0.0f);
        LibXR::Thread::Sleep(10);
        queue->Push(2.1f);
        return;
      },
      "test_task", 512, LibXR::Thread::Priority::REALTIME);
  tmp = 0.0f;

  queue.Pop(tmp, 200);
  ASSERT(tmp == 1.2f);
  queue.Pop(tmp, 20);
  ASSERT(tmp == 3.8f);
  queue.Pop(tmp, 20);
  ASSERT(tmp == 100.f);
  queue.Pop(tmp, 20);
  ASSERT(tmp == 0.0f);
  queue.Pop(tmp, 20);
  ASSERT(tmp == 2.1f);

  auto ret2 = queue.Pop(tmp, 20);
  ASSERT(ret2 != LibXR::ErrorCode::OK);
  ASSERT(tmp == 2.1f);

  // Test batch operations on the basic Queue implementation
  LibXR::Queue<int> batch_queue(5);

  int initial[5] = {1, 2, 3, 4, 5};
  ASSERT(batch_queue.PushBatch(initial, 5) == LibXR::ErrorCode::OK);

  int peek_buffer[5] = {};
  ASSERT(batch_queue.PeekBatch(peek_buffer, 5) == LibXR::ErrorCode::OK);
  for (int i = 0; i < 5; ++i)
  {
    ASSERT(peek_buffer[i] == initial[i]);
  }

  int dummy[2];
  ASSERT(batch_queue.PopBatch(dummy, 2) == LibXR::ErrorCode::OK);

  int wrap[2] = {6, 7};
  ASSERT(batch_queue.PushBatch(wrap, 2) == LibXR::ErrorCode::OK);

  int expected[5] = {3, 4, 5, 6, 7};
  ASSERT(batch_queue.PeekBatch(peek_buffer, 5) == LibXR::ErrorCode::OK);
  for (int i = 0; i < 5; ++i)
  {
    ASSERT(peek_buffer[i] == expected[i]);
  }
}
