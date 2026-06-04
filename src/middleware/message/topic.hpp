#pragma once

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>

#include "libxr_cb.hpp"
#include "libxr_def.hpp"
#include "libxr_time.hpp"
#include "libxr_type.hpp"
#include "lockfree_list.hpp"
#include "lockfree_queue.hpp"
#include "mutex.hpp"
#include "rbt.hpp"
#include "semaphore.hpp"
#include "thread.hpp"

namespace LibXR
{
template <typename Data>
concept TopicPayload =
    !std::is_reference_v<Data> && !std::is_const_v<Data> && !std::is_volatile_v<Data> &&
    std::is_object_v<Data> && std::is_default_constructible_v<Data> &&
    std::is_copy_assignable_v<Data> && std::is_trivially_destructible_v<Data>;

template <typename Data>
constexpr void CheckTopicPayload()
{
  static_assert(TopicPayload<Data>,
                "LibXR::Topic typed payload must be a non-cv/ref object type that is "
                "default-constructible, copy-assignable, and trivially destructible.");
}

/**
 * @class Topic
 * @brief 发布订阅主题 / Publish-subscribe topic
 *
 * 一个 `Topic` 表示一种精确类型的消息通道：它在每次发布时同步通知挂在上面的同步、
 * 异步、队列或回调订阅者，但自身不缓存最近一次消息。
 * One `Topic` represents one exact-typed message channel: it synchronously notifies
 * the attached synchronous, asynchronous, queued, or callback subscribers on each
 * publish, but does not cache the latest message itself.
 */
class Topic
{
  enum class LockState : uint32_t
  {
    UNLOCKED = 0,
    LOCKED = 1,
    USE_MUTEX = UINT32_MAX,
  };

 public:
  struct Block
  {
    std::atomic<LockState> busy;
    LockFreeList subers;
    TypeID::ID payload_type_id;
    uint32_t crc32;
    Mutex* mutex;
  };

  typedef RBTree<uint32_t>::Node<Block>* TopicHandle;

  template <typename Data>
  struct MessageView
  {
    static_assert(TopicPayload<Data>);
    MicrosecondTimestamp timestamp;
    Data* data;
  };

  template <typename Data>
  struct Message
  {
    static_assert(TopicPayload<Data>);
    MicrosecondTimestamp timestamp;
    Data data;
  };

  static void Lock(TopicHandle topic);
  static void Unlock(TopicHandle topic);
  static void LockFromCallback(TopicHandle topic);
  static void UnlockFromCallback(TopicHandle topic);

  class Domain
  {
   public:
    Domain(const char* name);
    RBTree<uint32_t>::Node<RBTree<uint32_t>>* node_;
  };

  enum class SuberType : uint8_t
  {
    SYNC,
    ASYNC,
    QUEUE,
    CALLBACK,
  };

  struct SuberBlock
  {
    SuberType type;
  };
  struct SyncBlock;
  template <typename Data>
  class SyncSubscriber;
  enum class ASyncSubscriberState : uint32_t;
  struct ASyncBlock;
  template <typename Data>
  class ASyncSubscriber;
  struct QueueBlock;
  class QueuedSubscriber;
  class Callback;
  struct CallbackBlock;

  void RegisterCallback(Callback& cb);

  static MicrosecondTimestamp NowTimestamp();

  Topic();
  Topic(const char* name, TypeID::ID payload_type_id, Domain* domain = nullptr,
        bool multi_publisher = false);

  template <typename Data>
  static Topic CreateTopic(const char* name, Domain* domain = nullptr,
                           bool multi_publisher = false)
  {
    CheckTopicPayload<Data>();
    return Topic(name, TypeID::GetID<Data>(), domain, multi_publisher);
  }

  Topic(TopicHandle topic);

  static TopicHandle Find(const char* name, Domain* domain = nullptr);

  template <typename Data>
  static TopicHandle FindOrCreate(const char* name, Domain* domain = nullptr,
                                  bool multi_publisher = false)
  {
    CheckTopicPayload<Data>();
    auto topic = Find(name, domain);
    if (topic == nullptr)
    {
      topic = CreateTopic<Data>(name, domain, multi_publisher).block_;
    }
    return topic;
  }

  template <typename Data>
  void Publish(Data& data)
  {
    PublishTyped(data, NowTimestamp(), false, false);
  }

  template <typename Data>
  void Publish(Data& data, MicrosecondTimestamp timestamp)
  {
    PublishTyped(data, timestamp, false, false);
  }

  template <typename Data>
  void PublishFromCallback(Data& data, bool in_isr)
  {
    PublishTyped(data, NowTimestamp(), true, in_isr);
  }

  template <typename Data>
  void PublishFromCallback(Data& data, MicrosecondTimestamp timestamp, bool in_isr)
  {
    PublishTyped(data, timestamp, true, in_isr);
  }

  static TopicHandle WaitTopic(const char* name, uint32_t timeout = UINT32_MAX,
                               Domain* domain = nullptr);

  operator TopicHandle() { return block_; }
  uint32_t GetKey() const;

 private:
  TopicHandle block_ = nullptr;

  static inline RBTree<uint32_t>* domain_ = nullptr;
  static inline Domain* def_domain_ = nullptr;

  static void EnsureDomainRegistry();
  static Domain* EnsureDefaultDomain();

  template <typename Data>
  static void CheckSubscriberType(Topic topic)
  {
    CheckTopicPayload<Data>();
    ASSERT(topic.block_ != nullptr);
    ASSERT(topic.block_->data_.payload_type_id == TypeID::GetID<Data>());
  }

  template <typename Data>
  static void* AllocateSubscriberBuffer()
  {
    CheckTopicPayload<Data>();
    return new Data;
  }

  template <typename Data>
  static void CopyPayload(void* dst, void* payload_addr)
  {
    CheckTopicPayload<Data>();
    ASSERT(dst != nullptr);
    ASSERT(payload_addr != nullptr);
    *reinterpret_cast<Data*>(dst) = *reinterpret_cast<Data*>(payload_addr);
  }

  template <typename Data>
  void PublishTyped(Data& data, MicrosecondTimestamp timestamp, bool from_callback,
                    bool in_isr)
  {
    CheckTopicPayload<Data>();

    if (from_callback)
    {
      LockFromCallback(block_);
    }
    else
    {
      Lock(block_);
    }

    CheckPublishContract(block_, TypeID::GetID<Data>());
    DispatchSubscribers(block_, timestamp, &data, from_callback, in_isr);

    if (from_callback)
    {
      UnlockFromCallback(block_);
    }
    else
    {
      Unlock(block_);
    }
  }

  static void CheckPublishContract(TopicHandle topic, TypeID::ID payload_type_id);
  static void DispatchSubscriber(SuberBlock& block, MicrosecondTimestamp timestamp,
                                 void* payload_addr, bool from_callback, bool in_isr);
  static void DispatchSubscribers(TopicHandle topic, MicrosecondTimestamp timestamp,
                                  void* payload_addr, bool from_callback, bool in_isr);
};
}  // namespace LibXR

#include "subscriber/async.hpp"
#include "subscriber/callback.hpp"
#include "subscriber/queue.hpp"
#include "subscriber/sync.hpp"
