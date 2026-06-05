#pragma once

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstddef>

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
  /**
   * @struct Block
   * @brief topic 运行时状态块 / Runtime state block of one topic
   *
   * @note 当前 topic 自身不缓存最近一次消息值；这里只保存类型契约、名称键值、发布串行化
   *       状态以及订阅链表。
   *       The topic no longer caches the latest payload value itself; this block
   *       keeps only the type contract, topic key, publish-serialization state,
   *       and subscriber list.
   */
  struct Block
  {
    std::atomic<LockState> busy;  ///< 发布路径串行化状态。Publish-path serialization state.
    LockFreeList subers;          ///< 已挂接订阅者链表。List of attached subscribers.
    TypeID::ID payload_type_id;   ///< 精确 payload 类型标识。Exact payload type identifier.
    uint32_t payload_size;        ///< 该 topic 固定 payload 字节数。Fixed payload size in bytes of this topic.
    uint32_t payload_alignment;   ///< 该 topic payload 所需对齐。Required payload alignment of this topic.
    uint32_t crc32;               ///< 主题名 CRC32 键。CRC32 key of the topic name.
    Mutex* mutex;                 ///< 多发布者主题使用的互斥量。Mutex used by multi-publisher topics.
  };

#ifndef __DOXYGEN__
  /**
   * @struct PackedDataHeader
   * @brief 打包消息固定头 / Fixed header of one packed message
   */
  struct PackedDataHeader;

  /**
   * @class PackedData
   * @brief 带固定头和尾 CRC 的打包消息对象 / Packed message object with fixed header
   *        and trailing CRC
   * @tparam Data 负载类型 / Payload type
   */
  template <typename Data>
  class PackedData;
  static constexpr uint8_t PACKET_PREFIX = 0x5A;  ///< 打包消息前缀字节。Packed-message prefix byte.
  static constexpr size_t PACK_BASE_SIZE = 18;  ///< 固定非 payload 开销：17 字节头 + 1 字节尾 CRC8。Fixed non-payload overhead: 17-byte header plus 1-byte trailing CRC8.
