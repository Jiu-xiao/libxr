#pragma once

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

#include "crc.hpp"
#include "libxr_cb.hpp"
#include "libxr_def.hpp"
#include "libxr_mem.hpp"
#include "libxr_time.hpp"
#include "libxr_type.hpp"
#include "lock_queue.hpp"
#include "lockfree_list.hpp"
#include "lockfree_queue.hpp"
#include "mutex.hpp"
#include "queue.hpp"
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
 * 该类提供了基于发布-订阅模式的主题管理，支持同步、异步、队列和回调订阅者，以及数据的缓存和校验机制。
 * This class provides topic management based on the publish-subscribe model, supporting
 * synchronous, asynchronous, queue-based, and callback subscribers, as well as data
 * caching and validation mechanisms.
 *
 * @note Topic、subscriber 和 server 应在初始化阶段创建，并在应用运行期间长期存在。
 *       该消息系统遵循 libxr 的静态生命周期模型：
 *       初始化阶段允许分配，发布、回调和解析热路径不应产生动态分配。
 *       Topics, subscribers, and servers are expected to be created during
 *       initialization and kept alive for the application lifetime. The message system
 *       follows libxr's static lifetime model: allocation is allowed during
 *       initialization, while publish, callback, and parser hot paths should not
 *       allocate dynamically.
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
   * subers 通常只在初始化期追加，发布热路径只遍历；busy/mutex 保护 data 和订阅者分发。
   * subers is normally appended during initialization and traversed on the publish hot
   * path; busy/mutex protects data storage and subscriber dispatch.
   */
  struct Block
  {
    std::atomic<LockState> busy;  ///< 是否忙碌。Indicates whether it is busy.
    LockFreeList subers;          ///< 订阅者列表。List of subscribers.
    uint32_t max_length;          ///< 数据的最大长度。Maximum length of data.
    uint32_t crc32;  ///< 主题名称的 CRC32 校验码。CRC32 checksum of the topic name.
    Mutex* mutex;    ///< 线程同步互斥锁。Mutex for thread synchronization.
    RawData data;    ///< 最近一次发布的数据视图。Latest published data view.
    MicrosecondTimestamp timestamp;  ///< 最近一次消息时间戳。Latest message timestamp.
    bool cache;         ///< 是否启用数据缓存。Indicates whether data caching is enabled.
    bool check_length;  ///< 是否检查数据长度。Indicates whether data length is checked.
  };

#ifndef __DOXYGEN__
#pragma pack(push, 1)
  /**
   * @struct PackedDataHeader
   * @brief 主题数据包头，用于网络传输。Packed data header for network transmission.
   */
  struct PackedDataHeader
  {
    uint8_t prefix;  ///< 数据包前缀（固定为 0x5A）。Packet prefix (fixed at 0x5A).
    uint8_t data_len_raw[3];  ///< 数据长度（最多 16MB）。Data length (up to 16MB).
    uint32_t
        topic_name_crc32;  ///< 主题名称的 CRC32 校验码。CRC32 checksum of the topic name.
    uint8_t timestamp_us_raw[8];  ///< 微秒时间戳。Microsecond timestamp.
    uint8_t pack_header_crc8;     ///< 头部 CRC8 校验码。CRC8 checksum of the header.

    void SetDataLen(uint32_t len);

    uint32_t GetDataLen() const;

    void SetTimestamp(MicrosecondTimestamp timestamp);

    MicrosecondTimestamp GetTimestamp() const;
  };
#pragma pack(pop)

  template <typename Data>
  class PackedData;

  static constexpr uint8_t PACKET_PREFIX = 0x5A;
  static constexpr size_t PACK_BASE_SIZE = sizeof(PackedDataHeader) + sizeof(uint8_t);
  static_assert(sizeof(PackedDataHeader) == 17);
  static_assert(offsetof(PackedDataHeader, prefix) == 0);
  static_assert(offsetof(PackedDataHeader, data_len_raw) == 1);
  static_assert(offsetof(PackedDataHeader, topic_name_crc32) == 4);
  static_assert(offsetof(PackedDataHeader, timestamp_us_raw) == 8);
  static_assert(offsetof(PackedDataHeader, pack_header_crc8) == 16);
