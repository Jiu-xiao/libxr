#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_queue() {
  LibXR::Thread thread1, thread2;
  auto lock_free_queue = LibXR::LockFreeQueue<float>(3);

  thread1.Create<LibXR::LockFreeQueue<float> *>(
      &lock_free_queue,
      [](LibXR::LockFreeQueue<float> *queue) {
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
      [](LibXR::LockQueue<float> *queue) {
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

  pthread_join(thread1, nullptr);
  pthread_join(thread2, nullptr);

  LibXR::ChunkQueue chunk_manager(10, 100);
  uint8_t data[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  uint8_t buffer[20] = {0};
  size_t out_size = 0;

  ASSERT(chunk_manager.CreateNewBlock() == ErrorCode::OK);

  ASSERT(chunk_manager.AppendToCurrentBlock(data, sizeof(data)) ==
         ErrorCode::OK);

  ASSERT(chunk_manager.AppendToCurrentBlock(data, sizeof(data)) ==
         ErrorCode::OK);

  chunk_manager.CreateNewBlock();

  ASSERT(chunk_manager.AppendToCurrentBlock(data, sizeof(data)) ==
         ErrorCode::OK);

  chunk_manager.CreateNewBlock();

  ASSERT(chunk_manager.AppendToCurrentBlock(data, sizeof(data)) ==
         ErrorCode::OK);

  ASSERT(chunk_manager.PopBlock(buffer, &out_size) == ErrorCode::OK);
  ASSERT(out_size == sizeof(data) * 2);
  for (int i = 0; i < sizeof(data); i++) {
    ASSERT(buffer[i] == data[i]);
    ASSERT(buffer[i + 10] == data[i]);

    buffer[i] = 0;
    buffer[i + 10] = 0;
  }

  ASSERT(chunk_manager.PopBlock(buffer, &out_size) == ErrorCode::OK);
  ASSERT(out_size == sizeof(data));
  for (int i = 0; i < sizeof(data); i++) {
    ASSERT(buffer[i] == data[i]);
  }

  for (unsigned char i : data) {
    chunk_manager.Pop(1, buffer);
    ASSERT(buffer[0] == i);
  }

  ASSERT(chunk_manager.AppendToCurrentBlock(data, sizeof(data)) ==
         ErrorCode::OK);

  chunk_manager.CreateNewBlock();

  ASSERT(chunk_manager.AppendToCurrentBlock(data, sizeof(data)) ==
         ErrorCode::OK);

  chunk_manager.Pop(18, buffer);

  for (int i = 0; i < 8; i++) {
    ASSERT(buffer[i] == data[i]);
    ASSERT(buffer[i + 10] == data[i]);

    buffer[i] = 0;
    buffer[i + 10] = 0;
  }

  ASSERT(chunk_manager.Size() == 2);
  ASSERT(chunk_manager.EmptySize() == 98);
}
