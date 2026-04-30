#include <atomic>

#include "libxr_mem.hpp"
#include "message/message.hpp"

using namespace LibXR;

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

RawData Topic::StorePublishedData(TopicHandle topic, void* addr, uint32_t size)
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

  return topic->data_.data;
}

void Topic::DispatchSubscriber(SuberBlock& block, RawData data, bool from_callback,
                               bool in_isr)
{
  switch (block.type)
  {
    case SuberType::SYNC:
    {
      auto sync = reinterpret_cast<SyncBlock*>(&block);
      LibXR::Memory::FastCopy(sync->buff.addr_, data.addr_, data.size_);
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
      auto async = reinterpret_cast<ASyncBlock*>(&block);
      if (async->state.load(std::memory_order_acquire) == ASyncSubscriberState::WAITING)
      {
        LibXR::Memory::FastCopy(async->buff.addr_, data.addr_, data.size_);
        async->state.store(ASyncSubscriberState::DATA_READY, std::memory_order_release);
      }
      break;
    }
    case SuberType::QUEUE:
    {
      auto queue_block = reinterpret_cast<QueueBlock*>(&block);
      queue_block->fun(data, queue_block->queue);
      break;
    }
    case SuberType::CALLBACK:
    {
      auto cb_block = reinterpret_cast<CallbackBlock*>(&block);
      cb_block->cb.Run(from_callback && in_isr, data);
      break;
    }
  }
}

void Topic::DispatchSubscribers(TopicHandle topic, RawData data, bool from_callback,
                                bool in_isr)
{
  topic->data_.subers.Foreach<SuberBlock>(
      [=](SuberBlock& block)
      {
        DispatchSubscriber(block, data, from_callback, in_isr);
        return ErrorCode::OK;
      });
}

void Topic::PublishRaw(void* addr, uint32_t size, bool from_callback, bool in_isr)
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
  RawData data = StorePublishedData(block_, addr, size);
  DispatchSubscribers(block_, data, from_callback, in_isr);

  if (from_callback)
  {
    UnlockFromCallback(block_);
  }
  else
  {
    Unlock(block_);
  }
}

void Topic::Publish(void* addr, uint32_t size) { PublishRaw(addr, size, false, false); }

void Topic::PublishFromCallback(void* addr, uint32_t size, bool in_isr)
{
  PublishRaw(addr, size, true, in_isr);
}
