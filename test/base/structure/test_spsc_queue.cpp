#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

namespace
{
using Queue = LibXR::SPSCQueue<uint32_t>;

struct NoDefaultPayload
{
  explicit NoDefaultPayload(uint32_t value_in) : value(value_in) {}

  uint32_t value;
};

struct ProducerArg
{
  Queue* queue;
  uint32_t total_items;
  std::atomic<bool>* producer_done;
};

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

static_assert(!std::is_default_constructible_v<NoDefaultPayload>);
static_assert(std::is_trivially_copyable_v<NoDefaultPayload>);
static_assert(std::is_same_v<LibXR::SPSCQueueBase::IndexType, uint32_t>);

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

  // Capacity 1 is valid and zero-length batches are no-ops.
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

  // Callback failures must not commit partially produced or consumed elements.
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

  // Batched byte callbacks receive contiguous chunks even when the ring wraps.
  {
    LibXR::SPSCQueue<uint8_t> byte_queue(6);
    uint8_t value = 0;
    const uint8_t initial[5] = {10, 11, 12, 13, 14};

    ASSERT(byte_queue.PushBatch(initial, 5) == LibXR::ErrorCode::OK);
    for (uint8_t expected = 10; expected < 13; ++expected)
    {
      ASSERT(byte_queue.Pop(value) == LibXR::ErrorCode::OK);
      ASSERT(value == expected);
    }

    uint8_t write_next = 20;
    size_t write_chunks = 0;
    ASSERT(byte_queue.PushWithWriter(4,
                                     [&](uint8_t* chunk, size_t count)
                                     {
                                       ASSERT(count == 2);
                                       ++write_chunks;
                                       for (size_t index = 0; index < count; ++index)
                                       {
                                         chunk[index] = write_next++;
                                       }
                                       return LibXR::ErrorCode::OK;
                                     }) == LibXR::ErrorCode::OK);
    ASSERT(write_chunks == 2);

    const uint8_t expected_after_write[6] = {13, 14, 20, 21, 22, 23};
    uint8_t readback[6] = {};
    ASSERT(byte_queue.PopBatch(readback, 6) == LibXR::ErrorCode::OK);
    for (size_t index = 0; index < 6; ++index)
    {
      ASSERT(readback[index] == expected_after_write[index]);
    }

    const uint8_t full[6] = {1, 2, 3, 4, 5, 6};
    const uint8_t wrap[4] = {7, 8, 9, 10};
    ASSERT(byte_queue.PushBatch(full, 6) == LibXR::ErrorCode::OK);
    for (size_t index = 0; index < 4; ++index)
    {
      ASSERT(byte_queue.Pop(value) == LibXR::ErrorCode::OK);
      ASSERT(value == static_cast<uint8_t>(index + 1));
    }
    ASSERT(byte_queue.PushBatch(wrap, 4) == LibXR::ErrorCode::OK);

    const uint8_t expected_read_chunks[5] = {5, 6, 7, 8, 9};
    size_t read_cursor = 0;
    size_t read_chunks = 0;
    ASSERT(byte_queue.PopWithReader(5,
                                    [&](const uint8_t* chunk, size_t count)
                                    {
                                      ASSERT((read_chunks == 0 && count == 1) ||
                                             (read_chunks == 1 && count == 4));
                                      for (size_t index = 0; index < count; ++index)
                                      {
                                        ASSERT(chunk[index] ==
                                               expected_read_chunks[read_cursor++]);
                                      }
                                      ++read_chunks;
                                      return LibXR::ErrorCode::OK;
                                    }) == LibXR::ErrorCode::OK);
    ASSERT(read_chunks == 2);
    ASSERT(read_cursor == 5);
    ASSERT(byte_queue.Pop(value) == LibXR::ErrorCode::OK);
    ASSERT(value == 10);
    ASSERT(byte_queue.Pop(value) == LibXR::ErrorCode::EMPTY);
  }

