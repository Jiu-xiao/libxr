#include <atomic>

#include "libxr_mem.hpp"
#include "subscriber/async.hpp"
#include "subscriber/callback.hpp"
#include "subscriber/queue.hpp"
#include "subscriber/sync.hpp"
#include "timebase.hpp"
#include "topic.hpp"

using namespace LibXR;

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

MicrosecondTimestamp Topic::NowTimestamp()
{
  ASSERT(Timebase::timebase != nullptr);
  return Timebase::GetMicroseconds();
}

void Topic::CheckPublishContract(TopicHandle topic, TypeID::ID payload_type_id)
{
  ASSERT(payload_type_id != nullptr);
  ASSERT(topic->data_.payload_type_id == payload_type_id);
}
