#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace
{
using Queue = LibXR::SPSCQueue<uint32_t>;  ///< 默认测试用 32 位 payload 队列 / Default test queue using 32-bit payloads.

/**
 * @struct ProducerArg
 * @brief 生产者线程参数 / Producer-thread arguments
 */
struct ProducerArg
{
  Queue* queue;                      ///< 共享队列 / Shared queue instance.
  uint32_t total_items;              ///< 总推送元素数 / Total items pushed by the producer.
  std::atomic<bool>* producer_done;  ///< 生产者完成标记 / Producer completion flag.
};

/**
 * @brief 生产者线程函数 / Producer-thread entry
 */
void ProducerTask(ProducerArg arg)
{
  for (uint32_t value = 0; value < arg.total_items; ++value)
  {
    while (arg.queue->Push(value) != LibXR::ErrorCode::OK)
    {
      LibXR::Thread::Yield();
    }
  }
  arg.producer_done->store(true, std::memory_order_release);
}
}  // namespace

/**
 * @brief 验证 SPSC 队列的基础行为和单生产者/单消费者语义 / Verify basic behavior
 *        and single-producer/single-consumer semantics of the SPSC queue
 */
void test_spsc_queue()
{
  // Basic push/pop/peek behavior on a tiny queue.
  {
    Queue queue(4);
    uint32_t value = 0;

    ASSERT(queue.MaxSize() == 4);
    ASSERT(queue.Size() == 0);
    ASSERT(queue.EmptySize() == 4);
    ASSERT(queue.Pop(value) == LibXR::ErrorCode::EMPTY);

    ASSERT(queue.Push(11) == LibXR::ErrorCode::OK);
    ASSERT(queue.Push(22) == LibXR::ErrorCode::OK);
    ASSERT(queue.Peek(value) == LibXR::ErrorCode::OK);
    ASSERT(value == 11);
    ASSERT(queue.Pop(value) == LibXR::ErrorCode::OK);
    ASSERT(value == 11);
    ASSERT(queue.Pop(value) == LibXR::ErrorCode::OK);
    ASSERT(value == 22);
    ASSERT(queue.Pop(value) == LibXR::ErrorCode::EMPTY);
  }

  // Batch APIs, wraparound, writer callback, and reset behavior.
  {
    Queue queue(5);
    uint32_t initial[5] = {1, 2, 3, 4, 5};
    uint32_t readback[5] = {};

    ASSERT(queue.PushBatch(initial, 5) == LibXR::ErrorCode::OK);
    ASSERT(queue.PushBatch(initial, 1) == LibXR::ErrorCode::FULL);

    ASSERT(queue.PeekBatch(readback, 5) == LibXR::ErrorCode::OK);
    for (size_t index = 0; index < 5; ++index)
    {
      ASSERT(readback[index] == initial[index]);
    }

    ASSERT(queue.PopBatch(readback, 2) == LibXR::ErrorCode::OK);
    ASSERT(readback[0] == 1);
    ASSERT(readback[1] == 2);

    uint32_t wrap[2] = {6, 7};
    ASSERT(queue.PushBatch(wrap, 2) == LibXR::ErrorCode::OK);

    uint32_t expected[5] = {3, 4, 5, 6, 7};
    ASSERT(queue.PeekBatch(readback, 5) == LibXR::ErrorCode::OK);
    for (size_t index = 0; index < 5; ++index)
    {
      ASSERT(readback[index] == expected[index]);
    }

    size_t write_cursor = 100;
    queue.Reset();
    ASSERT(queue.Size() == 0);
    ASSERT(queue.PushWithWriter(
               3,
               [&](uint32_t* slot, size_t count)
               {
                 ASSERT(count == 1);
                 slot[0] = static_cast<uint32_t>(write_cursor++);
                 return LibXR::ErrorCode::OK;
               }) == LibXR::ErrorCode::OK);

    ASSERT(queue.PopWithReader(
               3,
               [&](const uint32_t* slot, size_t count)
               {
                 ASSERT(count == 1);
                 static uint32_t expected_value = 100;
                 ASSERT(slot[0] == expected_value++);
                 return LibXR::ErrorCode::OK;
               }) == LibXR::ErrorCode::OK);
    uint32_t value = 0;
    ASSERT(queue.Pop(value) == LibXR::ErrorCode::EMPTY);
  }

  // End-to-end producer/consumer handoff under sustained contention.
  {
    constexpr uint32_t TOTAL_ITEMS = 50000;
    Queue queue(8);
    std::atomic<bool> producer_done = false;
    LibXR::Thread producer;

    producer.Create<ProducerArg>(ProducerArg{&queue, TOTAL_ITEMS, &producer_done}, ProducerTask,
                                 "spsc_prod", 1024, LibXR::Thread::Priority::REALTIME);

    for (uint32_t expected = 0; expected < TOTAL_ITEMS; ++expected)
    {
      uint32_t value = UINT32_MAX;
      while (queue.Pop(value) != LibXR::ErrorCode::OK)
      {
        LibXR::Thread::Yield();
      }
      ASSERT(value == expected);
    }

    ASSERT(producer_done.load(std::memory_order_acquire));
    uint32_t value = 0;
    ASSERT(queue.Pop(value) == LibXR::ErrorCode::EMPTY);
    ASSERT(queue.Size() == 0);
  }
}
