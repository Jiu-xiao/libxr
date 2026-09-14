/**
 * @file test_queue.cpp
 * @brief 检查普通队列的满空状态、覆盖写和跨环尾的批量顺序。 /
 * Tests FIFO capacity, overwrite and batch order across wraparound.
 *
 * 另检查三类队列的字节接口和类型关系，以及非默认构造的数据类型。
 * Also checks byte APIs, queue type relationships and non-default payloads.
 */

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"
#include "test_assert.hpp"

namespace
{
struct NoDefaultPayload
{
  explicit NoDefaultPayload(uint32_t value_in) : value(value_in) {}

  uint32_t value;
};
}  // namespace

static_assert(std::is_final_v<LibXR::Queue<int>>);
static_assert(std::is_final_v<LibXR::SPSCQueue<int>>);
static_assert(std::is_final_v<LibXR::MPMCQueue<int>>);
static_assert(std::is_base_of_v<LibXR::QueueBase, LibXR::Queue<int>>);
static_assert(std::is_base_of_v<LibXR::SPSCQueueBase, LibXR::SPSCQueue<int>>);
static_assert(std::is_base_of_v<LibXR::MPMCQueueBase, LibXR::MPMCQueue<int>>);
static_assert(
    std::is_base_of_v<LibXR::QueueTypedBase<LibXR::Queue<int>, int>, LibXR::Queue<int>>);
static_assert(std::is_base_of_v<LibXR::QueueTypedBase<LibXR::SPSCQueue<int>, int>,
                                LibXR::SPSCQueue<int>>);
static_assert(std::is_base_of_v<LibXR::QueueTypedBase<LibXR::MPMCQueue<int>, int>,
                                LibXR::MPMCQueue<int>>);
static_assert(!std::is_default_constructible_v<NoDefaultPayload>);
static_assert(std::is_trivially_copyable_v<NoDefaultPayload>);

void test_queue()
{
  // Direct byte-base usage for the public byte-queue families.
  {
    uint16_t input = 0x1234;
    uint16_t output = 0;

    LibXR::QueueBase queue_base(sizeof(input), 2);
    TEST_ASSERT(queue_base.PushBytes(&input) == LibXR::ErrorCode::OK);
    TEST_ASSERT(queue_base.PopBytes(&output) == LibXR::ErrorCode::OK);
    TEST_ASSERT(output == input);

    output = 0;
    LibXR::SPSCQueueBase spsc_base(sizeof(input), 2);
    TEST_ASSERT(spsc_base.PushBytes(&input) == LibXR::ErrorCode::OK);
    TEST_ASSERT(spsc_base.PopBytes(&output) == LibXR::ErrorCode::OK);
    TEST_ASSERT(output == input);

    output = 0;
    LibXR::MPMCQueueBase mpmc_base(sizeof(input), 2);
    TEST_ASSERT(mpmc_base.PushBytes(&input) == LibXR::ErrorCode::OK);
    TEST_ASSERT(mpmc_base.PopBytes(&output) == LibXR::ErrorCode::OK);
    TEST_ASSERT(output == input);
  }

  // Queue::Overwrite replaces the queue contents with exactly one new element.
  {
    LibXR::Queue<uint32_t> queue(16);
    uint32_t value = 0;

    for (uint32_t item = 0; item < queue.MaxSize(); ++item)
    {
      TEST_ASSERT(queue.Push(item) == LibXR::ErrorCode::OK);
    }
    TEST_ASSERT(queue.Size() == queue.MaxSize());

    TEST_ASSERT(queue.Overwrite(0xA5A5A5A5U) == LibXR::ErrorCode::OK);
    TEST_ASSERT(queue.Size() == 1);
    TEST_ASSERT(queue.Pop(value) == LibXR::ErrorCode::OK);
    TEST_ASSERT(value == 0xA5A5A5A5U);
    TEST_ASSERT(queue.Pop(value) == LibXR::ErrorCode::EMPTY);
  }

  // The ordinary FIFO queue supports capacity 1 and no-default payloads for normal byte
  // Push/Pop.
  {
    LibXR::Queue<uint32_t> queue(1);
    uint32_t value = 0;
    uint32_t batch[1] = {44};

    TEST_ASSERT(queue.Push(11) == LibXR::ErrorCode::OK);
    TEST_ASSERT(queue.Push(22) == LibXR::ErrorCode::FULL);
    TEST_ASSERT(queue.Pop(value) == LibXR::ErrorCode::OK);
    TEST_ASSERT(value == 11);
    TEST_ASSERT(queue.Pop(value) == LibXR::ErrorCode::EMPTY);

    TEST_ASSERT(queue.PushBatch(batch, 1) == LibXR::ErrorCode::OK);
    TEST_ASSERT(queue.PopBatch(&value, 1) == LibXR::ErrorCode::OK);
    TEST_ASSERT(value == 44);

    LibXR::Queue<NoDefaultPayload> no_default_queue(1);
    NoDefaultPayload pushed(88);
    NoDefaultPayload popped(0);
    TEST_ASSERT(no_default_queue.Push(pushed) == LibXR::ErrorCode::OK);
    TEST_ASSERT(no_default_queue.Pop(popped) == LibXR::ErrorCode::OK);
    TEST_ASSERT(popped.value == 88);
  }

  // Batch operations preserve FIFO order across wraparound.
  {
    LibXR::Queue<int> batch_queue(5);

    int initial[5] = {1, 2, 3, 4, 5};
    TEST_ASSERT(batch_queue.PushBatch(initial, 5) == LibXR::ErrorCode::OK);

    int peek_buffer[5] = {};
    TEST_ASSERT(batch_queue.PeekBatch(peek_buffer, 5) == LibXR::ErrorCode::OK);
    for (int i = 0; i < 5; ++i)
    {
      TEST_ASSERT(peek_buffer[i] == initial[i]);
    }

    int dummy[2] = {};
    TEST_ASSERT(batch_queue.PopBatch(dummy, 2) == LibXR::ErrorCode::OK);

    int wrap[2] = {6, 7};
    TEST_ASSERT(batch_queue.PushBatch(wrap, 2) == LibXR::ErrorCode::OK);

    int expected[5] = {3, 4, 5, 6, 7};
    TEST_ASSERT(batch_queue.PeekBatch(peek_buffer, 5) == LibXR::ErrorCode::OK);
    for (int i = 0; i < 5; ++i)
    {
      TEST_ASSERT(peek_buffer[i] == expected[i]);
    }
  }
}