#endif

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
   * @brief  构造函数，使用指定名称、最大长度、域及其他选项初始化主题
   *         Constructor to initialize a topic with the specified name, maximum length,
   * domain, and options
   * @param  name 主题名称 Topic name
   * @param  max_length 数据的最大长度 Maximum length of data
   * @param  domain 主题所属的域（默认为 nullptr）Domain to which the topic belongs
   * (default: nullptr)
   * @param  multi_publisher 是否允许多个订阅者（默认为 false）Whether to allow multiple
   * subscribers (default: false)
   * @param  cache 是否启用缓存（默认为 false）Whether to enable caching (default: false)
   * @param  check_length 是否检查数据长度（默认为 false）Whether to check data length
   * (default: false)
   *
   * @note 包含初始化期动态内存分配，主题应长期存在。
   *       Contains initialization-time dynamic allocation; topics are expected to be
   *       long-lived.
   * @note 未启用缓存时，Topic 只保留最近一次发布数据的地址；调用者必须保证该地址在
   *       DumpData() 前仍然有效。多发布者主题使用 mutex，不支持 PublishFromCallback()。
   *       Without cache, Topic keeps only the last published address; the caller must
   *       keep it valid until DumpData(). Multi-publisher topics use a mutex and do not
   *       support PublishFromCallback().
   */
  Topic(const char* name, uint32_t max_length, Domain* domain = nullptr,
        bool multi_publisher = false, bool cache = false, bool check_length = false);

  /**
   * @brief  创建一个新的主题
   *         Creates a new topic
   * @tparam Data 主题数据类型 Topic data type
   * @param  name 主题名称 Topic name
   * @param  domain 主题所属的域（默认为 nullptr）Domain to which the topic belongs
   * (default: nullptr)
   * @param  multi_publisher 是否允许多个订阅者（默认为 false）Whether to allow multiple
   * subscribers (default: false)
   * @param  cache 是否启用缓存（默认为 false）Whether to enable caching (default: false)
   * @param  check_length 是否检查数据长度（默认为 false）Whether to check data length
   * (default: false)
   * @return 创建的 Topic 实例 The created Topic instance
   *
   * @note 包含初始化期动态内存分配，主题应长期存在。
   *       Contains initialization-time dynamic allocation; topics are expected to be
   *       long-lived.
   */
  template <typename Data>
  static Topic CreateTopic(const char* name, Domain* domain = nullptr,
                           bool multi_publisher = false, bool cache = false,
                           bool check_length = false)
  {
    CheckTopicPayload<Data>();
    return Topic(name, sizeof(Data), domain, multi_publisher, cache, check_length);
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
   * @param cache  可选的缓存标志位 Optional cache flag (default: false)
   * @param check_length 可选的数据长度检查标志位 Optional data length check flag
   * (default: false)
   * @return TopicHandle 主题句柄 Topic handle
   *
   * @note 包含初始化期动态内存分配，主题应长期存在。
   *       Contains initialization-time dynamic allocation; topics are expected to be
   *       long-lived.
   */
  template <typename Data>
  static TopicHandle FindOrCreate(const char* name, Domain* domain = nullptr,
                                  bool multi_publisher = false, bool cache = false,
                                  bool check_length = false)
  {
    CheckTopicPayload<Data>();
    auto topic = Find(name, domain);
    if (topic == nullptr)
    {
      topic =
          CreateTopic<Data>(name, domain, multi_publisher, cache, check_length).block_;
    }
    return topic;
  }

  /**
   * @brief  启用主题的缓存功能
   *         Enables caching for the topic
   *
   * @note 包含初始化期动态内存分配，缓存启用后应长期存在。
   *       Contains initialization-time dynamic allocation; the cache is expected to
   *       remain enabled for the topic lifetime.
   */
  void EnableCache();

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
    Publish(static_cast<void*>(&data), static_cast<uint32_t>(sizeof(Data)));
  }

  template <typename Data>
  void Publish(Data& data, MicrosecondTimestamp timestamp)
  {
    CheckTopicPayload<Data>();
    Publish(static_cast<void*>(&data), static_cast<uint32_t>(sizeof(Data)), timestamp);
  }

  /**
   * @brief  以原始地址和大小发布数据
   *         Publishes data using raw address and size
   * @param  addr 数据的地址 Address of the data
   * @param  size 数据大小 Size of the data
   */
  void Publish(void* addr, uint32_t size);

  void Publish(void* addr, uint32_t size, MicrosecondTimestamp timestamp);

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
    PublishFromCallback(static_cast<void*>(&data), static_cast<uint32_t>(sizeof(Data)),
                        in_isr);
  }

  template <typename Data>
  void PublishFromCallback(Data& data, MicrosecondTimestamp timestamp, bool in_isr)
  {
    CheckTopicPayload<Data>();
    PublishFromCallback(static_cast<void*>(&data), static_cast<uint32_t>(sizeof(Data)),
                        timestamp, in_isr);
  }

  /**
   * @brief  在回调函数中以原始地址和大小发布数据
   *         Publishes data using raw address and size in a callback
   * @param  addr 数据的地址 Address of the data
   * @param  size 数据大小 Size of the data
   * @param  in_isr 是否在中断中发布数据 Whether to publish data in an interrupt
   *
   * @note 仅适用于非 mutex 主题。multi_publisher 主题会在回调发布路径触发断言。
   *       Only valid for non-mutex topics. Multi-publisher topics assert on the
   *       callback publish path.
   * @note 回调发布路径不分配内存，但仍会同步分发到该主题的订阅者。
   *       The callback publish path does not allocate, but still dispatches to the
   *       topic subscribers synchronously.
   */
  void PublishFromCallback(void* addr, uint32_t size, bool in_isr);
  void PublishFromCallback(void* addr, uint32_t size, MicrosecondTimestamp timestamp,
                           bool in_isr);

  /**
   * @brief 打包数据
   *
   * @param topic_name_crc32 话题名称的 CRC32 校验码
   * @param buffer 等待写入的包 Packed data to be written
   * @param timestamp 需要打包的消息时间戳 Message timestamp to be packed
   * @param data 需要打包的消息数据 Message data to be packed
   */
  static void PackData(uint32_t topic_name_crc32, RawData buffer,
                       MicrosecondTimestamp timestamp, ConstRawData data);

  /**
   * @brief  转储数据到 PackedData
   *         Dumps data into PackedData format
   * @tparam Data 数据类型 Data type
   * @param  data 存储数据的 PackedData 结构 PackedData structure to store data
   */
  template <typename Data>
  ErrorCode DumpData(PackedData<Data>& data);

  /**
   * @brief 转储数据到原始缓冲区。Dumps data into a raw buffer.
   */
  template <SizeLimitMode Mode = SizeLimitMode::MORE>
  ErrorCode DumpData(RawData data)
  {
    MicrosecondTimestamp timestamp;
    return DumpPayload<Mode>(data, timestamp);
  }

  template <SizeLimitMode Mode = SizeLimitMode::MORE>
  ErrorCode DumpData(RawData data, MicrosecondTimestamp& timestamp)
  {
    return DumpPayload<Mode>(data, timestamp);
  }

  /**
   * @brief  转储数据到普通数据结构
   *         Dumps data into a normal data structure
   *
   * @note 允许目标类型只覆盖当前 payload 的前缀，例如把扩展 payload 转储到基础
   *       payload 结构。若目标类型大于当前 payload，则返回 SIZE_ERR。
   *       The destination type may cover only a prefix of the current payload, for
   *       example dumping an extended payload into a base payload structure. SIZE_ERR
   *       is returned if the destination type is larger than the current payload.
   * @tparam Data 数据类型 Data type
   * @param  data 存储数据的变量 Variable to store the data
   */
  template <typename Data>
    requires(!std::same_as<std::remove_cv_t<Data>, RawData>)
  ErrorCode DumpData(Data& data)
  {
    MicrosecondTimestamp timestamp;
    return DumpData(data, timestamp);
  }

  template <typename Data>
    requires(!std::same_as<std::remove_cv_t<Data>, RawData>)
  ErrorCode DumpData(Data& data, MicrosecondTimestamp& timestamp)
  {
    CheckTopicPayload<Data>();
    return DumpPayload<SizeLimitMode::LESS>(RawData(data), timestamp);
  }

  MicrosecondTimestamp GetTimestamp() const;

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

  class Server;

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
   * @brief 校验订阅者数据类型和主题最大长度是否兼容。
   *        Checks whether subscriber data type is compatible with topic max length.
   */
  template <typename Data>
  static void CheckSubscriberDataSize(Topic topic)
  {
    CheckTopicPayload<Data>();
    if (topic.block_->data_.check_length)
    {
      ASSERT(topic.block_->data_.max_length == sizeof(Data));
    }
    else
    {
      ASSERT(topic.block_->data_.max_length <= sizeof(Data));
    }
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

  template <SizeLimitMode Mode = SizeLimitMode::MORE>
  ErrorCode DumpPayload(RawData buffer, MicrosecondTimestamp& timestamp)
  {
    if (block_->data_.data.addr_ == nullptr)
    {
      return ErrorCode::EMPTY;
    }

    const size_t payload_size = block_->data_.data.size_;
    size_t copy_size = payload_size;

    if constexpr (Mode == SizeLimitMode::EQUAL)
    {
      if (payload_size != buffer.size_)
      {
        return ErrorCode::SIZE_ERR;
      }
    }
    else if constexpr (Mode == SizeLimitMode::LESS)
    {
      if (payload_size < buffer.size_)
      {
        return ErrorCode::SIZE_ERR;
      }
      copy_size = buffer.size_;
    }
    else if constexpr (Mode == SizeLimitMode::MORE)
    {
      if (payload_size > buffer.size_)
      {
        return ErrorCode::SIZE_ERR;
      }
    }

    Lock(block_);
    timestamp = block_->data_.timestamp;
    LibXR::Memory::FastCopy(buffer.addr_, block_->data_.data.addr_, copy_size);
    Unlock(block_);

    return ErrorCode::OK;
  }

  template <SizeLimitMode Mode = SizeLimitMode::MORE>
  ErrorCode DumpPacket(RawData buffer)
  {
    if (block_->data_.data.addr_ == nullptr)
    {
      return ErrorCode::EMPTY;
    }

    Assert::SizeLimitCheck<Mode>(PACK_BASE_SIZE + block_->data_.data.size_, buffer.size_);
    Lock(block_);
    PackData(block_->data_.crc32, buffer, block_->data_.timestamp, block_->data_.data);
    Unlock(block_);

    return ErrorCode::OK;
  }

  void PublishRaw(void* addr, uint32_t size, MicrosecondTimestamp timestamp,
                  bool from_callback, bool in_isr);

  static void CheckPublishSize(TopicHandle topic, uint32_t size);
  static RawData StorePublishedData(TopicHandle topic, void* addr, uint32_t size,
                                    MicrosecondTimestamp timestamp);
  static void DispatchSubscriber(SuberBlock& block, MicrosecondTimestamp timestamp,
                                 RawData data, bool from_callback, bool in_isr);
  static void DispatchSubscribers(TopicHandle topic, MicrosecondTimestamp timestamp,
                                  RawData data, bool from_callback, bool in_isr);
};
}  // namespace LibXR

#include "message/packet.hpp"
#include "message/server.hpp"
#include "message/subscriber.hpp"
