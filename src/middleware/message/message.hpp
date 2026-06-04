#pragma once

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

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
 * @brief 主题（Topic）管理类 / Topic management class
 *
 * 该类提供了基于发布-订阅模式的主题管理，支持同步、异步、队列和回调订阅者。
 * 一个 Topic 对应一个精确的 payload 类型；Topic 自身不缓存最近一次消息。
 * This class provides topic management based on the publish-subscribe model, supporting
 * synchronous, asynchronous, queue-based, and callback subscribers.
 * One Topic maps to one exact payload type, and the Topic itself does not cache the
 * latest message.
 *
 * @note Topic 和 subscriber 应在初始化阶段创建，并在应用运行期间长期存在。
 *       该消息系统遵循 libxr 的静态生命周期模型：
 *       初始化阶段允许分配，发布和回调热路径不应产生动态分配。
 *       Topics and subscribers are expected to be created during initialization and
 *       kept alive for the application lifetime. The message system follows libxr's
 *       static lifetime model: allocation is allowed during initialization, while
 *       publish and callback hot paths should not allocate dynamically.
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
   * @brief 存储主题运行状态和静态配置。Stores topic runtime state and static
   * configuration.
   *
   * subers 通常只在初始化期追加，发布热路径只遍历；busy/mutex 保护一次发布和订阅者分发。
   * subers is normally appended during initialization and traversed on the publish hot
   * path; busy/mutex protects one publish and subscriber dispatch.
   */
  struct Block
  {
    std::atomic<LockState> busy;  ///< 是否忙碌。Indicates whether it is busy.
    LockFreeList subers;          ///< 订阅者列表。List of subscribers.
    uint32_t payload_size;        ///< 该 topic 的精确 payload 长度。Exact payload size of this topic.
    TypeID::ID payload_type_id;   ///< 该 topic 绑定的 payload 类型标识。Bound payload type identifier of this topic.
    uint32_t crc32;  ///< 主题名称的 CRC32 校验码。CRC32 checksum of the topic name.
    Mutex* mutex;    ///< 线程同步互斥锁。Mutex for thread synchronization.
  };

  /**
   * @typedef TopicHandle
   * @brief 主题句柄，指向存储数据的红黑树节点。Handle pointing to a red-black tree node
   * storing data.
   */
  typedef RBTree<uint32_t>::Node<Block>* TopicHandle;

  /**
   * @struct MessageView
   * @brief 带时间戳的类型化消息视图。Timestamped typed message view.
   */
  template <typename Data>
  struct MessageView
  {
    static_assert(TopicPayload<Data>);

    MicrosecondTimestamp timestamp;  ///< 消息时间戳。Message timestamp.
    Data& data;                      ///< 消息数据引用。Message data reference.
  };

  /**
   * @struct Message
   * @brief 带时间戳的类型化消息。Timestamped typed message.
   */
  template <typename Data>
  struct Message
  {
    static_assert(TopicPayload<Data>);

    MicrosecondTimestamp timestamp;  ///< 消息时间戳。Message timestamp.
    Data data;                       ///< 消息数据。Message payload.
  };

  /**
   * @brief 锁定主题，防止其被多个订阅者同时访问。Lock the topic to prevent it from being
   * accessed by multiple subscribers at the same time.
   *
   * @param topic 要锁定的主题。Topic to be locked.
   */
  static void Lock(TopicHandle topic);

  /**
   * @brief 解锁主题。Unlock the topic.
   *
   * @param topic 要解锁的主题。Topic to be unlocked.
   */
  static void Unlock(TopicHandle topic);

  /**
   * @brief 从回调中锁定主题，防止其被多个订阅者同时访问。Lock the topic from a callback
   * to prevent it from being accessed by multiple subscribers at the same time.
   *
   * @param topic 要锁定的主题。Topic to be locked.
   */
  static void LockFromCallback(TopicHandle topic);

  /**
   * @brief 从回调中解锁主题。Unlock the topic from a callback.
   *
   * @param topic 要解锁的主题。Topic to be unlocked.
   */
  static void UnlockFromCallback(TopicHandle topic);

  /**
   * @class Domain
   * @brief 主题域（Domain）管理器，用于组织多个主题。Domain manager for organizing
   * multiple topics.
   */
  class Domain
  {
   public:
    /**
     * @brief 构造函数，初始化或查找指定名称的主题域。Constructor initializing or looking
     * up a domain by name.
     * @param name 主题域的名称。Name of the domain.
     *
     * @note 包含初始化期动态内存分配，domain 应长期存在。
     *       Contains initialization-time dynamic allocation; domains are expected to be
     *       long-lived.
     */
    Domain(const char* name);

    /**
     * @brief 指向该域的根节点。Pointer to the root node of the domain.
     */
    RBTree<uint32_t>::Node<RBTree<uint32_t>>* node_;
  };

  enum class SuberType : uint8_t;
  struct SuberBlock;
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

  /**
   * @brief  注册回调函数
   *         Registers a callback function
   * @param  cb 需要注册的回调函数 The callback function to register
   *
   * @note 包含初始化期动态内存分配，回调订阅应长期存在。
   *       Contains initialization-time dynamic allocation; callback subscriptions are
   *       expected to be long-lived.
   * @note 回调在发布锁内运行，不应重入发布同一个主题。
   *       The callback runs while the publish lock is held and should not re-enter
   *       publishing on the same topic.
   */
  void RegisterCallback(Callback& cb);

  /**
   * @brief 生成默认发布时刻。Create the default publish timestamp.
   * @return 当前 Timebase 微秒时间戳。Current Timebase microsecond timestamp.
   */
  static MicrosecondTimestamp NowTimestamp();

  /**
   * @brief  默认构造函数，创建一个空的 Topic 实例
   *         Default constructor, creates an empty Topic instance
   */
  Topic();

  /**
   * @brief  构造函数，使用指定名称、payload 契约和域初始化主题
   *         Constructor to initialize a topic with the specified name, payload
   *         contract, and domain
   * @param  name 主题名称 Topic name
   * @param  payload_size 主题 payload 的精确长度 Exact topic payload size
   * @param  payload_type_id 主题 payload 的类型标识 Topic payload type identifier
   * @param  domain 主题所属的域（默认为 nullptr）Domain to which the topic belongs
   * (default: nullptr)
   * @param  multi_publisher 是否允许多个订阅者（默认为 false）Whether to allow multiple
   * subscribers (default: false)
   *
   * @note 包含初始化期动态内存分配，主题应长期存在。
   *       Contains initialization-time dynamic allocation; topics are expected to be
   *       long-lived.
   * @note Topic 只做发布时同步分发，不缓存最近一条消息。多发布者主题使用 mutex，
   *       不支持 PublishFromCallback()。
   *       Topics only dispatch synchronously during publish and do not retain the
   *       latest message. Multi-publisher topics use a mutex and do not support
   *       PublishFromCallback().
   */
  Topic(const char* name, uint32_t payload_size, TypeID::ID payload_type_id,
        Domain* domain = nullptr, bool multi_publisher = false);

  /**
   * @brief  创建一个新的主题
   *         Creates a new topic
   * @tparam Data 主题数据类型 Topic data type
   * @param  name 主题名称 Topic name
   * @param  domain 主题所属的域（默认为 nullptr）Domain to which the topic belongs
   * (default: nullptr)
   * @param  multi_publisher 是否允许多个订阅者（默认为 false）Whether to allow multiple
   * subscribers (default: false)
   * @return 创建的 Topic 实例 The created Topic instance
   *
   * @note 包含初始化期动态内存分配，主题应长期存在。
   *       Contains initialization-time dynamic allocation; topics are expected to be
   *       long-lived.
   */
  template <typename Data>
  static Topic CreateTopic(const char* name, Domain* domain = nullptr,
                           bool multi_publisher = false)
  {
    CheckTopicPayload<Data>();
    return Topic(name, sizeof(Data), TypeID::GetID<Data>(), domain, multi_publisher);
  }

  /**
   * @brief  通过句柄构造主题
   *         Constructs a topic from a topic handle
   * @param  topic 主题句柄 Topic handle
   */
  Topic(TopicHandle topic);

  /**
   * @brief  在指定域中查找主题
   *         Finds a topic in the specified domain
   * @param  name 主题名称 Topic name
   * @param  domain 主题所属的域（默认为 nullptr）Domain to search in (default: nullptr)
   * @return 主题句柄，如果找到则返回对应的句柄，否则返回 nullptr
   *         Topic handle if found, otherwise returns nullptr
   *
   * @note 查询不会创建默认域；默认域尚未初始化时返回 nullptr。
   *       Lookup does not create the default domain; it returns nullptr when the
   *       default domain has not been initialized.
   */
  static TopicHandle Find(const char* name, Domain* domain = nullptr);

  /**
   * @brief  在指定域中查找或创建主题
   *         Finds or creates a topic in the specified domain
   *
   * @tparam Data 数据类型 Data type
   * @param name 话题名称 Topic name
   * @param domain 可选的域指针 Optional domain pointer (default: nullptr)
   * @param multi_publisher 可选的多发布标志位 Optional multi-publisher flag
   * @return TopicHandle 主题句柄 Topic handle
   *
   * @note 包含初始化期动态内存分配，主题应长期存在。
   *       Contains initialization-time dynamic allocation; topics are expected to be
   *       long-lived.
   */
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
   * @brief  发布数据
   *         Publishes data
   * @tparam Data 数据类型 Data type
   * @param  data 需要发布的数据 Data to be published
   */
  template <typename Data>
  void Publish(Data& data)
  {
    CheckTopicPayload<Data>();
    PublishRaw(static_cast<void*>(&data), static_cast<uint32_t>(sizeof(Data)),
               TypeID::GetID<Data>(), NowTimestamp(), false, false);
  }

  template <typename Data>
  void Publish(Data& data, MicrosecondTimestamp timestamp)
  {
    CheckTopicPayload<Data>();
    PublishRaw(static_cast<void*>(&data), static_cast<uint32_t>(sizeof(Data)),
               TypeID::GetID<Data>(), timestamp, false, false);
  }

  /**
   * @brief  在回调函数中发布数据
   *         Publishes data in a callback
   * @tparam Data 数据类型 Data type
   * @param  data 需要发布的数据 Data to be published
   * @param  in_isr 是否在中断中发布数据 Whether to publish data in an interrupt
   */
  template <typename Data>
  void PublishFromCallback(Data& data, bool in_isr)
  {
    CheckTopicPayload<Data>();
    PublishRaw(static_cast<void*>(&data), static_cast<uint32_t>(sizeof(Data)),
               TypeID::GetID<Data>(), NowTimestamp(), true, in_isr);
  }

  template <typename Data>
  void PublishFromCallback(Data& data, MicrosecondTimestamp timestamp, bool in_isr)
  {
    CheckTopicPayload<Data>();
    PublishRaw(static_cast<void*>(&data), static_cast<uint32_t>(sizeof(Data)),
               TypeID::GetID<Data>(), timestamp, true, in_isr);
  }

  /**
   * @brief  等待主题的创建并返回其句柄
   *         Waits for a topic to be created and returns its handle
   * @param  name 主题名称 The name of the topic
   * @param  timeout 等待的超时时间（默认 UINT32_MAX）Timeout duration to wait (default:
   * UINT32_MAX)
   * @param  domain 主题所属的域（默认为 nullptr）The domain in which to search for the
   * topic (default: nullptr)
   * @return TopicHandle 如果找到主题，则返回其句柄，否则返回 nullptr
   *         TopicHandle if the topic is found, otherwise returns nullptr
   */
  static TopicHandle WaitTopic(const char* name, uint32_t timeout = UINT32_MAX,
                               Domain* domain = nullptr);

  /**
   * @brief  将 Topic 转换为 TopicHandle
   *         Converts Topic to TopicHandle
   * @return TopicHandle
   */
  operator TopicHandle() { return block_; }

  /**
   * @brief  获取主题的键值
   *         Gets the key value of the topic
   *
   * @return uint32_t
   */
  uint32_t GetKey() const;

 private:
  /**
   * @brief  主题句柄，指向当前主题的内存块
   *         Topic handle pointing to the memory block of the current topic
   */
  TopicHandle block_ = nullptr;

  /**
   * @brief  主题域的红黑树结构，存储不同的主题
   *         Red-Black Tree structure for storing different topics in the domain
   *
   * @note 初始化期懒创建；调用者应在并发发布前完成 topic/domain 创建。
   *       Lazily created during initialization; callers should finish topic/domain
   *       creation before concurrent publishing starts.
   */
  static inline RBTree<uint32_t>* domain_ = nullptr;

  /**
   * @brief  默认的主题域，所有未指定域的主题都会归入此域
   *         Default domain where all topics without a specified domain are assigned
   */
  static inline Domain* def_domain_ = nullptr;

  static void EnsureDomainRegistry();
  static Domain* EnsureDefaultDomain();

  /**
   * @brief 校验订阅者数据类型和主题类型契约是否一致。
   *        Checks whether the subscriber data type matches the topic type contract.
   */
  template <typename Data>
  static void CheckSubscriberType(Topic topic)
  {
    CheckTopicPayload<Data>();
    ASSERT(topic.block_ != nullptr);
    ASSERT(topic.block_->data_.payload_type_id == TypeID::GetID<Data>());
    ASSERT(topic.block_->data_.payload_size == sizeof(Data));
  }

  /**
   * @brief 为异步订阅者分配长期存在的接收缓冲区。
   *        Allocates a long-lived receive buffer for an async subscriber.
   */
  template <typename Data>
  static RawData NewSubscriberBuffer()
  {
    CheckTopicPayload<Data>();
    auto* data = new Data;
    return RawData(*data);
  }

  void PublishRaw(void* addr, uint32_t size, TypeID::ID payload_type_id,
                  MicrosecondTimestamp timestamp, bool from_callback, bool in_isr);

  static void CheckPublishContract(TopicHandle topic, TypeID::ID payload_type_id,
                                   uint32_t size);
  static void DispatchSubscriber(SuberBlock& block, MicrosecondTimestamp timestamp,
                                 RawData data, bool from_callback, bool in_isr);
  static void DispatchSubscribers(TopicHandle topic, MicrosecondTimestamp timestamp,
                                  RawData data, bool from_callback, bool in_isr);
};
}  // namespace LibXR
#include "message/subscriber.hpp"
