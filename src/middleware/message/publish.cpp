#include <atomic>

#include "libxr_mem.hpp"
#include "message.hpp"
#include "timebase.hpp"

using namespace LibXR;

MicrosecondTimestamp Topic::NowTimestamp()
{
  ASSERT(Timebase::timebase != nullptr);
  return Timebase::GetMicroseconds();
}

void Topic::CheckPublishSize(TopicHandle topic, uint32_t size)
{
  if (topic->data_.check_length)
  {
    ASSERT(size == topic->data_.max_length);
  }
  else
  {
    ASSERT(size <= topic->data_.max_length);
  }
}

RawData Topic::StorePublishedData(TopicHandle topic, void* addr, uint32_t size,
                                  MicrosecondTimestamp timestamp)
{
  if (topic->data_.cache)
  {
    LibXR::Memory::FastCopy(topic->data_.data.addr_, addr, size);
    topic->data_.data.size_ = size;
  }
  else
  {
    topic->data_.data.addr_ = addr;
    topic->data_.data.size_ = size;
  }

  topic->data_.timestamp = timestamp;
  return topic->data_.data;
}

void Topic::DispatchSubscriber(SuberBlock& block, MicrosecondTimestamp timestamp,
                               RawData data, bool from_callback, bool in_isr)
{
  switch (block.type)
  {
    case SuberType::SYNC:
    {
      auto sync = static_cast<SyncBlock*>(&block);
      uint32_t expected = SyncBlock::WAITING;
      auto wake_waiter = sync->wait_state.compare_exchange_strong(
          expected, SyncBlock::WAIT_CLAIMED, std::memory_order_acq_rel,
          std::memory_order_acquire);

      if (!wake_waiter)
      {
        break;
      }

      LibXR::Memory::FastCopy(sync->buff.addr_, data.addr_, data.size_);
      sync->timestamp = timestamp;

      if (from_callback)
      {
        sync->sem.PostFromCallback(in_isr);
      }
      else
      {
        sync->sem.Post();
      }
      break;
    }
    case SuberType::ASYNC:
    {
      auto async = static_cast<ASyncBlock*>(&block);
      if (async->state.load(std::memory_order_acquire) == ASyncSubscriberState::WAITING)
      {
        LibXR::Memory::FastCopy(async->buff.addr_, data.addr_, data.size_);
        async->timestamp = timestamp;
        async->state.store(ASyncSubscriberState::DATA_READY, std::memory_order_release);
      }
      break;
    }
    case SuberType::QUEUE:
    {
      auto queue_block = static_cast<QueueBlock*>(&block);
      queue_block->fun(timestamp, data, *queue_block);
      break;
    }
    case SuberType::CALLBACK:
    {
      auto cb_block = static_cast<CallbackBlock*>(&block);
      cb_block->cb.Run(from_callback && in_isr, timestamp, data);
      break;
    }
  }
}

void Topic::DispatchSubscribers(TopicHandle topic, MicrosecondTimestamp timestamp,
                                RawData data, bool from_callback, bool in_isr)
{
  topic->data_.subers.Foreach<SuberBlock>(
      [=](SuberBlock& block)
      {
        DispatchSubscriber(block, timestamp, data, from_callback, in_isr);
        return ErrorCode::OK;
      });
}

void Topic::PublishRaw(void* addr, uint32_t size, MicrosecondTimestamp timestamp,
                       bool from_callback, bool in_isr)
{
  if (from_callback)
  {
    LockFromCallback(block_);
  }
  else
  {
    Lock(block_);
  }

  CheckPublishSize(block_, size);
  RawData data = StorePublishedData(block_, addr, size, timestamp);
  DispatchSubscribers(block_, timestamp, data, from_callback, in_isr);

  if (from_callback)
  {
    UnlockFromCallback(block_);
  }
  else
  {
    Unlock(block_);
  }
}

void Topic::Publish(void* addr, uint32_t size) { Publish(addr, size, NowTimestamp()); }

void Topic::Publish(void* addr, uint32_t size, MicrosecondTimestamp timestamp)
{
  PublishRaw(addr, size, timestamp, false, false);
}

void Topic::PublishFromCallback(void* addr, uint32_t size, bool in_isr)
{
  PublishFromCallback(addr, size, NowTimestamp(), in_isr);
}

void Topic::PublishFromCallback(void* addr, uint32_t size, MicrosecondTimestamp timestamp,
                                bool in_isr)
{
  PublishRaw(addr, size, timestamp, true, in_isr);
}