  // Partial consumption exposes at most two spans through one callback invocation.
  {
    LibXR::SPSCQueue<uint8_t> byte_queue(6);
    const uint8_t initial[4] = {1, 2, 3, 4};
    size_t calls = 0;

    ASSERT(byte_queue.PushBatch(initial, 4) == LibXR::ErrorCode::OK);
    ASSERT(byte_queue.ConsumeWithReader(3,
                                        [&](const uint8_t* first, size_t first_count,
                                            const uint8_t* second, size_t second_count)
                                        {
                                          ++calls;
                                          ASSERT(first_count == 3);
                                          ASSERT(second == nullptr);
                                          ASSERT(second_count == 0);
                                          ASSERT(first[0] == 1);
                                          return 0U;
                                        }) == 0);
    ASSERT(calls == 1);
    ASSERT(byte_queue.Size() == 4);

    ASSERT(byte_queue.ConsumeWithReader(3,
                                        [&](const uint8_t* first, size_t first_count,
                                            const uint8_t* second, size_t second_count)
                                        {
                                          ++calls;
                                          ASSERT(first_count == 3);
                                          ASSERT(second == nullptr);
                                          ASSERT(second_count == 0);
                                          ASSERT(first[0] == 1);
                                          ASSERT(first[1] == 2);
                                          ASSERT(first[2] == 3);
                                          return 2U;
                                        }) == 2);
    ASSERT(calls == 2);
    ASSERT(byte_queue.Size() == 2);

    uint8_t readback[2] = {};
    ASSERT(byte_queue.PopBatch(readback, 2) == LibXR::ErrorCode::OK);
    ASSERT(readback[0] == 3);
    ASSERT(readback[1] == 4);
  }

  // A wrapped prefix is still presented once, and only the accepted prefix advances.
  {
    LibXR::SPSCQueue<uint8_t> byte_queue(6);
    const uint8_t initial[6] = {1, 2, 3, 4, 5, 6};
    const uint8_t wrap[4] = {7, 8, 9, 10};
    uint8_t value = 0;
    size_t calls = 0;

    ASSERT(byte_queue.PushBatch(initial, 6) == LibXR::ErrorCode::OK);
    for (uint8_t expected = 1; expected <= 5; ++expected)
    {
      ASSERT(byte_queue.Pop(value) == LibXR::ErrorCode::OK);
      ASSERT(value == expected);
    }
    ASSERT(byte_queue.PushBatch(wrap, 4) == LibXR::ErrorCode::OK);

    ASSERT(byte_queue.ConsumeWithReader(5,
                                        [&](const uint8_t* first, size_t first_count,
                                            const uint8_t* second, size_t second_count)
                                        {
                                          ++calls;
                                          ASSERT(first_count == 2);
                                          ASSERT(second_count == 3);
                                          ASSERT(first[0] == 6);
                                          ASSERT(first[1] == 7);
                                          ASSERT(second[0] == 8);
                                          ASSERT(second[1] == 9);
                                          ASSERT(second[2] == 10);
                                          return 4U;
                                        }) == 4);
    ASSERT(calls == 1);
    ASSERT(byte_queue.Pop(value) == LibXR::ErrorCode::OK);
    ASSERT(value == 10);
    ASSERT(byte_queue.Pop(value) == LibXR::ErrorCode::EMPTY);
  }

  // Zero offers skip the callback, and a large limit is clamped to available data.
  {
    Queue queue(3);
    size_t calls = 0;

    ASSERT(queue.ConsumeWithReader(1,
                                   [&](const uint32_t*, size_t, const uint32_t*, size_t)
                                   {
                                     ++calls;
                                     return 0U;
                                   }) == 0);
    ASSERT(queue.Push(11) == LibXR::ErrorCode::OK);
    ASSERT(queue.Push(22) == LibXR::ErrorCode::OK);
    ASSERT(queue.ConsumeWithReader(0,
                                   [&](const uint32_t*, size_t, const uint32_t*, size_t)
                                   {
                                     ++calls;
                                     return 0U;
                                   }) == 0);
    ASSERT(calls == 0);

    ASSERT(queue.ConsumeWithReader(100,
                                   [&](const uint32_t* first, size_t first_count,
                                       const uint32_t* second, size_t second_count)
                                   {
                                     ++calls;
                                     ASSERT(first_count == 2);
                                     ASSERT(first[0] == 11);
                                     ASSERT(first[1] == 22);
                                     ASSERT(second == nullptr);
                                     ASSERT(second_count == 0);
                                     return first_count + second_count;
                                   }) == 2);
    ASSERT(calls == 1);
    ASSERT(queue.Size() == 0);
  }

  // End-to-end producer/consumer handoff under sustained contention.
  {
    constexpr uint32_t TOTAL_ITEMS = 50000;
    Queue queue(8);
    std::atomic<bool> producer_done = false;
    LibXR::Thread producer;

    producer.Create<ProducerArg>(ProducerArg{&queue, TOTAL_ITEMS, &producer_done},
                                 ProducerTask, "spsc_prod", 1024,
                                 LibXR::Thread::Priority::REALTIME);

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
    ASSERT(producer.Join() == LibXR::ErrorCode::OK);
    uint32_t value = 0;
    ASSERT(queue.Pop(value) == LibXR::ErrorCode::EMPTY);
    ASSERT(queue.Size() == 0);
  }
}
