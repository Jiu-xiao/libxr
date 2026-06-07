#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace
{
using Queue = LibXR::MPMCQueue<uint16_t>;

bool EqualDouble(double a, double b) { return std::abs(a - b) < 1e-9; }

struct ProducerArg
{
  size_t begin;
  size_t count;
  Queue* queue;
  std::atomic<size_t>* done_count;
};

struct ConsumerArg
{
  Queue* queue;
  size_t total_items;
  size_t producer_count;
  std::atomic<size_t>* produced_done_count;
  std::atomic<size_t>* consumed_done_count;
  std::atomic<size_t>* pop_count;
  std::atomic<unsigned long long>* pop_sum;
  std::atomic<uint8_t>* seen;
};

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

void test_mpmc_queue()
{
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
