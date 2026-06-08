#include "libxr.hpp"
#include "test.hpp"

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

  ASSERT(pool.Acquire(a) == LibXR::ErrorCode::OK);
  ASSERT(pool.Acquire(b) == LibXR::ErrorCode::OK);
  ASSERT(pool.Acquire(c) == LibXR::ErrorCode::OK);
  ASSERT(pool.EmptySize() == 0);

  b.Reset();
  ASSERT(pool.EmptySize() == 1);
}

template <typename QueueType>
void RunExternalSlotChecks()
{
  Payload slots[3] = {};
  LibXR::BasicObjectPool<Payload, QueueType> pool(3, slots);

  typename LibXR::BasicObjectPool<Payload, QueueType>::Handle handle;
  ASSERT(pool.Acquire(handle) == LibXR::ErrorCode::OK);
  handle->value = 123;
  ASSERT(slots[handle.Index()].value == 123);
}

template <typename QueueType>
void RunExternalQueueAndSlotChecks()
{
  QueueType free_queue(3);
  Payload slots[3] = {};
  LibXR::BasicObjectPool<Payload, QueueType> pool(free_queue, 3, slots);

  typename LibXR::BasicObjectPool<Payload, QueueType>::Handle handle;
  ASSERT(pool.Acquire(handle) == LibXR::ErrorCode::OK);
  handle->value = 456;
  ASSERT(slots[handle.Index()].value == 456);
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

    ASSERT(pool.Acquire(a) == LibXR::ErrorCode::OK);
    ASSERT(pool.Acquire(b) == LibXR::ErrorCode::OK);
    ASSERT(pool.Acquire(c) == LibXR::ErrorCode::OK);
    ASSERT(pool.EmptySize() == 0);
    ASSERT(pool.Acquire(d) == LibXR::ErrorCode::EMPTY);

    a->value = 11;
    b->value = 22;
    c->value = 33;
    ASSERT((*a).value == 11);
    ASSERT((*b).value == 22);
    ASSERT((*c).value == 33);

    const auto a_index = a.Index();
    a.Reset();
    ASSERT(pool.EmptySize() == 1);
    ASSERT(pool.Acquire(d) == LibXR::ErrorCode::OK);
    ASSERT(d.Index() == a_index);
    d->value = 44;
    ASSERT(pool[d.Index()].value == 44);
  }

  // The same pool contract should work with the typed SPSC free-index queue.
  {
    LibXR::SPSCObjectPool<Payload> pool(3);

    LibXR::SPSCObjectPool<Payload>::Handle a;
    LibXR::SPSCObjectPool<Payload>::Handle b;
    LibXR::SPSCObjectPool<Payload>::Handle c;
    LibXR::SPSCObjectPool<Payload>::Handle d;

    ASSERT(pool.Acquire(a) == LibXR::ErrorCode::OK);
    ASSERT(pool.Acquire(b) == LibXR::ErrorCode::OK);
    ASSERT(pool.Acquire(c) == LibXR::ErrorCode::OK);
    ASSERT(pool.EmptySize() == 0);
    ASSERT(pool.Acquire(d) == LibXR::ErrorCode::EMPTY);

    a->value = 11;
    b->value = 22;
    c->value = 33;
    ASSERT((*a).value == 11);
    ASSERT((*b).value == 22);
    ASSERT((*c).value == 33);

    const auto a_index = a.Index();
    a.Reset();
    ASSERT(pool.EmptySize() == 1);
    ASSERT(pool.Acquire(d) == LibXR::ErrorCode::OK);
    ASSERT(d.Index() == a_index);
    d->value = 44;
    ASSERT(pool[d.Index()].value == 44);
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
      ASSERT(pool.Acquire(handle) == LibXR::ErrorCode::OK);
      handle->value = 77;
      ASSERT(pool.EmptySize() == 1);
    }

    ASSERT(pool.EmptySize() == 2);
  }

  // Move-only handle ownership should transfer the return responsibility.
  {
    LibXR::ObjectPool<Payload> pool(1);

    LibXR::ObjectPool<Payload>::Handle first;
    ASSERT(pool.Acquire(first) == LibXR::ErrorCode::OK);
    ASSERT(pool.EmptySize() == 0);

    auto second = std::move(first);
    ASSERT(!first.Valid());
    ASSERT(second.Valid());
    second->value = 99;
    ASSERT(pool[second.Index()].value == 99);

    second.Reset();
    ASSERT(pool.EmptySize() == 1);
  }
}
