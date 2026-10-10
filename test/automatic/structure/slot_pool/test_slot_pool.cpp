/**
 * @file test_slot_pool.cpp
 * @brief 检查槽池的状态转换、发布序、边界条件与多线程传输。 /
 * Tests slot-pool state transitions, publish order, boundaries and multithreaded
 * transfers.
 *
 * 顺序用例检查状态转换与"发布完成先后"的出队顺序；并发用例逐项检查数据
 * 没有丢失、重复；同一生产者内部保序由多生产者、单消费者用例在线断言。
 * Sequential cases check state transitions and publish-completion dequeue order;
 * concurrent cases check loss and duplication item by item; per-producer ordering
 * is asserted online by the multi-producer, single-consumer case.
 */

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"
#include "test_assert.hpp"

namespace
{
using Pool = LibXR::SlotPool<uint32_t>;  ///< 默认压测载荷 / Default stress payload.

/// 允许预置发布序号的测试子类，用于验证序号回绕。
/// Test subclass that can preset the publish sequence to exercise wraparound.
class WrapPool : public LibXR::SlotPool<uint32_t>
{
 public:
  using LibXR::SlotPool<uint32_t>::SlotPool;

  /// 预置发布序号计数器 / Preset the publish sequence counter.
  void PrimePublishSeq(uint32_t value)
  {
    publish_seq_.store(value, std::memory_order_relaxed);
  }
};

struct NoDefaultPayload
{
  explicit NoDefaultPayload(uint32_t value_in) : value(value_in) {}

