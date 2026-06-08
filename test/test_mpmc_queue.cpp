#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace
{
using Queue = LibXR::MPMCQueue<uint16_t>;  ///< 默认压力用 16 位 payload 队列 / Default stress-test queue using 16-bit payloads.

/**
 * @brief 比较两个双精度值是否足够接近 / Check whether two doubles are close enough
 */
bool EqualDouble(double a, double b) { return std::abs(a - b) < 1e-9; }

/**
 * @struct ProducerArg
 * @brief 生产者线程参数 / Producer-thread arguments
 */
struct ProducerArg
{
  size_t begin;                    ///< 本生产者负责的起始值 / First value owned by this producer.
  size_t count;                    ///< 本生产者负责推送的元素数 / Number of items pushed by this producer.
  Queue* queue;                    ///< 共享队列 / Shared queue instance.
  std::atomic<size_t>* done_count;  ///< 完成计数器 / Producer completion counter.
};

/**
 * @struct ConsumerArg
 * @brief 消费者线程参数 / Consumer-thread arguments
 */
struct ConsumerArg
{
  Queue* queue;                                 ///< 共享队列 / Shared queue instance.
  size_t total_items;                           ///< 目标总元素数 / Expected total item count.
  size_t producer_count;                        ///< 生产者总数 / Total number of producers.
  std::atomic<size_t>* produced_done_count;     ///< 生产者完成计数 / Producer completion counter.
  std::atomic<size_t>* consumed_done_count;     ///< 消费者完成计数 / Consumer completion counter.
  std::atomic<size_t>* pop_count;               ///< 已消费元素数 / Total consumed item count.
  std::atomic<unsigned long long>* pop_sum;     ///< 已消费元素和 / Running sum of consumed values.
  std::atomic<uint8_t>* seen;                   ///< 去重标记表 / Deduplication mark table.
};

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
 * @brief 生产者线程函数 / Producer-thread entry
 */
void ProducerTask(ProducerArg arg)
{
  for (size_t offset = 0; offset < arg.count; ++offset)
  {
    const size_t value = arg.begin + offset;
    ASSERT(value <= UINT16_MAX);
    while (arg.queue->Push(static_cast<uint16_t>(value)) != LibXR::ErrorCode::OK)
    {
      LibXR::Thread::Yield();
    }
  }

  arg.done_count->fetch_add(1, std::memory_order_release);
}

/**
 * @brief 消费者线程函数 / Consumer-thread entry
 */
void ConsumerTask(ConsumerArg arg)
{
  while (true)
  {
    Queue::ValueType value = 0;
    const auto ec = arg.queue->Pop(value);
    if (ec == LibXR::ErrorCode::OK)
    {
      ASSERT(value < arg.total_items);
      const auto previous = arg.seen[value].exchange(1, std::memory_order_relaxed);
      ASSERT(previous == 0);

      arg.pop_sum->fetch_add(static_cast<unsigned long long>(value),
                             std::memory_order_relaxed);
      const size_t pop_index = arg.pop_count->fetch_add(1, std::memory_order_relaxed) + 1;
      ASSERT(pop_index <= arg.total_items);
      continue;
    }

    if (arg.produced_done_count->load(std::memory_order_acquire) == arg.producer_count &&
        arg.pop_count->load(std::memory_order_acquire) == arg.total_items)
    {
      break;
    }

    LibXR::Thread::Yield();
  }

  arg.consumed_done_count->fetch_add(1, std::memory_order_release);
}
}  // namespace

static_assert(!std::is_default_constructible_v<NoDefaultPayload>);
static_assert(std::is_trivially_copyable_v<NoDefaultPayload>);

/**
 * @brief 验证有界 MPMC 队列的基础行为和并发语义 / Verify basic behavior and
 *        concurrent semantics of the bounded MPMC queue
 */
