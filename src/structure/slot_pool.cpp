#include "slot_pool.hpp"

#include <algorithm>
#include <limits>

namespace LibXR
{
SlotPoolBase::SlotPoolBase(size_t slot_size, size_t slot_align, size_t slot_count)
    : slot_size_(slot_size),
      slot_count_(slot_count),
      slot_stride_(AlignUp(slot_size_, slot_align)),
      alloc_align_(std::max(alignof(std::max_align_t), slot_align)),
      states_(nullptr),
      seqs_(nullptr),
      slots_(nullptr),
      publish_seq_(0),
      release_count_(0)
{
  ASSERT(slot_size_ > 0);
  ASSERT(slot_count_ > 0);
  ASSERT(slot_align > 0);
  ASSERT(slot_count_ <= std::numeric_limits<size_t>::max() / slot_stride_);
  ASSERT(slot_stride_ <= std::numeric_limits<size_t>::max() / slot_align);

  states_ = new (std::align_val_t(LibXR::CONCURRENCY_ALIGNMENT))
      std::atomic<uint32_t>[slot_count_];
  seqs_ = new (std::align_val_t(LibXR::CONCURRENCY_ALIGNMENT))
      std::atomic<uint32_t>[slot_count_];

  slots_ = static_cast<std::byte*>(
      ::operator new[](slot_stride_ * slot_count_, std::align_val_t(alloc_align_)));

  for (size_t index = 0; index < slot_count_; ++index)
  {
    states_[index].store(SLOT_FREE, std::memory_order_relaxed);
    seqs_[index].store(0, std::memory_order_relaxed);
  }
}

SlotPoolBase::~SlotPoolBase()
{
  ::operator delete[](slots_, std::align_val_t(alloc_align_));
  delete[] states_;
  delete[] seqs_;
}

ErrorCode SlotPoolBase::Acquire(size_t& index)
{
  // 释放计数作为重扫的变化检测：扫描期间有槽被释放时，FULL 才允许重扫一次。
  // The release counter detects changes across the scan: a FULL result may only
  // trigger one rescan when some slot was released meanwhile.
  uint32_t release_begin = release_count_.load(std::memory_order_acquire);
  bool rescanned = false;

  for (;;)
  {
    for (size_t i = 0; i < slot_count_; ++i)
    {
      if (states_[i].load(std::memory_order_acquire) != SLOT_FREE)
      {
        continue;
      }

      uint32_t expected = SLOT_FREE;
      if (states_[i].compare_exchange_strong(expected, SLOT_WRITING,
                                             std::memory_order_acq_rel,
                                             std::memory_order_acquire))
      {
        index = i;
        return ErrorCode::OK;
      }
      // 该槽刚被其他上下文认领：继续扫剩余槽位。
      // The slot was just claimed by another context: keep scanning the rest.
    }

    const uint32_t release_end = release_count_.load(std::memory_order_acquire);
    if (release_end == release_begin || rescanned)
    {
      return ErrorCode::FULL;
    }
    release_begin = release_end;
    rescanned = true;
  }
}

void SlotPoolBase::Publish(size_t index)
{
  ASSERT(index < slot_count_);

  // 先取序号再置就绪：重扫的发布计数变化先于就绪状态可见。
  // Take the sequence first, then publish: the counter change that drives the
  // rescan becomes visible before the ready state itself.
  const uint32_t seq = publish_seq_.fetch_add(1, std::memory_order_release);
  seqs_[index].store(seq, std::memory_order_relaxed);

  uint32_t expected = SLOT_WRITING;
  const bool ok = states_[index].compare_exchange_strong(expected, SLOT_READY,
                                                         std::memory_order_release,
                                                         std::memory_order_acquire);
  ASSERT(ok);
}

ErrorCode SlotPoolBase::Take(size_t& index)
{
  uint32_t publish_begin = publish_seq_.load(std::memory_order_acquire);
  bool rescanned = false;

  for (;;)
  {
    size_t best = INVALID_INDEX;
    uint32_t best_seq = 0;

    for (size_t i = 0; i < slot_count_; ++i)
    {
      if (states_[i].load(std::memory_order_acquire) != SLOT_READY)
      {
        continue;
      }

      const uint32_t seq = seqs_[i].load(std::memory_order_relaxed);
      if (best == INVALID_INDEX || SeqBefore(seq, best_seq))
      {
        best = i;
        best_seq = seq;
      }
    }

    if (best != INVALID_INDEX)
    {
      uint32_t expected = SLOT_READY;
      if (states_[best].compare_exchange_strong(expected, SLOT_READING,
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire))
      {
        // CAS 成功后复读序号：扫描到 CAS 之间该槽若被取走、释放并重新发布，
        // CAS 赢到的是新条目。撤销本次认领（读取中 → 就绪），把新条目留在
        // 池里，重新扫描，保证选中的仍是最早发布项。
        // Re-read the sequence after a winning CAS: if the slot was taken,
        // released and re-published between the scan and the CAS, the CAS won
        // the NEW item. Undo this claim (reading -> ready) to leave the new
        // item in the pool, then rescan so the oldest item still wins.
        if (seqs_[best].load(std::memory_order_relaxed) == best_seq)
        {
          index = best;
          return ErrorCode::OK;
        }
        uint32_t reading = SLOT_READING;
        const bool undone =
            states_[best].compare_exchange_strong(reading, SLOT_READY,
                                                  std::memory_order_release,
                                                  std::memory_order_acquire);
        ASSERT(undone);
        continue;
      }
      // 该槽刚被其他消费者取走：重新扫描。
      // Another consumer just claimed the slot: rescan.
      continue;
    }

    const uint32_t publish_end = publish_seq_.load(std::memory_order_acquire);
    if (publish_end == publish_begin || rescanned)
    {
      return ErrorCode::EMPTY;
    }
    publish_begin = publish_end;
    rescanned = true;
  }
}

void SlotPoolBase::Release(size_t index)
{
  ASSERT(index < slot_count_);

  // 释放计数先于状态置回空闲：已经扫过该槽的上下文能通过计数变化发现它。
  // Bump the release counter before the state returns to free: a context that
  // already scanned past this slot can detect it through the counter change.
  release_count_.fetch_add(1, std::memory_order_release);

  uint32_t expected = states_[index].load(std::memory_order_relaxed);
  // 只有持有者（写入中或读取中）才能释放 / Only the holder (writing or reading)
  // may release.
  ASSERT(expected == SLOT_WRITING || expected == SLOT_READING);
  const bool ok = states_[index].compare_exchange_strong(expected, SLOT_FREE,
                                                         std::memory_order_release,
                                                         std::memory_order_acquire);
  ASSERT(ok);
}

void* SlotPoolBase::SlotPtr(size_t index)
{
  ASSERT(index < slot_count_);
  return slots_ + index * slot_stride_;
}

const void* SlotPoolBase::SlotPtr(size_t index) const
{
  ASSERT(index < slot_count_);
  return slots_ + index * slot_stride_;
}

size_t SlotPoolBase::AlignUp(size_t value, size_t align)
{
  ASSERT(value <= std::numeric_limits<size_t>::max() - (align - 1));
  return ((value + align - 1) / align) * align;
}
}  // namespace LibXR