#endif

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
  class Server;

  void RegisterCallback(Callback& cb);

  static MicrosecondTimestamp NowTimestamp();

  Topic();
  /**
   * @brief 用显式类型契约构造或查找一个 topic / Construct or look up one topic with
   *        an explicit runtime type contract
   * @param name topic 名称 / Topic name
   * @param payload_type_id payload 类型标识 / Payload type identifier
   * @param payload_size 该 topic 固定 payload 字节数 / Fixed payload size of this topic
   * @param payload_alignment 该 topic payload 所需对齐 / Required payload alignment of this topic
   * @param domain 可选主题域 / Optional topic domain
   * @param multi_publisher 是否允许多发布者串行化 / Whether to allow serialized multi-publisher use
   */
  Topic(const char* name, TypeID::ID payload_type_id, size_t payload_size,
        size_t payload_alignment, Domain* domain = nullptr,
        bool multi_publisher = false);

  template <typename Data>
  static Topic CreateTopic(const char* name, Domain* domain = nullptr,
                           bool multi_publisher = false)
  {
    CheckTopicPayload<Data>();
    return Topic(name, TypeID::GetID<Data>(), sizeof(Data), alignof(Data), domain,
                 multi_publisher);
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

  /**
   * @brief 获取该 topic 固定 payload 字节数 / Get the fixed payload size of this topic
   * @return payload 字节数 / Payload size in bytes
   */
  [[nodiscard]] size_t PayloadSize() const
  {
    ASSERT(block_ != nullptr);
    return block_->data_.payload_size;
  }

  /**
   * @brief 获取该 topic payload 的对齐要求 / Get the payload alignment requirement of
   *        this topic
   * @return payload 对齐要求 / Payload alignment requirement
   */
  [[nodiscard]] size_t PayloadAlignment() const
  {
    ASSERT(block_ != nullptr);
    return block_->data_.payload_alignment;
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

  /**
   * @brief 供 packet/server 路径按字节发布一条消息 / Publish one message from the
   *        packet/server path using bytes already arranged as the exact payload object
   * @param payload_addr 已满足对齐的 payload 起始地址 / Start address of the already-aligned payload object
   * @param payload_size payload 字节数，必须等于 topic 固定大小 / Payload size, must equal the fixed topic size
   * @param timestamp 这条消息的时间戳 / Timestamp of this message
   */
  void PublishBytesFromServer(void* payload_addr, size_t payload_size,
                              MicrosecondTimestamp timestamp)
  {
    PublishServerBytes(payload_addr, payload_size, timestamp, false, false);
  }

  /**
   * @brief 供回调/ISR 上下文里的 packet/server 路径按字节发布一条消息 / Publish one
   *        packet/server message from callback/ISR context using bytes already arranged as
   *        the exact payload object
   * @param payload_addr 已满足对齐的 payload 起始地址 / Start address of the already-aligned payload object
   * @param payload_size payload 字节数，必须等于 topic 固定大小 / Payload size, must equal the fixed topic size
   * @param timestamp 这条消息的时间戳 / Timestamp of this message
   * @param in_isr 当前是否位于 ISR / Whether the current path is in ISR context
   */
  void PublishBytesFromServerCallback(void* payload_addr, size_t payload_size,
                                      MicrosecondTimestamp timestamp, bool in_isr)
  {
    PublishServerBytes(payload_addr, payload_size, timestamp, true, in_isr);
  }

  template <typename Data>
  /**
   * @brief 将一个精确类型消息打包成 packet / Pack one exact-typed message into one
   *        packet using the topic's runtime contract and current timestamp
   * @tparam Data 负载类型 / Payload type
   * @param data 待打包 payload / Payload to pack
   * @param packet 输出 packet 对象 / Output packed message object
   * @return 操作结果错误码 / Error code
   */
  ErrorCode PackData(const Data& data, PackedData<Data>& packet)
  {
    return PackData(data, packet, NowTimestamp());
  }

  template <typename Data>
  /**
   * @brief 将一个精确类型消息按指定时间戳打包成 packet / Pack one exact-typed message
   *        into one packet with the given timestamp
   * @tparam Data 负载类型 / Payload type
   * @param data 待打包 payload / Payload to pack
   * @param packet 输出 packet 对象 / Output packed message object
   * @param timestamp 指定时间戳 / Explicit timestamp
   * @return 操作结果错误码 / Error code
   */
  ErrorCode PackData(const Data& data, PackedData<Data>& packet,
                     MicrosecondTimestamp timestamp);

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

  /**
   * @brief 校验 server 侧字节发布前提 / Check the preconditions of one server-side byte
   *        publish
   * @param topic 目标 topic / Target topic
   * @param payload_addr 已满足对齐的 payload 地址 / Already-aligned payload address
   * @param payload_size payload 字节数 / Payload size in bytes
   */
  static void CheckServerPublishContract(TopicHandle topic, void* payload_addr,
                                         size_t payload_size)
  {
    ASSERT(topic != nullptr);
    ASSERT(payload_addr != nullptr);
    ASSERT(payload_size == topic->data_.payload_size);
    ASSERT(topic->data_.payload_alignment != 0);
    ASSERT(reinterpret_cast<uintptr_t>(payload_addr) % topic->data_.payload_alignment ==
           0);
  }

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

  /**
   * @brief 将一段 payload 字节和 topic 元数据拼成 packet / Pack one payload byte range
   *        together with topic metadata into one packet
   * @param topic_name_crc32 目标 topic 的 CRC32 键 / CRC32 key of the target topic
   * @param buffer 输出原始缓冲区 / Output raw buffer
   * @param timestamp 消息时间戳 / Message timestamp
   * @param data 待打包的 payload 字节 / Payload bytes to pack
   */
  static void PackBytes(uint32_t topic_name_crc32, RawData buffer,
                        MicrosecondTimestamp timestamp, ConstRawData data);
  static void DispatchSubscriber(SuberBlock& block, MicrosecondTimestamp timestamp,
                                 void* payload_addr, bool from_callback, bool in_isr);
  static void DispatchSubscribers(TopicHandle topic, MicrosecondTimestamp timestamp,
                                  void* payload_addr, bool from_callback, bool in_isr);

  /**
   * @brief `PublishBytesFromServer*()` 的共享实现 / Shared implementation behind
   *        `PublishBytesFromServer*()`
   * @param payload_addr 已满足对齐的 payload 地址 / Already-aligned payload address
   * @param payload_size payload 字节数 / Payload size in bytes
   * @param timestamp 消息时间戳 / Message timestamp
   * @param from_callback 是否来自回调发布路径 / Whether this publish comes from callback path
   * @param in_isr 当前是否位于 ISR / Whether the current path is in ISR context
   */
  void PublishServerBytes(void* payload_addr, size_t payload_size,
                          MicrosecondTimestamp timestamp, bool from_callback,
                          bool in_isr)
  {
    CheckServerPublishContract(block_, payload_addr, payload_size);

    if (from_callback)
    {
      LockFromCallback(block_);
    }
    else
    {
      Lock(block_);
    }

    DispatchSubscribers(block_, timestamp, payload_addr, from_callback, in_isr);

    if (from_callback)
    {
      UnlockFromCallback(block_);
    }
    else
    {
      Unlock(block_);
    }
  }
};
}  // namespace LibXR

#include "packet/packet.hpp"
#include "server/server.hpp"
#include "subscriber/async.hpp"
#include "subscriber/callback.hpp"
#include "subscriber/queue.hpp"
#include "subscriber/sync.hpp"
