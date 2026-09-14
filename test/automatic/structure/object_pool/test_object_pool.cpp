/**
 * @file test_object_pool.cpp
 * @brief 检查对象池分配、耗尽、回收和句柄移动。 /
 * Tests object-pool allocation, exhaustion, release and handle moves.
 *
 * 检查三种空闲队列，以及外部队列、外部存储和析构自动回收。
 * Checks three free-slot queues, external storage and automatic handle release.
 */

#include "libxr.hpp"
#include "test.hpp"
#include "test_assert.hpp"

namespace
{
struct Payload
{
  int value = 0;
};

template <typename QueueType>
void RunExternalQueueChecks()
{
  QueueType free_queue(3);
  LibXR::BasicObjectPool<Payload, QueueType> pool(free_queue, 3);

  typename LibXR::BasicObjectPool<Payload, QueueType>::Handle a;
  typename LibXR::BasicObjectPool<Payload, QueueType>::Handle b;
  typename LibXR::BasicObjectPool<Payload, QueueType>::Handle c;

  TEST_ASSERT(pool.Acquire(a) == LibXR::ErrorCode::OK);
  TEST_ASSERT(pool.Acquire(b) == LibXR::ErrorCode::OK);
  TEST_ASSERT(pool.Acquire(c) == LibXR::ErrorCode::OK);
  TEST_ASSERT(pool.EmptySize() == 0);

  b.Reset();
  TEST_ASSERT(pool.EmptySize() == 1);
}

template <typename QueueType>
void RunExternalSlotChecks()
{
  Payload slots[3] = {};
  LibXR::BasicObjectPool<Payload, QueueType> pool(3, slots);

  typename LibXR::BasicObjectPool<Payload, QueueType>::Handle handle;
  TEST_ASSERT(pool.Acquire(handle) == LibXR::ErrorCode::OK);
  handle->value = 123;
  TEST_ASSERT(slots[handle.Index()].value == 123);
}

template <typename QueueType>
void RunExternalQueueAndSlotChecks()
{
  QueueType free_queue(3);
  Payload slots[3] = {};
  LibXR::BasicObjectPool<Payload, QueueType> pool(free_queue, 3, slots);

  typename LibXR::BasicObjectPool<Payload, QueueType>::Handle handle;
  TEST_ASSERT(pool.Acquire(handle) == LibXR::ErrorCode::OK);
  handle->value = 456;
  TEST_ASSERT(slots[handle.Index()].value == 456);
}
}  // namespace

