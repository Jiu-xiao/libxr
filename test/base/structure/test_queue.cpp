/**
 * @file test_queue.cpp
 * @brief 公开队列家族的基础字节语义与 FIFO helper 测试。 Tests for public queue-family byte semantics and FIFO helpers.
 *
 * 测试项目 / Test items:
 * 1. `QueueBase` / `SPSCQueueBase` / `MPMCQueueBase` 的字节级 push/pop。 Byte-level push/pop on the public queue bases.
 * 2. 普通 `Queue` 的 `Overwrite()`、capacity-1 和无默认构造 payload 行为。 Ordinary `Queue` overwrite, capacity-1, and no-default-payload behavior.
 * 3. 普通 `Queue` 的批量 push/pop/peek wraparound 顺序。 FIFO wraparound ordering for ordinary `Queue` batch helpers.
 *
 * 测试原理 / Test principles:
 * 1. 主线现在公开了 queue-base 层，因此要直接验证 byte API，而不只验证 typed wrapper。 The mainline now exposes the queue-base layer, so verify the byte API directly instead of only the typed wrappers.
 * 2. 保留普通 `Queue` 的 typed FIFO 验证，因为它仍然是 object-pool 和通用测试里的基础构件。 Keep typed FIFO checks on ordinary `Queue`, because it remains a foundational building block for object-pool and general tests.
 */
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

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
static_assert(std::is_base_of_v<LibXR::QueueTypedBase<LibXR::Queue<int>, int>,
                                LibXR::Queue<int>>);
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
    ASSERT(queue_base.PushBytes(&input) == LibXR::ErrorCode::OK);
    ASSERT(queue_base.PopBytes(&output) == LibXR::ErrorCode::OK);
    ASSERT(output == input);

    output = 0;
    LibXR::SPSCQueueBase spsc_base(sizeof(input), 2);
    ASSERT(spsc_base.PushBytes(&input) == LibXR::ErrorCode::OK);
    ASSERT(spsc_base.PopBytes(&output) == LibXR::ErrorCode::OK);
    ASSERT(output == input);

    output = 0;
    LibXR::MPMCQueueBase mpmc_base(sizeof(input), 2);
    ASSERT(mpmc_base.PushBytes(&input) == LibXR::ErrorCode::OK);
    ASSERT(mpmc_base.PopBytes(&output) == LibXR::ErrorCode::OK);
    ASSERT(output == input);
  }

  // Queue::Overwrite replaces the queue contents with exactly one new element.
  {
    LibXR::Queue<uint32_t> queue(16);
    uint32_t value = 0;

    for (uint32_t item = 0; item < queue.MaxSize(); ++item)
    {
      ASSERT(queue.Push(item) == LibXR::ErrorCode::OK);
    }
    ASSERT(queue.Size() == queue.MaxSize());

    ASSERT(queue.Overwrite(0xA5A5A5A5U) == LibXR::ErrorCode::OK);
    ASSERT(queue.Size() == 1);
    ASSERT(queue.Pop(value) == LibXR::ErrorCode::OK);
    ASSERT(value == 0xA5A5A5A5U);
    ASSERT(queue.Pop(value) == LibXR::ErrorCode::EMPTY);
  }

  // The ordinary FIFO queue supports capacity 1 and no-default payloads for normal byte Push/Pop.
  {
    LibXR::Queue<uint32_t> queue(1);
    uint32_t value = 0;
    uint32_t batch[1] = {44};

    ASSERT(queue.Push(11) == LibXR::ErrorCode::OK);
    ASSERT(queue.Push(22) == LibXR::ErrorCode::FULL);
    ASSERT(queue.Pop(value) == LibXR::ErrorCode::OK);
    ASSERT(value == 11);
    ASSERT(queue.Pop(value) == LibXR::ErrorCode::EMPTY);

    ASSERT(queue.PushBatch(batch, 1) == LibXR::ErrorCode::OK);
    ASSERT(queue.PopBatch(&value, 1) == LibXR::ErrorCode::OK);
    ASSERT(value == 44);

    LibXR::Queue<NoDefaultPayload> no_default_queue(1);
    NoDefaultPayload pushed(88);
    NoDefaultPayload popped(0);
    ASSERT(no_default_queue.Push(pushed) == LibXR::ErrorCode::OK);
    ASSERT(no_default_queue.Pop(popped) == LibXR::ErrorCode::OK);
    ASSERT(popped.value == 88);
  }

  // Batch operations preserve FIFO order across wraparound.
  {
    LibXR::Queue<int> batch_queue(5);

    int initial[5] = {1, 2, 3, 4, 5};
    ASSERT(batch_queue.PushBatch(initial, 5) == LibXR::ErrorCode::OK);

    int peek_buffer[5] = {};
    ASSERT(batch_queue.PeekBatch(peek_buffer, 5) == LibXR::ErrorCode::OK);
    for (int i = 0; i < 5; ++i)
    {
      ASSERT(peek_buffer[i] == initial[i]);
    }

    int dummy[2] = {};
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
}
