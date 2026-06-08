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
 * @struct NoDefaultPayload
 * @brief 无默认构造 payload，用于验证普通 Push/Pop 不依赖默认构造
 *        / Payload without default construction, used to verify normal Push/Pop
 *        do not depend on default construction
 */
struct NoDefaultPayload
{
  explicit NoDefaultPayload(uint32_t value_in) : value(value_in) {}

  uint32_t value;  ///< 有效载荷值 / Payload value.
};

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

  // Capacity 1 is a valid SPSC queue and zero-length batches are no-ops.
  {
    Queue queue(1);
    uint32_t value = 0;
    uint32_t batch[1] = {33};

    ASSERT(queue.PushBatch(nullptr, 0) == LibXR::ErrorCode::OK);
    ASSERT(queue.PopBatch(nullptr, 0) == LibXR::ErrorCode::OK);
    ASSERT(queue.PeekBatch(nullptr, 0) == LibXR::ErrorCode::OK);

    ASSERT(queue.Push(11) == LibXR::ErrorCode::OK);
    ASSERT(queue.Push(22) == LibXR::ErrorCode::FULL);
    ASSERT(queue.Peek(value) == LibXR::ErrorCode::OK);
    ASSERT(value == 11);
    ASSERT(queue.Pop(value) == LibXR::ErrorCode::OK);
    ASSERT(value == 11);
    ASSERT(queue.Pop(value) == LibXR::ErrorCode::EMPTY);

    ASSERT(queue.PushBatch(batch, 1) == LibXR::ErrorCode::OK);
    ASSERT(queue.PopBatch(&value, 1) == LibXR::ErrorCode::OK);
    ASSERT(value == 33);
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
    for (size_t index = 0; index < 3; ++index)
    {
      ASSERT(queue.PushWithWriter(
                 [&](uint32_t* slot, size_t count)
                 {
                   ASSERT(count == 1);
                   slot[0] = static_cast<uint32_t>(write_cursor++);
                   return LibXR::ErrorCode::OK;
                 }) == LibXR::ErrorCode::OK);
    }

    uint32_t expected_value = 100;
    for (size_t index = 0; index < 3; ++index)
    {
      ASSERT(queue.PopWithReader(
                 [&](const uint32_t* slot, size_t count)
                 {
                   ASSERT(count == 1);
                   ASSERT(slot[0] == expected_value++);
                   return LibXR::ErrorCode::OK;
                 }) == LibXR::ErrorCode::OK);
    }
    uint32_t value = 0;
    ASSERT(queue.Pop(value) == LibXR::ErrorCode::EMPTY);
  }

  // Normal Push/Pop remain byte-payload operations and do not require Data{}.
  {
    LibXR::SPSCQueue<NoDefaultPayload> queue(2);
    NoDefaultPayload pushed(88);
    NoDefaultPayload popped(0);

    ASSERT(queue.Push(pushed) == LibXR::ErrorCode::OK);
    ASSERT(queue.Pop(popped) == LibXR::ErrorCode::OK);
    ASSERT(popped.value == 88);
  }

  // Callback failures must not commit a partially produced or consumed element.
  {
    Queue queue(2);
    uint32_t value = 0;
    bool writer_called = false;

    ASSERT(queue.PushWithWriter(
               [](uint32_t* slot, size_t count)
               {
                 ASSERT(count == 1);
                 slot[0] = 42;
                 return LibXR::ErrorCode::FAILED;
               }) == LibXR::ErrorCode::FAILED);
    ASSERT(queue.Size() == 0);

    ASSERT(queue.Push(1) == LibXR::ErrorCode::OK);
    ASSERT(queue.Push(2) == LibXR::ErrorCode::OK);
    ASSERT(queue.PushWithWriter(
               [&](uint32_t* slot, size_t count)
               {
                 UNUSED(slot);
                 UNUSED(count);
                 writer_called = true;
                 return LibXR::ErrorCode::OK;
               }) == LibXR::ErrorCode::FULL);
    ASSERT(!writer_called);
    ASSERT(queue.Pop() == LibXR::ErrorCode::OK);
    ASSERT(queue.Pop() == LibXR::ErrorCode::OK);

    ASSERT(queue.Push(55) == LibXR::ErrorCode::OK);
    ASSERT(queue.PopWithReader(
               [](const uint32_t* slot, size_t count)
               {
                 ASSERT(count == 1);
                 ASSERT(slot[0] == 55);
                 return LibXR::ErrorCode::FAILED;
               }) == LibXR::ErrorCode::FAILED);
    ASSERT(queue.Pop(value) == LibXR::ErrorCode::OK);
    ASSERT(value == 55);
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

    while (!producer_done.load(std::memory_order_acquire))
    {
      LibXR::Thread::Yield();
    }
    uint32_t value = 0;
    ASSERT(queue.Pop(value) == LibXR::ErrorCode::EMPTY);
    ASSERT(queue.Size() == 0);
  }
}
