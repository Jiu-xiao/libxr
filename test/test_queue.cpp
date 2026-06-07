#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

#include <atomic>
#include <cstdint>
#include <type_traits>

namespace
{
using SPMCStressQueue = LibXR::SPMCQueue<uint16_t>;

/**
 * @struct SPMCConsumerArg
 * @brief SPMC 压力测试消费者线程参数 / SPMC stress-test consumer arguments
 */
struct SPMCConsumerArg
{
  SPMCStressQueue* queue;                 ///< 共享 SPMC 队列 / Shared SPMC queue.
  size_t total_items;                     ///< 目标总元素数 / Expected total item count.
  std::atomic<bool>* producer_done;       ///< 生产者完成标记 / Producer completion flag.
  std::atomic<size_t>* consumer_done;     ///< 消费者完成计数 / Consumer completion count.
  std::atomic<size_t>* pop_count;         ///< 已消费元素数 / Total consumed item count.
  std::atomic<unsigned long long>* sum;   ///< 已消费元素和 / Sum of consumed values.
  std::atomic<uint8_t>* seen;             ///< 去重标记表 / Deduplication mark table.
};

/**
 * @brief SPMC 压力测试消费者线程入口 / SPMC stress-test consumer thread entry
 */
void SPMCConsumerTask(SPMCConsumerArg arg)
{
  while (true)
  {
    SPMCStressQueue::ValueType value = 0;
    const auto ec = arg.queue->Pop(value);
    if (ec == LibXR::ErrorCode::OK)
    {
      ASSERT(value < arg.total_items);
      const auto previous = arg.seen[value].exchange(1, std::memory_order_relaxed);
      ASSERT(previous == 0);
      arg.sum->fetch_add(value, std::memory_order_relaxed);
      const size_t index = arg.pop_count->fetch_add(1, std::memory_order_relaxed) + 1;
      ASSERT(index <= arg.total_items);
      continue;
    }

    if (arg.producer_done->load(std::memory_order_acquire) &&
        arg.pop_count->load(std::memory_order_acquire) == arg.total_items)
    {
      arg.consumer_done->fetch_add(1, std::memory_order_release);
      return;
    }

    LibXR::Thread::Yield();
  }
}
}  // namespace

static_assert(std::is_final_v<LibXR::Queue<int>>);
static_assert(std::is_final_v<LibXR::SPSCQueue<int>>);
static_assert(std::is_final_v<LibXR::SPMCQueue<int>>);
static_assert(std::is_final_v<LibXR::MPMCQueue<int>>);
static_assert(std::is_base_of_v<LibXR::QueueBase, LibXR::Queue<int>>);
static_assert(std::is_base_of_v<LibXR::SPSCQueueBase, LibXR::SPSCQueue<int>>);
static_assert(std::is_base_of_v<LibXR::SPMCQueueBase, LibXR::SPMCQueue<int>>);
static_assert(std::is_base_of_v<LibXR::MPMCQueueBase, LibXR::MPMCQueue<int>>);
static_assert(std::is_base_of_v<LibXR::QueueTypedBase<LibXR::Queue<int>, int>,
                                LibXR::Queue<int>>);
static_assert(std::is_base_of_v<LibXR::QueueTypedBase<LibXR::SPSCQueue<int>, int>,
                                LibXR::SPSCQueue<int>>);
static_assert(std::is_base_of_v<LibXR::QueueTypedBase<LibXR::SPMCQueue<int>, int>,
                                LibXR::SPMCQueue<int>>);
static_assert(std::is_base_of_v<LibXR::QueueTypedBase<LibXR::MPMCQueue<int>, int>,
                                LibXR::MPMCQueue<int>>);

