#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_queue()
{
  LibXR::Thread thread1, thread2;
  auto lock_free_queue = LibXR::LockFreeQueue<float>(3);

  thread1.Create<LibXR::LockFreeQueue<float> *>(
      &lock_free_queue,
      [](LibXR::LockFreeQueue<float> *queue)
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

  lock_free_queue.Pop(tmp);
  ASSERT(tmp == 2.1f);

  auto queue = LibXR::LockQueue<float>(3);

  thread2.Create<LibXR::LockQueue<float> *>(
      &queue,
      [](LibXR::LockQueue<float> *queue)
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

  queue.Pop(tmp, 20);
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