  uint32_t value;  ///< 有效载荷值 / Payload value.
};

struct ProducerArg
{
  size_t begin;  ///< 本生产者负责的起始值 / First value owned by this producer.
  size_t count;  ///< 本生产者负责推送的元素数 / Number of items pushed by this producer.
  Pool* pool;    ///< 共享槽池 / Shared pool instance.
  std::atomic<size_t>* done_count;  ///< 完成计数器 / Producer completion counter.
};

struct ConsumerArg
{
  Pool* pool;             ///< 共享槽池 / Shared pool instance.
  size_t total_items;     ///< 目标总元素数 / Expected total item count.
  size_t producer_count;  ///< 生产者总数 / Total number of producers.
  std::atomic<size_t>*
      produced_done_count;  ///< 生产者完成计数 / Producer completion counter.
  std::atomic<size_t>*
      consumed_done_count;         ///< 消费者完成计数 / Consumer completion counter.
  std::atomic<size_t>* pop_count;  ///< 已消费元素数 / Total consumed item count.
  std::atomic<uint8_t>* seen;      ///< 去重标记表 / Deduplication mark table.
};

void ProducerTask(ProducerArg arg)
{
  for (size_t offset = 0; offset < arg.count; ++offset)
  {
    const size_t value = arg.begin + offset;
    TEST_ASSERT(value <= UINT32_MAX);
    while (arg.pool->Put(static_cast<uint32_t>(value)) != LibXR::ErrorCode::OK)
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
    uint32_t value = 0;
    const auto ec = arg.pool->Get(value);
    if (ec == LibXR::ErrorCode::OK)
    {
      TEST_ASSERT(value < arg.total_items);
      const auto previous = arg.seen[value].exchange(1, std::memory_order_relaxed);
      TEST_ASSERT(previous == 0);

      const size_t pop_index = arg.pop_count->fetch_add(1, std::memory_order_relaxed) + 1;
      TEST_ASSERT(pop_index <= arg.total_items);
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

/// 多生产者多消费者压测：校验不丢、不重。
/// Multi-producer/multi-consumer stress: no loss and no duplication.
template <size_t PRODUCER_COUNT, size_t CONSUMER_COUNT, size_t ITEMS_PER_PRODUCER>
void RunStress(size_t pool_capacity)
{
  constexpr size_t TOTAL_ITEMS = PRODUCER_COUNT * ITEMS_PER_PRODUCER;

  Pool pool(pool_capacity);
  std::atomic<uint8_t> seen[TOTAL_ITEMS] = {};
  std::atomic<size_t> produced_done_count = 0;
  std::atomic<size_t> consumed_done_count = 0;
  std::atomic<size_t> pop_count = 0;

  // 线程数组长度依赖模板参数，用 std::array 让元素访问成为依赖类型。
  // The thread arrays have a template-parameter-dependent length; std::array
  // makes element access a dependent type.
  std::array<LibXR::Thread, PRODUCER_COUNT> producers;
  std::array<LibXR::Thread, CONSUMER_COUNT> consumers;

  for (size_t index = 0; index < PRODUCER_COUNT; ++index)
  {
    producers[index].template Create<ProducerArg>(
        ProducerArg{index * ITEMS_PER_PRODUCER, ITEMS_PER_PRODUCER, &pool,
                    &produced_done_count},
        ProducerTask, "slot_pool_prod", 1024, LibXR::Thread::Priority::REALTIME);
  }

  for (size_t index = 0; index < CONSUMER_COUNT; ++index)
  {
    consumers[index].template Create<ConsumerArg>(
        ConsumerArg{&pool, TOTAL_ITEMS, PRODUCER_COUNT, &produced_done_count,
                    &consumed_done_count, &pop_count, seen},
        ConsumerTask, "slot_pool_cons", 1024, LibXR::Thread::Priority::REALTIME);
  }

  const uint32_t start_ms = LibXR::Thread::GetTime();
  while ((produced_done_count.load(std::memory_order_acquire) != PRODUCER_COUNT ||
          consumed_done_count.load(std::memory_order_acquire) != CONSUMER_COUNT) &&
         (LibXR::Thread::GetTime() - start_ms) < 5000U)
  {
    LibXR::Thread::Sleep(1);
  }

  TEST_ASSERT(produced_done_count.load(std::memory_order_acquire) == PRODUCER_COUNT);
  TEST_ASSERT(consumed_done_count.load(std::memory_order_acquire) == CONSUMER_COUNT);
  for (auto& producer : producers)
  {
    TEST_ASSERT(producer.Join() == LibXR::ErrorCode::OK);
  }
  for (auto& consumer : consumers)
  {
    TEST_ASSERT(consumer.Join() == LibXR::ErrorCode::OK);
  }
  TEST_ASSERT(pop_count.load(std::memory_order_acquire) == TOTAL_ITEMS);

  uint32_t value = 0;
  TEST_ASSERT(pool.Get(value) == LibXR::ErrorCode::EMPTY);

  for (size_t index = 0; index < TOTAL_ITEMS; ++index)
  {
    TEST_ASSERT(seen[index].load(std::memory_order_relaxed) == 1);
  }
}

struct OrderConsumerArg
{
  Pool* pool;                     ///< 共享槽池 / Shared pool instance.
  size_t total_items;             ///< 目标总元素数 / Expected total item count.
  size_t items_per_producer;      ///< 每个生产者的元素数 / Items per producer.
  size_t producer_count;          ///< 生产者总数 / Total number of producers.
  std::atomic<size_t>* produced_done_count;  ///< 生产者完成计数
                                             ///< Producer completion counter.
};

/// 单消费者在线断言同一生产者内部保序：每个生产者的序内偏移必须逐个递增。
/// One consumer asserts per-producer order online: in-producer offsets must
/// increase one by one.
void OrderConsumerTask(OrderConsumerArg arg)
{
  // last_offset 初始化为 SIZE_MAX：SIZE_MAX + 1 == 0，第一个偏移必须是 0。
  // last_offset starts at SIZE_MAX: SIZE_MAX + 1 == 0, so the first offset must
  // be zero.
  std::array<size_t, 4> last_offset{};
  last_offset.fill(SIZE_MAX);
  size_t received = 0;

  while (received < arg.total_items)
  {
    uint32_t value = 0;
    if (arg.pool->Get(value) == LibXR::ErrorCode::OK)
    {
      const size_t producer = value / arg.items_per_producer;
      const size_t offset = value % arg.items_per_producer;
      TEST_ASSERT(producer < arg.producer_count);
      TEST_ASSERT(offset == last_offset[producer] + 1);
      last_offset[producer] = offset;
      ++received;
      continue;
    }

    LibXR::Thread::Yield();
  }
}
}  // namespace

static_assert(std::is_trivially_copyable_v<NoDefaultPayload>);

void test_slot_pool()
{
  // Every state transition over the low-level interface: acquire, in-place
  // write, publish, take, in-place read, release; plus the allocator pattern
  // that abandons an acquired slot.
  {
    Pool pool(2);
    TEST_ASSERT(pool.Size() == 2);

    size_t index_a = 0;
    size_t index_b = 0;
    TEST_ASSERT(pool.Acquire(index_a) == LibXR::ErrorCode::OK);
    TEST_ASSERT(pool.Acquire(index_b) == LibXR::ErrorCode::OK);
    TEST_ASSERT(index_a != index_b);

    pool.At(index_a) = 100;
    pool.At(index_b) = 200;
    pool.Publish(index_a);
    pool.Publish(index_b);

    size_t taken = 0;
    TEST_ASSERT(pool.Take(taken) == LibXR::ErrorCode::OK);
    TEST_ASSERT(pool.At(taken) == 100);
    pool.Release(taken);

    TEST_ASSERT(pool.Take(taken) == LibXR::ErrorCode::OK);
    TEST_ASSERT(pool.At(taken) == 200);
    pool.Release(taken);

    TEST_ASSERT(pool.Take(taken) == LibXR::ErrorCode::EMPTY);
  }

  // Dequeue order follows publish-completion order, not slot-index order.
  {
    Pool pool(2);

    size_t first = 0;
    size_t second = 0;
    TEST_ASSERT(pool.Acquire(first) == LibXR::ErrorCode::OK);
    TEST_ASSERT(pool.Acquire(second) == LibXR::ErrorCode::OK);
    pool.At(first) = 1;
    pool.At(second) = 2;
    // Publish the later slot first; it must dequeue first.
    pool.Publish(second);
    pool.Publish(first);

    size_t taken = 0;
    TEST_ASSERT(pool.Take(taken) == LibXR::ErrorCode::OK);
    TEST_ASSERT(pool.At(taken) == 2);
    pool.Release(taken);
    TEST_ASSERT(pool.Take(taken) == LibXR::ErrorCode::OK);
    TEST_ASSERT(pool.At(taken) == 1);
    pool.Release(taken);
    TEST_ASSERT(pool.Take(taken) == LibXR::ErrorCode::EMPTY);
  }

  // FULL/EMPTY boundaries on the convenience interface.
  {
    Pool pool(3);
    TEST_ASSERT(pool.Put(1) == LibXR::ErrorCode::OK);
    TEST_ASSERT(pool.Put(2) == LibXR::ErrorCode::OK);
    TEST_ASSERT(pool.Put(3) == LibXR::ErrorCode::OK);
    TEST_ASSERT(pool.Put(4) == LibXR::ErrorCode::FULL);

    uint32_t value = 0;
    TEST_ASSERT(pool.Get(value) == LibXR::ErrorCode::OK);
    TEST_ASSERT(value == 1);
    TEST_ASSERT(pool.Get(value) == LibXR::ErrorCode::OK);
    TEST_ASSERT(value == 2);
    TEST_ASSERT(pool.Get(value) == LibXR::ErrorCode::OK);
    TEST_ASSERT(value == 3);
    TEST_ASSERT(pool.Get(value) == LibXR::ErrorCode::EMPTY);
  }

  // Releasing an acquired-but-unpublished slot is valid and frees the slot.
  {
    Pool pool(1);
    size_t index = 0;
    TEST_ASSERT(pool.Acquire(index) == LibXR::ErrorCode::OK);
    pool.At(index) = 77;
    pool.Release(index);

    TEST_ASSERT(pool.Put(88) == LibXR::ErrorCode::OK);
    uint32_t value = 0;
    TEST_ASSERT(pool.Get(value) == LibXR::ErrorCode::OK);
    TEST_ASSERT(value == 88);
    TEST_ASSERT(pool.Get(value) == LibXR::ErrorCode::EMPTY);
  }

  // Publish sequence wraparound: preset the counter next to 2^32 and keep the
  // publish-completion order across the wrap point.
  {
    WrapPool pool(4);
    pool.PrimePublishSeq(0xFFFFFFFEU);

    uint32_t value = 0;
    for (uint32_t round = 0; round < 16; ++round)
    {
      TEST_ASSERT(pool.Put(round) == LibXR::ErrorCode::OK);
      TEST_ASSERT(pool.Get(value) == LibXR::ErrorCode::OK);
      TEST_ASSERT(value == round);
    }
    TEST_ASSERT(pool.Get(value) == LibXR::ErrorCode::EMPTY);
  }

  // Payloads without default constructors work on the raw-slot storage.
  {
    LibXR::SlotPool<NoDefaultPayload> pool(2);
    NoDefaultPayload pushed(77);
    NoDefaultPayload popped(0);

    TEST_ASSERT(pool.Put(pushed) == LibXR::ErrorCode::OK);
    TEST_ASSERT(pool.Get(popped) == LibXR::ErrorCode::OK);
    TEST_ASSERT(popped.value == 77);
    TEST_ASSERT(pool.Get(popped) == LibXR::ErrorCode::EMPTY);
  }

  // Per-producer FIFO with three concurrent producers and one consumer. The
  // small pool keeps publications landing in slots the consumer has already
  // scanned, which is where a pass could pick a newer item of one producer.
  {
    constexpr size_t PRODUCER_COUNT = 3;
    constexpr size_t ITEMS_PER_PRODUCER = 50000;
    constexpr size_t TOTAL_ITEMS = PRODUCER_COUNT * ITEMS_PER_PRODUCER;

    Pool pool(4);
    std::atomic<size_t> produced_done_count = 0;
    std::array<LibXR::Thread, PRODUCER_COUNT> producers;

    for (size_t index = 0; index < PRODUCER_COUNT; ++index)
    {
      producers[index].template Create<ProducerArg>(
          ProducerArg{index * ITEMS_PER_PRODUCER, ITEMS_PER_PRODUCER, &pool,
                      &produced_done_count},
          ProducerTask, "slot_pool_ord", 1024, LibXR::Thread::Priority::REALTIME);
    }

    OrderConsumerTask(OrderConsumerArg{&pool, TOTAL_ITEMS, ITEMS_PER_PRODUCER,
                                       PRODUCER_COUNT, &produced_done_count});

    for (auto& producer : producers)
    {
      TEST_ASSERT(producer.Join() == LibXR::ErrorCode::OK);
    }

    uint32_t value = 0;
    TEST_ASSERT(pool.Get(value) == LibXR::ErrorCode::EMPTY);
  }

  // Two producers and two consumers with the smallest legal capacity.
  {
    RunStress<2, 2, 5000>(2);
  }

  // Wider producer/consumer fan-in/fan-out on a moderate pool capacity.
  {
    RunStress<4, 4, 2000>(64);
  }
}