void test_object_pool()
{
  // Basic acquire/release using the ordinary FIFO queue as free-index storage.
  {
    LibXR::ObjectPool<Payload> pool(3);

    LibXR::ObjectPool<Payload>::Handle a;
    LibXR::ObjectPool<Payload>::Handle b;
    LibXR::ObjectPool<Payload>::Handle c;
    LibXR::ObjectPool<Payload>::Handle d;

    TEST_ASSERT(pool.Acquire(a) == LibXR::ErrorCode::OK);
    TEST_ASSERT(pool.Acquire(b) == LibXR::ErrorCode::OK);
    TEST_ASSERT(pool.Acquire(c) == LibXR::ErrorCode::OK);
    TEST_ASSERT(pool.EmptySize() == 0);
    TEST_ASSERT(pool.Acquire(d) == LibXR::ErrorCode::EMPTY);

    a->value = 11;
    b->value = 22;
    c->value = 33;
    TEST_ASSERT((*a).value == 11);
    TEST_ASSERT((*b).value == 22);
    TEST_ASSERT((*c).value == 33);

    const auto a_index = a.Index();
    a.Reset();
    TEST_ASSERT(pool.EmptySize() == 1);
    TEST_ASSERT(pool.Acquire(d) == LibXR::ErrorCode::OK);
    TEST_ASSERT(d.Index() == a_index);
    d->value = 44;
    TEST_ASSERT(pool.UnsafeAt(d.Index()).value == 44);
  }

  // The same pool contract should work with the typed SPSC free-index queue.
  {
    LibXR::SPSCObjectPool<Payload> pool(3);

    LibXR::SPSCObjectPool<Payload>::Handle a;
    LibXR::SPSCObjectPool<Payload>::Handle b;
    LibXR::SPSCObjectPool<Payload>::Handle c;
    LibXR::SPSCObjectPool<Payload>::Handle d;

    TEST_ASSERT(pool.Acquire(a) == LibXR::ErrorCode::OK);
    TEST_ASSERT(pool.Acquire(b) == LibXR::ErrorCode::OK);
    TEST_ASSERT(pool.Acquire(c) == LibXR::ErrorCode::OK);
    TEST_ASSERT(pool.EmptySize() == 0);
    TEST_ASSERT(pool.Acquire(d) == LibXR::ErrorCode::EMPTY);

    a->value = 11;
    b->value = 22;
    c->value = 33;
    TEST_ASSERT((*a).value == 11);
    TEST_ASSERT((*b).value == 22);
    TEST_ASSERT((*c).value == 33);

    const auto a_index = a.Index();
    a.Reset();
    TEST_ASSERT(pool.EmptySize() == 1);
    TEST_ASSERT(pool.Acquire(d) == LibXR::ErrorCode::OK);
    TEST_ASSERT(d.Index() == a_index);
    d->value = 44;
    TEST_ASSERT(pool.UnsafeAt(d.Index()).value == 44);
  }

  // The same pool contract should work with the typed MPMC free-index queue.
  {
    LibXR::MPMCObjectPool<Payload> pool(3);

    LibXR::MPMCObjectPool<Payload>::Handle a;
    LibXR::MPMCObjectPool<Payload>::Handle b;
    LibXR::MPMCObjectPool<Payload>::Handle c;
    LibXR::MPMCObjectPool<Payload>::Handle d;

    TEST_ASSERT(pool.Acquire(a) == LibXR::ErrorCode::OK);
    TEST_ASSERT(pool.Acquire(b) == LibXR::ErrorCode::OK);
    TEST_ASSERT(pool.Acquire(c) == LibXR::ErrorCode::OK);
    TEST_ASSERT(pool.EmptySize() == 0);
    TEST_ASSERT(pool.Acquire(d) == LibXR::ErrorCode::EMPTY);

    const auto a_index = a.Index();
    a.Reset();
    TEST_ASSERT(pool.EmptySize() == 1);
    TEST_ASSERT(pool.Acquire(d) == LibXR::ErrorCode::OK);
    TEST_ASSERT(d.Index() == a_index);
  }

  // External queue should also be supported.
  {
    using FreeQueue = LibXR::Queue<uint32_t>;
    RunExternalQueueChecks<FreeQueue>();
  }

  // External slot storage should also be supported.
  {
    using FreeQueue = LibXR::Queue<uint32_t>;
    RunExternalSlotChecks<FreeQueue>();
  }

  // External queue + external slot storage should both work together.
  {
    using FreeQueue = LibXR::Queue<uint32_t>;
    RunExternalQueueAndSlotChecks<FreeQueue>();
  }

  // RAII release through destructor should return the slot automatically.
  {
    LibXR::ObjectPool<Payload> pool(2);

    {
      LibXR::ObjectPool<Payload>::Handle handle;
      TEST_ASSERT(pool.Acquire(handle) == LibXR::ErrorCode::OK);
      handle->value = 77;
      TEST_ASSERT(pool.EmptySize() == 1);
    }

    TEST_ASSERT(pool.EmptySize() == 2);
  }

  // Move-only handle ownership should transfer the return responsibility.
  {
    LibXR::ObjectPool<Payload> pool(1);

    LibXR::ObjectPool<Payload>::Handle first;
    TEST_ASSERT(pool.Acquire(first) == LibXR::ErrorCode::OK);
    TEST_ASSERT(pool.EmptySize() == 0);

    auto second = std::move(first);
    TEST_ASSERT(!first.Valid());
    TEST_ASSERT(second.Valid());
    second->value = 99;
    TEST_ASSERT(pool.UnsafeAt(second.Index()).value == 99);

    second.Reset();
    TEST_ASSERT(pool.EmptySize() == 1);
  }
}