void test_mpmc_queue()
{
  // Basic empty/full behavior on a tiny queue.
  {
    Queue queue(3);
    Queue::ValueType value = 0;

    ASSERT(queue.MaxSize() == 3);
    ASSERT(queue.Size() == 0);
    ASSERT(queue.EmptySize() == 3);
    ASSERT(queue.Pop(value) == LibXR::ErrorCode::EMPTY);

    ASSERT(queue.Push(11) == LibXR::ErrorCode::OK);
    ASSERT(queue.Push(22) == LibXR::ErrorCode::OK);
    ASSERT(queue.Push(33) == LibXR::ErrorCode::OK);
    ASSERT(queue.Push(44) == LibXR::ErrorCode::FULL);
    ASSERT(queue.Size() == 3);
    ASSERT(queue.EmptySize() == 0);

    ASSERT(queue.Pop(value) == LibXR::ErrorCode::OK);
    ASSERT(value == 11);
    ASSERT(queue.Pop(value) == LibXR::ErrorCode::OK);
    ASSERT(value == 22);
    ASSERT(queue.Pop(value) == LibXR::ErrorCode::OK);
    ASSERT(value == 33);
    ASSERT(queue.Pop(value) == LibXR::ErrorCode::EMPTY);
    ASSERT(queue.Size() == 0);
    ASSERT(queue.EmptySize() == 3);
  }

  // Repeat fill-and-drain cycles to verify FIFO order stays stable.
  {
    Queue queue(4);
    Queue::ValueType value = 0;

    for (size_t round = 0; round < 256; ++round)
    {
      ASSERT(queue.Push(round * 4 + 0) == LibXR::ErrorCode::OK);
      ASSERT(queue.Push(round * 4 + 1) == LibXR::ErrorCode::OK);
      ASSERT(queue.Push(round * 4 + 2) == LibXR::ErrorCode::OK);
      ASSERT(queue.Push(round * 4 + 3) == LibXR::ErrorCode::OK);
      ASSERT(queue.Push(9999) == LibXR::ErrorCode::FULL);

      ASSERT(queue.Pop(value) == LibXR::ErrorCode::OK);
      ASSERT(value == round * 4 + 0);
      ASSERT(queue.Pop(value) == LibXR::ErrorCode::OK);
      ASSERT(value == round * 4 + 1);
      ASSERT(queue.Pop(value) == LibXR::ErrorCode::OK);
      ASSERT(value == round * 4 + 2);
      ASSERT(queue.Pop(value) == LibXR::ErrorCode::OK);
      ASSERT(value == round * 4 + 3);
      ASSERT(queue.Pop(value) == LibXR::ErrorCode::EMPTY);
    }
  }

  // Verify naturally aligned non-integer payloads also work.
  {
    LibXR::MPMCQueue<double> queue(2);
    double value = 0.0;

    ASSERT(queue.Push(1.25) == LibXR::ErrorCode::OK);
    ASSERT(queue.Push(2.5) == LibXR::ErrorCode::OK);
    ASSERT(queue.Push(3.75) == LibXR::ErrorCode::FULL);

    ASSERT(queue.Pop(value) == LibXR::ErrorCode::OK);
    ASSERT(EqualDouble(value, 1.25));
    ASSERT(queue.Pop(value) == LibXR::ErrorCode::OK);
    ASSERT(EqualDouble(value, 2.5));
    ASSERT(queue.Pop(value) == LibXR::ErrorCode::EMPTY);
  }

  // Normal Push/Pop use byte payloads and do not require Payload{}.
  {
    LibXR::MPMCQueue<NoDefaultPayload> queue(2);
    NoDefaultPayload pushed(77);
    NoDefaultPayload popped(0);

    ASSERT(queue.Push(pushed) == LibXR::ErrorCode::OK);
    ASSERT(queue.Pop(popped) == LibXR::ErrorCode::OK);
    ASSERT(popped.value == 77);
    ASSERT(queue.Pop(popped) == LibXR::ErrorCode::EMPTY);
  }

  // Two producers and two consumers with the smallest legal capacity.
  {
    constexpr size_t PRODUCER_COUNT = 2;
    constexpr size_t CONSUMER_COUNT = 2;
    constexpr size_t ITEMS_PER_PRODUCER = 5000;
    constexpr size_t TOTAL_ITEMS = PRODUCER_COUNT * ITEMS_PER_PRODUCER;
    constexpr unsigned long long EXPECTED_SUM =
        (static_cast<unsigned long long>(TOTAL_ITEMS) *
         static_cast<unsigned long long>(TOTAL_ITEMS - 1)) /
        2ULL;
    static_assert(TOTAL_ITEMS - 1 <= UINT16_MAX);

    Queue queue(2);
    std::atomic<uint8_t> seen[TOTAL_ITEMS] = {};
    std::atomic<size_t> produced_done_count = 0;
    std::atomic<size_t> consumed_done_count = 0;
    std::atomic<size_t> pop_count = 0;
    std::atomic<unsigned long long> pop_sum = 0;
    Queue::ValueType value = 0;

    LibXR::Thread producers[PRODUCER_COUNT];
    LibXR::Thread consumers[CONSUMER_COUNT];

    for (size_t index = 0; index < PRODUCER_COUNT; ++index)
    {
      producers[index].Create<ProducerArg>(
          ProducerArg{index * ITEMS_PER_PRODUCER, ITEMS_PER_PRODUCER, &queue,
                      &produced_done_count},
          ProducerTask, "mpmc_prod2", 1024, LibXR::Thread::Priority::REALTIME);
    }

    for (size_t index = 0; index < CONSUMER_COUNT; ++index)
    {
      consumers[index].Create<ConsumerArg>(
          ConsumerArg{&queue, TOTAL_ITEMS, PRODUCER_COUNT, &produced_done_count,
                      &consumed_done_count, &pop_count, &pop_sum, seen},
          ConsumerTask, "mpmc_cons2", 1024, LibXR::Thread::Priority::REALTIME);
    }

    const uint32_t start_ms = LibXR::Thread::GetTime();
    while ((produced_done_count.load(std::memory_order_acquire) != PRODUCER_COUNT ||
            consumed_done_count.load(std::memory_order_acquire) != CONSUMER_COUNT) &&
           (LibXR::Thread::GetTime() - start_ms) < 5000U)
    {
      LibXR::Thread::Sleep(1);
    }

    ASSERT(produced_done_count.load(std::memory_order_acquire) == PRODUCER_COUNT);
    ASSERT(consumed_done_count.load(std::memory_order_acquire) == CONSUMER_COUNT);
    ASSERT(pop_count.load(std::memory_order_acquire) == TOTAL_ITEMS);
    ASSERT(pop_sum.load(std::memory_order_acquire) == EXPECTED_SUM);
    ASSERT(queue.Pop(value) == LibXR::ErrorCode::EMPTY);

    for (size_t index = 0; index < TOTAL_ITEMS; ++index)
    {
      ASSERT(seen[index].load(std::memory_order_relaxed) == 1);
    }
  }

  // Wider producer/consumer fan-in/fan-out on a moderate queue capacity.
  {
    constexpr size_t PRODUCER_COUNT = 4;
    constexpr size_t CONSUMER_COUNT = 4;
    constexpr size_t ITEMS_PER_PRODUCER = 2000;
    constexpr size_t TOTAL_ITEMS = PRODUCER_COUNT * ITEMS_PER_PRODUCER;
    constexpr unsigned long long EXPECTED_SUM =
        (static_cast<unsigned long long>(TOTAL_ITEMS) *
         static_cast<unsigned long long>(TOTAL_ITEMS - 1)) /
        2ULL;
    static_assert(TOTAL_ITEMS - 1 <= UINT16_MAX);

    Queue queue(64);
    std::atomic<uint8_t> seen[TOTAL_ITEMS] = {};
    std::atomic<size_t> produced_done_count = 0;
    std::atomic<size_t> consumed_done_count = 0;
    std::atomic<size_t> pop_count = 0;
    std::atomic<unsigned long long> pop_sum = 0;
    Queue::ValueType value = 0;

    LibXR::Thread producers[PRODUCER_COUNT];
    LibXR::Thread consumers[CONSUMER_COUNT];

    for (size_t index = 0; index < PRODUCER_COUNT; ++index)
    {
      producers[index].Create<ProducerArg>(
          ProducerArg{index * ITEMS_PER_PRODUCER, ITEMS_PER_PRODUCER, &queue,
                      &produced_done_count},
          ProducerTask, "mpmc_prod", 1024, LibXR::Thread::Priority::REALTIME);
    }

    for (size_t index = 0; index < CONSUMER_COUNT; ++index)
    {
      consumers[index].Create<ConsumerArg>(
          ConsumerArg{&queue, TOTAL_ITEMS, PRODUCER_COUNT, &produced_done_count,
                      &consumed_done_count, &pop_count, &pop_sum, seen},
          ConsumerTask, "mpmc_cons", 1024, LibXR::Thread::Priority::REALTIME);
    }

    const uint32_t start_ms = LibXR::Thread::GetTime();
    while ((produced_done_count.load(std::memory_order_acquire) != PRODUCER_COUNT ||
            consumed_done_count.load(std::memory_order_acquire) != CONSUMER_COUNT) &&
           (LibXR::Thread::GetTime() - start_ms) < 5000U)
    {
      LibXR::Thread::Sleep(1);
    }

    ASSERT(produced_done_count.load(std::memory_order_acquire) == PRODUCER_COUNT);
    ASSERT(consumed_done_count.load(std::memory_order_acquire) == CONSUMER_COUNT);
    ASSERT(pop_count.load(std::memory_order_acquire) == TOTAL_ITEMS);
    ASSERT(pop_sum.load(std::memory_order_acquire) == EXPECTED_SUM);
    ASSERT(queue.Pop(value) == LibXR::ErrorCode::EMPTY);

    for (size_t index = 0; index < TOTAL_ITEMS; ++index)
    {
      ASSERT(seen[index].load(std::memory_order_relaxed) == 1);
    }
  }
}