void test_queue()
{
  // Direct byte-base usage for all queue families.
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
    LibXR::SPMCQueueBase spmc_base(sizeof(input), 2);
    ASSERT(spmc_base.PushBytes(&input) == LibXR::ErrorCode::OK);
    ASSERT(spmc_base.PopBytes(&output) == LibXR::ErrorCode::OK);
    ASSERT(output == input);

    output = 0;
    LibXR::MPMCQueueBase mpmc_base(sizeof(input), 2);
    ASSERT(mpmc_base.PushBytes(&input) == LibXR::ErrorCode::OK);
    ASSERT(mpmc_base.PopBytes(&output) == LibXR::ErrorCode::OK);
    ASSERT(output == input);
  }

  LibXR::Thread thread1;
  static auto spmc_queue = LibXR::SPMCQueue<float>(3);

  thread1.Create<LibXR::SPMCQueue<float>*>(
      &spmc_queue,
      [](LibXR::SPMCQueue<float>* queue)
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
  spmc_queue.Pop(tmp);
  ASSERT(tmp == 1.2f);
  spmc_queue.Pop(tmp);
  ASSERT(tmp == 3.8f);
  LibXR::Thread::Sleep(100);
  spmc_queue.Pop(tmp);
  ASSERT(tmp == 100.f);
  spmc_queue.Pop(tmp);
  ASSERT(tmp == 0.0f);
  spmc_queue.Pop(tmp);
  ASSERT(tmp == 2.1f);

  auto ret = spmc_queue.Pop(tmp);
  ASSERT(ret == LibXR::ErrorCode::EMPTY);
  ASSERT(tmp == 2.1f);

  // Single producer with multiple consumers must not duplicate or lose payloads.
  {
    constexpr size_t CONSUMER_COUNT = 4;
    constexpr size_t TOTAL_ITEMS = 10000;
    constexpr unsigned long long EXPECTED_SUM =
        (static_cast<unsigned long long>(TOTAL_ITEMS) *
         static_cast<unsigned long long>(TOTAL_ITEMS - 1)) /
        2ULL;
    static_assert(TOTAL_ITEMS - 1 <= UINT16_MAX);

    SPMCStressQueue queue(8);
    std::atomic<uint8_t> seen[TOTAL_ITEMS] = {};
    std::atomic<bool> producer_done = false;
    std::atomic<size_t> consumer_done = 0;
    std::atomic<size_t> pop_count = 0;
    std::atomic<unsigned long long> sum = 0;
    LibXR::Thread consumers[CONSUMER_COUNT];

    for (size_t index = 0; index < CONSUMER_COUNT; ++index)
    {
      consumers[index].Create<SPMCConsumerArg>(
          SPMCConsumerArg{&queue, TOTAL_ITEMS, &producer_done, &consumer_done, &pop_count,
                          &sum, seen},
          SPMCConsumerTask, "spmc_cons", 1024, LibXR::Thread::Priority::REALTIME);
    }

    for (size_t value = 0; value < TOTAL_ITEMS; ++value)
    {
      while (queue.Push(static_cast<uint16_t>(value)) != LibXR::ErrorCode::OK)
      {
        LibXR::Thread::Yield();
      }
    }
    producer_done.store(true, std::memory_order_release);

    const uint32_t start_ms = LibXR::Thread::GetTime();
    while ((pop_count.load(std::memory_order_acquire) != TOTAL_ITEMS ||
            consumer_done.load(std::memory_order_acquire) != CONSUMER_COUNT) &&
           (LibXR::Thread::GetTime() - start_ms) < 5000U)
    {
      LibXR::Thread::Sleep(1);
    }

    ASSERT(pop_count.load(std::memory_order_acquire) == TOTAL_ITEMS);
    ASSERT(consumer_done.load(std::memory_order_acquire) == CONSUMER_COUNT);
    ASSERT(sum.load(std::memory_order_acquire) == EXPECTED_SUM);
    for (size_t index = 0; index < TOTAL_ITEMS; ++index)
    {
      ASSERT(seen[index].load(std::memory_order_relaxed) == 1);
    }
  }

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
