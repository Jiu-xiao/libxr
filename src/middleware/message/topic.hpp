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
/**
 * @brief 可直接拿来发 topic 的数据类型要求 / Data-type requirements for values that can
 *        be published through a topic directly
 * @tparam Data 待检查的数据类型 / Data type to check
 */
template <typename Data>
concept TopicPayload =
    !std::is_reference_v<Data> && !std::is_const_v<Data> && !std::is_volatile_v<Data> &&
    std::is_object_v<Data> && std::is_default_constructible_v<Data> &&
    std::is_copy_assignable_v<Data> && std::is_trivially_destructible_v<Data>;

/**
 * @brief 编译期校验主题负载类型 / Check the topic payload type at compile time
 * @tparam Data 待校验的数据类型 / Data type to validate
 *
 * @note 当前 topic 负载会被直接按对象字节拷贝、缓存、排队和打包，因此这里只接受
 *       默认构造、可赋值、平凡析构的普通对象类型 /
 *       Topic payloads are currently copied, cached, queued, and packed directly
 *       as object bytes, so only regular object types that are default
 *       constructible, assignable, and trivially destructible are accepted here
 */
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
 * 一个 `Topic` 表示一种消息：它保存最近一次发布的内容，并在每次发布时同步通知挂在
 * 上面的同步、异步、队列或回调订阅者。
 * One `Topic` represents one message stream: it keeps the latest published value
 * and synchronously notifies the attached synchronous, asynchronous, queued, or
 * callback subscribers on each publish.
 *
 * @note Topic、subscriber 和 server 应在初始化阶段创建，并在应用运行期间长期存在 /
 *       Topics, subscribers, and servers are expected to be created during
 *       initialization and kept alive for the application lifetime
 * @note 该消息系统遵循 libxr 的静态生命周期模型：初始化阶段允许分配，发布、回调和解析热路径不应产生动态分配 /
 *       The message system follows libxr's static lifetime model: allocation is
 *       allowed during initialization, while publish, callback, and parser hot
 *       paths should not allocate dynamically
 */
class Topic
{
  /**
   * @enum LockState
   * @brief 主题发布锁状态 / Topic publish-lock state
   */
  enum class LockState : uint32_t
  {
    UNLOCKED = 0,        ///< 当前未被发布路径持有。Not currently held by a publish path.
    LOCKED = 1,          ///< 当前正被无 mutex 主题的一次发布持有。Currently held by one publish on a non-mutex topic.
    USE_MUTEX = UINT32_MAX, ///< 当前主题改用外部 mutex 进行串行化。This topic is serialized by an external mutex instead.
  };

 public:
  /**
   * @struct Block
   * @brief topic 自己手里那份状态和参数 / The state and settings held by the topic
   *        itself
   *
   * @note `subers` 通常只在初始化期追加，发布热路径只遍历；`busy/mutex` 保护数据存储和订阅者分发 /
   *       `subers` is normally appended during initialization and traversed on
   *       the publish hot path; `busy/mutex` protects data storage and
   *       subscriber dispatch
   */
  struct Block
  {
    std::atomic<LockState> busy;  ///< 非 mutex 主题的发布锁状态。Publish-lock state for non-mutex topics.
    LockFreeList subers;          ///< 当前挂接的订阅者块列表。List of subscriber blocks currently attached to the topic.
    uint32_t max_length;          ///< 该主题声明允许的最大负载长度。Declared maximum payload length of this topic.
    uint32_t crc32;  ///< 主题名称的 CRC32 校验码。CRC32 checksum of the topic name.
    Mutex* mutex;    ///< 多发布者主题使用的串行化 mutex。Serialization mutex used by multi-publisher topics.
    RawData data;    ///< 当前 topic 保存的最近一次负载视图。View of the latest payload currently retained by the topic.
    MicrosecondTimestamp timestamp;  ///< 最近一次消息时间戳。Latest message timestamp.
    bool cache;         ///< 是否把最近一次负载复制进 topic 自有缓存。Whether the latest payload is copied into topic-owned cache storage.
    bool check_length;  ///< 是否要求发布/订阅尺寸与 `max_length` 精确相等。Whether publish/subscribe sizes must match `max_length` exactly.
  };

#ifndef __DOXYGEN__
  /**
   * @struct PackedDataHeader
   * @brief 打包消息固定头，具体定义见 `packet/packet.hpp` / Packed-message fixed
   *        header; the full definition lives in `packet/packet.hpp`
   */
  struct PackedDataHeader;

  /**
   * @class PackedData
   * @brief 打包消息对象模板，具体定义见 `packet/packet.hpp` / Packed-message object
   *        template; the full definition lives in `packet/packet.hpp`
   * @tparam Data 打包负载的数据类型 / Packed payload data type
   */
  template <typename Data>
  class PackedData;

  static constexpr uint8_t PACKET_PREFIX = 0x5A;  ///< 打包消息固定前缀字节。Fixed prefix byte of packed messages.
  static constexpr size_t PACK_BASE_SIZE = 18;  ///< 不含 payload 的包固定开销：17 字节头加 1 字节尾 CRC8。Fixed non-payload packet overhead: 17-byte header plus 1-byte trailing CRC8.
#endif

  /**
   * @typedef TopicHandle
   * @brief 指向某个 topic 节点的句柄 / Handle pointing to one topic node
   */
  typedef RBTree<uint32_t>::Node<Block>* TopicHandle;

  /**
   * @struct MessageView
   * @brief 带时间戳的类型化消息视图 / Timestamped typed message view
   * @tparam Data 消息里的数据类型 / Payload data type stored in the message
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
   * @brief 带时间戳的类型化消息 / Timestamped typed message
   * @tparam Data 消息里的数据类型 / Payload data type stored in the message
   */
  template <typename Data>
  struct Message
  {
    static_assert(TopicPayload<Data>);

    MicrosecondTimestamp timestamp;  ///< 消息时间戳。Message timestamp.
    Data data;                       ///< 消息数据。Message payload.
  };

  /**
   * @brief 锁定一次普通发布路径 / Lock one normal publish path
   * @param topic 要锁定的主题 / Topic to be locked
   *
   * @note 这把锁保护“写入当前负载 + 同步分发订阅者”这整个发布临界区；它不是给订阅者读取
   *       单独使用的外部互斥量 /
   *       This lock protects the whole publish critical section of "store the
   *       current payload + synchronously dispatch subscribers"; it is not an
   *       external mutex for subscriber-side reads
   */
  static void Lock(TopicHandle topic);

  /**
   * @brief 结束一次普通发布路径 / Finish one normal publish path
   * @param topic 要解锁的主题 / Topic to be unlocked
   */
  static void Unlock(TopicHandle topic);

  /**
   * @brief 锁定一次回调发布路径 / Lock one callback-originated publish path
   * @param topic 要锁定的主题 / Topic to be locked
   *
   * @note 仅适用于非 mutex 主题；这条路径要求外围上下文自己保证不会与普通发布或另一条
   *       回调发布并发进入同一个 topic /
   *       This is valid only for non-mutex topics; the surrounding context must
   *       already guarantee that neither a normal publish nor another callback
   *       publish can enter the same topic concurrently
   */
  static void LockFromCallback(TopicHandle topic);

  /**
   * @brief 结束一次回调发布路径 / Finish one callback-originated publish path
   * @param topic 要解锁的主题 / Topic to be unlocked
   */
  static void UnlockFromCallback(TopicHandle topic);

  /**
   * @class Domain
   * @brief 一组 topic 共用的查找范围 / Shared lookup scope for one group of topics
   */
  class Domain
  {
   public:
    /**
     * @brief 初始化或查找指定名称的主题域 / Initialize or look up a domain by name
     * @param name 主题域的名称 / Name of the domain
     *
     * @note 包含初始化期动态内存分配，domain 应长期存在 /
     *       Contains initialization-time dynamic allocation; domains are
     *       expected to be long-lived
     */
    Domain(const char* name);

    /**
     * @brief 这个域下面那棵 topic 树的根节点 / Root node of the topic tree owned by
     *        this domain
     */
    RBTree<uint32_t>::Node<RBTree<uint32_t>>* node_;
  };

  /**
   * @enum SuberType
   * @brief 订阅记录类型枚举，具体定义见 `subscriber/base.hpp` / Subscriber-record
   *        type enum; the full definition lives in `subscriber/base.hpp`
   */
  enum class SuberType : uint8_t;

  /**
   * @struct SuberBlock
   * @brief 订阅记录公共头，具体定义见 `subscriber/base.hpp` / Common subscriber-block
   *        header; the full definition lives in `subscriber/base.hpp`
   */
  struct SuberBlock;

  /**
   * @struct SyncBlock
   * @brief 同步订阅块，具体定义见 `subscriber/sync.hpp` / Synchronous subscriber
   *        block; the full definition lives in `subscriber/sync.hpp`
   */
  struct SyncBlock;

  /**
   * @class SyncSubscriber
   * @brief 同步订阅者模板，具体定义见 `subscriber/sync.hpp` / Synchronous subscriber
   *        template; the full definition lives in `subscriber/sync.hpp`
   * @tparam Data 订阅的数据类型 / Subscribed data type
   */
  template <typename Data>
  class SyncSubscriber;

  /**
   * @enum ASyncSubscriberState
   * @brief 异步订阅状态枚举，具体定义见 `subscriber/async.hpp` / Async subscriber
   *        state enum; the full definition lives in `subscriber/async.hpp`
   */
  enum class ASyncSubscriberState : uint32_t;

  /**
   * @struct ASyncBlock
   * @brief 异步订阅块，具体定义见 `subscriber/async.hpp` / Asynchronous subscriber
   *        block; the full definition lives in `subscriber/async.hpp`
   */
  struct ASyncBlock;

  /**
   * @class ASyncSubscriber
   * @brief 异步订阅者模板，具体定义见 `subscriber/async.hpp` / Asynchronous subscriber
   *        template; the full definition lives in `subscriber/async.hpp`
   * @tparam Data 订阅的数据类型 / Subscribed data type
   */
  template <typename Data>
  class ASyncSubscriber;

  /**
   * @struct QueueBlock
   * @brief 队列订阅块，具体定义见 `subscriber/queue.hpp` / Queued subscriber block;
   *        the full definition lives in `subscriber/queue.hpp`
   */
  struct QueueBlock;

  /**
   * @class QueuedSubscriber
   * @brief 队列订阅者，具体定义见 `subscriber/queue.hpp` / Queued subscriber; the full
   *        definition lives in `subscriber/queue.hpp`
   */
  class QueuedSubscriber;

  /**
   * @class Callback
   * @brief 回调订阅句柄，具体定义见 `subscriber/callback.hpp` / Callback subscription
   *        handle; the full definition lives in `subscriber/callback.hpp`
   */
  class Callback;

  /**
   * @struct CallbackBlock
   * @brief 回调订阅块，具体定义见 `subscriber/callback.hpp` / Callback subscriber
   *        block; the full definition lives in `subscriber/callback.hpp`
   */
  struct CallbackBlock;

  /**
   * @brief 注册回调函数 / Register a callback function
   * @param cb 需要注册的回调函数 / The callback function to register
   *
   * @note 包含初始化期动态内存分配，回调订阅应长期存在 /
   *       Contains initialization-time dynamic allocation; callback
   *       subscriptions are expected to be long-lived
   * @note 回调在发布锁内运行，不应重入发布同一个主题 /
   *       The callback runs while the publish lock is held and should not
   *       re-enter publishing on the same topic
   */
  void RegisterCallback(Callback& cb);

  /**
   * @brief 取默认发布时间戳 / Get the default publish timestamp
   * @return 当前 Timebase 微秒时间戳 / Current Timebase microsecond timestamp
   */
  static MicrosecondTimestamp NowTimestamp();

  /**
   * @brief 构造一个空 topic 句柄 / Construct one empty topic handle
   */
  Topic();

  /**
   * @brief 按名字查找或创建一个 topic / Find or create one topic by name
   * @param name 主题名称 / Topic name
   * @param max_length 数据的最大长度 / Maximum length of data
   * @param domain 主题所属的域，默认为 `nullptr` / Domain to which the topic belongs, default `nullptr`
   * @param multi_publisher 是否允许多个发布者，默认为 `false` / Whether to allow multiple publishers, default `false`
   * @param cache 是否启用缓存，默认为 `false` / Whether to enable caching, default `false`
   * @param check_length 是否检查数据长度，默认为 `false` / Whether to check data length, default `false`
   *
   * @note 包含初始化期动态内存分配，主题应长期存在 /
   *       Contains initialization-time dynamic allocation; topics are expected
   *       to be long-lived
   * @note 未启用缓存时，Topic 只保留最近一次发布数据的地址；调用者必须保证该地址在 `DumpData()` 前仍然有效 /
   *       Without cache, Topic keeps only the last published address; the
   *       caller must keep it valid until `DumpData()`
   * @note 多发布者主题使用 mutex，不支持 `PublishFromCallback()` /
   *       Multi-publisher topics use a mutex and do not support
   *       `PublishFromCallback()`
   */
  Topic(const char* name, uint32_t max_length, Domain* domain = nullptr,
        bool multi_publisher = false, bool cache = false, bool check_length = false);

  /**
   * @brief 按数据类型创建一个 topic / Create one topic from a payload type
   * @tparam Data 主题数据类型 / Topic data type
   * @param name 主题名称 / Topic name
   * @param domain 主题所属的域，默认为 `nullptr` / Domain to which the topic belongs, default `nullptr`
   * @param multi_publisher 是否允许多个发布者，默认为 `false` / Whether to allow multiple publishers, default `false`
   * @param cache 是否启用缓存，默认为 `false` / Whether to enable caching, default `false`
   * @param check_length 是否检查数据长度，默认为 `false` / Whether to check data length, default `false`
   * @return 创建的 Topic 实例 / Returns the created Topic instance
   * @note 包含初始化期动态内存分配，主题应长期存在 / Contains initialization-time dynamic allocation; topics are expected to be long-lived
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
   * @brief 通过句柄构造主题 / Construct a topic from a topic handle
   * @param topic 主题句柄 / Topic handle
   * @note 这只是一个轻量包装，不创建、不校验、也不接管该句柄的生命周期 /
   *       This is only one lightweight wrapper; it neither creates, validates,
   *       nor takes ownership of the handle lifetime
   */
  Topic(TopicHandle topic);

  /**
   * @brief 在指定域中查找主题 / Find a topic in the specified domain
   * @param name 主题名称 / Topic name
   * @param domain 待查找的主题域，默认为 `nullptr` / Domain to search in, default `nullptr`
   * @return 如果找到则返回主题句柄，否则返回 `nullptr` / Returns the topic handle if found, otherwise returns `nullptr`
   * @note 查询不会创建默认域；默认域尚未初始化时返回 `nullptr` / Lookup does not create the default domain; it returns `nullptr` when the default domain has not been initialized
   */
  static TopicHandle Find(const char* name, Domain* domain = nullptr);

  /**
   * @brief 在指定域中查找或创建主题 / Find or create a topic in the specified domain
   * @tparam Data 数据类型 / Data type
   * @param name 主题名称 / Topic name
   * @param domain 可选的域指针，默认为 `nullptr` / Optional domain pointer, default
   *        `nullptr`
   * @param multi_publisher 是否允许多个发布者，默认为 `false` / Whether to allow
   *        multiple publishers, default `false`
   * @param cache 是否启用缓存，默认为 `false` / Whether to enable caching, default
   *        `false`
   * @param check_length 是否检查数据长度，默认为 `false` / Whether to check data
   *        length, default `false`
   * @return 主题句柄 / Returns the topic handle
   * @note 包含初始化期动态内存分配，主题应长期存在 / Contains initialization-time dynamic allocation; topics are expected to be long-lived
   * @note 若该名称的 topic 已存在，这里直接返回现有句柄，不会重新检查这些创建参数 /
   *       If a topic with the same name already exists, this returns the
   *       existing handle directly and does not re-check these creation options
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
   * @brief 启用主题的缓存功能 / Enable caching for the topic
   * @note 包含初始化期动态内存分配，缓存启用后应长期存在 / Contains initialization-time dynamic allocation; the cache is expected to remain enabled for the topic lifetime
   * @note 若缓存原本已经启用，这里不会重复分配 /
   *       If caching is already enabled, this does not allocate again
   * @note 该操作只负责分配 topic 自有缓存；若之前已经有“未缓存”的最近一次发布地址，
   *       这里不会回填旧数据 /
   *       This only allocates topic-owned cache storage; if the latest publish
   *       was previously retained as a non-cached external address, that old
   *       payload is not backfilled here
   */
  void EnableCache();

  /**
   * @brief 发布类型化数据 / Publish typed data
   * @tparam Data 数据类型 / Data type
   * @param data 需要发布的数据 / Data to be published
   * @note 对于未启用缓存的 topic，若这里传入的是外部对象，topic 只保留它的地址直到下一次
   *       发布或 `DumpData()` 读取 /
   *       For a non-cached topic, when this publishes an external object the
   *       topic retains only its address until the next publish or `DumpData()`
   */
  template <typename Data>
  void Publish(Data& data)
  {
    CheckTopicPayload<Data>();
    Publish(static_cast<void*>(&data), static_cast<uint32_t>(sizeof(Data)));
  }

  /**
   * @brief 发布带显式时间戳的类型化数据 / Publish typed data with an explicit timestamp
   * @tparam Data 数据类型 / Data type
   * @param data 需要发布的数据 / Data to be published
   * @param timestamp 发布使用的时间戳 / Timestamp used for publishing
   * @note 对于未启用缓存的 topic，若这里传入的是外部对象，topic 只保留它的地址直到下一次
   *       发布或 `DumpData()` 读取 /
   *       For a non-cached topic, when this publishes an external object the
   *       topic retains only its address until the next publish or `DumpData()`
   */
  template <typename Data>
  void Publish(Data& data, MicrosecondTimestamp timestamp)
  {
    CheckTopicPayload<Data>();
    Publish(static_cast<void*>(&data), static_cast<uint32_t>(sizeof(Data)), timestamp);
  }

  /**
   * @brief 以原始地址和大小发布数据 / Publish data using raw address and size
   * @param addr 数据的地址 / Address of the data
   * @param size 数据大小 / Size of the data
   * @note `size` 必须满足该 topic 的尺寸规则：`check_length=true` 时要求等于
   *       `max_length`，否则要求不超过 `max_length` /
   *       `size` must satisfy the topic's size rule: it must equal
   *       `max_length` when `check_length=true`, otherwise it must not exceed
   *       `max_length`
   * @note 未启用缓存时，topic 只保留 `addr` 指向的那段外部数据地址，直到下一次发布或
   *       `DumpData()` 读取 /
   *       Without cache, the topic keeps only the external address pointed to
   *       by `addr` until the next publish or `DumpData()`
   */
  void Publish(void* addr, uint32_t size);

  /**
   * @brief 以原始地址和大小发布带显式时间戳的数据 / Publish raw data with an explicit timestamp
   * @param addr 数据的地址 / Address of the data
   * @param size 数据大小 / Size of the data
   * @param timestamp 发布使用的时间戳 / Timestamp used for publishing
   * @note 未启用缓存时，topic 只保留 `addr` 指向的那段外部数据地址，直到下一次发布或
   *       `DumpData()` 读取 /
   *       Without cache, the topic keeps only the external address pointed to
   *       by `addr` until the next publish or `DumpData()`
   */
  void Publish(void* addr, uint32_t size, MicrosecondTimestamp timestamp);

  /**
   * @brief 在回调函数中发布类型化数据 / Publish typed data in a callback
   * @tparam Data 数据类型 / Data type
   * @param data 需要发布的数据 / Data to be published
   * @param in_isr 是否在中断中发布数据 / Whether to publish data in an interrupt
   * @note 对于未启用缓存的 topic，若这里传入的是外部对象，topic 只保留它的地址直到下一次
   *       发布或 `DumpData()` 读取 /
   *       For a non-cached topic, when this publishes an external object the
   *       topic retains only its address until the next publish or `DumpData()`
   */
  template <typename Data>
  void PublishFromCallback(Data& data, bool in_isr)
  {
    CheckTopicPayload<Data>();
    PublishFromCallback(static_cast<void*>(&data), static_cast<uint32_t>(sizeof(Data)),
                        in_isr);
  }

  /**
   * @brief 在回调函数中发布带显式时间戳的类型化数据 / Publish typed data with an explicit timestamp in a callback
   * @tparam Data 数据类型 / Data type
   * @param data 需要发布的数据 / Data to be published
   * @param timestamp 发布使用的时间戳 / Timestamp used for publishing
   * @param in_isr 是否在中断中发布数据 / Whether to publish data in an interrupt
   * @note 对于未启用缓存的 topic，若这里传入的是外部对象，topic 只保留它的地址直到下一次
   *       发布或 `DumpData()` 读取 /
   *       For a non-cached topic, when this publishes an external object the
   *       topic retains only its address until the next publish or `DumpData()`
   */
  template <typename Data>
  void PublishFromCallback(Data& data, MicrosecondTimestamp timestamp, bool in_isr)
  {
    CheckTopicPayload<Data>();
    PublishFromCallback(static_cast<void*>(&data), static_cast<uint32_t>(sizeof(Data)),
                        timestamp, in_isr);
  }

  /**
   * @brief 在回调函数中以原始地址和大小发布数据 / Publish data using raw address and size in a callback
   * @param addr 数据的地址 / Address of the data
   * @param size 数据大小 / Size of the data
   * @param in_isr 是否在中断中发布数据 / Whether to publish data in an interrupt
   * @note 仅适用于非 mutex 主题；multi_publisher 主题会在回调发布路径触发断言 / Only valid for non-mutex topics; multi-publisher topics assert on the callback publish path
   * @note 未启用缓存时，topic 只保留 `addr` 指向的那段外部数据地址，直到下一次发布或
   *       `DumpData()` 读取 /
   *       Without cache, the topic keeps only the external address pointed to
   *       by `addr` until the next publish or `DumpData()`
   * @note 回调发布路径不分配内存，但仍会同步分发到该主题的订阅者 / The callback publish path does not allocate, but still dispatches to the topic subscribers synchronously
   */
  void PublishFromCallback(void* addr, uint32_t size, bool in_isr);

  /**
   * @brief 在回调函数中以原始地址和大小发布带显式时间戳的数据 / Publish raw data with an explicit timestamp in a callback
   * @param addr 数据的地址 / Address of the data
   * @param size 数据大小 / Size of the data
   * @param timestamp 发布使用的时间戳 / Timestamp used for publishing
   * @param in_isr 是否在中断中发布数据 / Whether to publish data in an interrupt
   * @note 仅适用于非 mutex 主题；multi_publisher 主题会在回调发布路径触发断言 / Only valid for non-mutex topics; multi-publisher topics assert on the callback publish path
   * @note 未启用缓存时，topic 只保留 `addr` 指向的那段外部数据地址，直到下一次发布或
   *       `DumpData()` 读取 /
   *       Without cache, the topic keeps only the external address pointed to
   *       by `addr` until the next publish or `DumpData()`
   * @note 回调发布路径不分配内存，但仍会同步分发到该主题的订阅者 / The callback publish path does not allocate, but still dispatches to the topic subscribers synchronously
   */
  void PublishFromCallback(void* addr, uint32_t size, MicrosecondTimestamp timestamp,
                           bool in_isr);

  /**
   * @brief 打包数据 / Pack topic data into a packet buffer
   * @param topic_name_crc32 话题名称的 CRC32 校验码 / CRC32 checksum of the topic name
   * @param buffer 等待写入的包 / Packed data buffer to be written
   * @param timestamp 需要打包的消息时间戳 / Message timestamp to be packed
   * @param data 需要打包的消息数据 / Message data to be packed
   *
   * @note 这一步只负责按 message 的打包报文格式写字节，不查 topic、不分发订阅者 /
   *       This step only writes bytes in the message packet format; it does not
   *       look up topics or dispatch subscribers
   * @note `buffer` 必须至少提供 `PACK_BASE_SIZE + data.size_` 字节 /
   *       `buffer` must provide at least `PACK_BASE_SIZE + data.size_` bytes
   */
  static void PackData(uint32_t topic_name_crc32, RawData buffer,
                       MicrosecondTimestamp timestamp, ConstRawData data);

  /**
   * @brief 把最近一次发布的数据拷成一个打包报文 / Copy the latest published data into
   *        one packed message object
   * @tparam Data 数据类型 / Data type
   * @param data 用来接收结果的 `PackedData` / `PackedData` object that receives the result
   * @return 操作结果错误码 / Error code indicating the operation result
   */
  template <typename Data>
  ErrorCode DumpData(PackedData<Data>& data);

  /**
   * @brief 把最近一次发布的 payload 拷到原始缓冲区 / Copy the latest published payload
   *        into a raw buffer
   * @tparam Mode 尺寸检查模式 / Size check mode
   * @param data 存储数据的原始缓冲区 / Raw buffer used to store data
   * @return 操作结果错误码 / Error code indicating the operation result
   */
  template <SizeLimitMode Mode = SizeLimitMode::MORE>
  ErrorCode DumpData(RawData data)
  {
    MicrosecondTimestamp timestamp;
    return DumpPayload<Mode>(data, timestamp);
  }

  /**
   * @brief 把最近一次发布的 payload 拷到原始缓冲区，并带回时间戳 / Copy the latest
   *        published payload into a raw buffer and return its timestamp
   * @tparam Mode 尺寸检查模式 / Size check mode
   * @param data 存储数据的原始缓冲区 / Raw buffer used to store data
   * @param timestamp 用来带回消息时间戳 / Receives the message timestamp
   * @return 操作结果错误码 / Error code indicating the operation result
   */
  template <SizeLimitMode Mode = SizeLimitMode::MORE>
  ErrorCode DumpData(RawData data, MicrosecondTimestamp& timestamp)
  {
    return DumpPayload<Mode>(data, timestamp);
  }

  /**
   * @brief 把最近一次发布的数据拷到普通对象里 / Copy the latest published data into a
   *        typed object
   * @tparam Data 数据类型 / Data type
   * @param data 用来接收结果的对象 / Destination object receiving the result
   * @return 操作结果错误码 / Error code indicating the operation result
   * @note 目标对象可以只接前面一段字节，例如用一个更小的结构去接一份更大的 payload；
   *       但如果目标对象反而比当前 payload 更大，就会返回 `SIZE_ERR` /
   *       The destination object may receive only the leading bytes, for example
   *       using a smaller base struct to receive a larger payload; but if the
   *       destination object is larger than the current payload,
   *       `SIZE_ERR` is returned
   */
  template <typename Data>
    requires(!std::same_as<std::remove_cv_t<Data>, RawData>)
  ErrorCode DumpData(Data& data)
  {
    MicrosecondTimestamp timestamp;
    return DumpData(data, timestamp);
  }

  /**
   * @brief 把最近一次发布的数据拷到普通对象里，并带回时间戳 / Copy the latest
   *        published data into a typed object and return its timestamp
   * @tparam Data 数据类型 / Data type
   * @param data 用来接收结果的对象 / Destination object receiving the result
   * @param timestamp 用来带回消息时间戳 / Receives the message timestamp
   * @return 操作结果错误码 / Error code indicating the operation result
   */
  template <typename Data>
    requires(!std::same_as<std::remove_cv_t<Data>, RawData>)
  ErrorCode DumpData(Data& data, MicrosecondTimestamp& timestamp)
  {
    CheckTopicPayload<Data>();
    return DumpPayload<SizeLimitMode::LESS>(RawData(data), timestamp);
  }

  /**
   * @brief 取这个 topic 记着的最近一次时间戳 / Get the latest timestamp currently kept
   *        by this topic
   * @return 最近一次消息时间戳 / Returns the latest message timestamp
   * @note 若当前句柄为空，则返回默认构造的时间戳 /
   *       Returns a default-constructed timestamp when the current handle is empty
   */
  MicrosecondTimestamp GetTimestamp() const;

  /**
   * @brief 等待主题创建并返回其句柄 / Wait for a topic to be created and return its handle
   * @param name 主题名称 / Topic name
   * @param timeout 等待超时时间，默认为 `UINT32_MAX` / Timeout duration to wait, default `UINT32_MAX`
   * @param domain 待查找的主题域，默认为 `nullptr` / Domain in which to search for the topic, default `nullptr`
   * @return 如果找到主题则返回其句柄，否则返回 `nullptr` / Returns the topic handle if found, otherwise returns `nullptr`
   * @note 该等待通过 `Find()` + `Thread::Sleep(1)` 轮询实现，不是事件驱动注册通知 /
   *       This wait is implemented by polling `Find()` plus `Thread::Sleep(1)`,
   *       not by an event-driven registration notification
   */
  static TopicHandle WaitTopic(const char* name, uint32_t timeout = UINT32_MAX,
                               Domain* domain = nullptr);

  /**
   * @brief 将 `Topic` 转换为 `TopicHandle` / Convert `Topic` to `TopicHandle`
   * @return 主题句柄 / Returns the topic handle
   */
  operator TopicHandle() { return block_; }

  /**
   * @brief 获取主题的键值 / Get the topic key value
   * @return 主题键值 / Returns the topic key value
   * @note 若当前句柄为空，则返回 `0` /
   *       Returns `0` when the current handle is empty
   */
  uint32_t GetKey() const;

  /**
   * @class Server
   * @brief 把字节流拼成消息再发布的解析器 / Parser that assembles byte streams into
   *        messages and then publishes them
   * @note 具体定义见 `server/server.hpp` / The full definition lives in
   *       `server/server.hpp`
   */
  class Server;

 private:
  /**
   * @brief 当前这个 `Topic` 视图指向的节点 / Node currently pointed to by this
   *        `Topic` view
   */
  TopicHandle block_ = nullptr;

  /**
   * @brief 全局 domain 表 / Global table of domains
   * @note 初始化期懒创建；调用者应在并发发布前完成 topic 和 domain 创建 /
   *       Lazily created during initialization; callers should finish topic and
   *       domain creation before concurrent publishing starts
   */
  static inline RBTree<uint32_t>* domain_ = nullptr;

  /**
   * @brief 没显式指定 domain 时使用的默认 domain / Default domain used when no domain
   *        is specified explicitly
   */
  static inline Domain* def_domain_ = nullptr;

  /**
   * @brief 确保主题域注册表已创建 / Ensure the topic-domain registry exists
   */
  static void EnsureDomainRegistry();

  /**
   * @brief 确保默认主题域已创建 / Ensure the default topic domain exists
   * @return 默认主题域指针 / Returns the default topic domain pointer
   */
  static Domain* EnsureDefaultDomain();

  /**
   * @brief 按尺寸规则把最近一次发布的 payload 拷到原始缓冲区 / Copy the latest
   *        published payload into a raw buffer under one size rule
   * @tparam Mode 尺寸检查模式 / Size check mode
   * @param buffer 接收结果的原始缓冲区 / Raw buffer receiving the copied payload
   * @param timestamp 用来带回消息时间戳 / Receives the message timestamp
   * @return 操作结果错误码 / Error code indicating the operation result
   * @note `EQUAL` 要求缓冲区和当前 payload 等长；`LESS` 允许只把 payload 前缀拷到
   *       更小的目标；`MORE` 要求目标至少完整容纳当前 payload /
   *       `EQUAL` requires the buffer to match the current payload size exactly;
   *       `LESS` allows dumping only the payload prefix into a smaller
   *       destination; `MORE` requires the destination to hold the full current
   *       payload
   */
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

  /**
   * @brief 按尺寸规则把最近一次发布的数据拷成打包报文 / Copy the latest published
   *        data into a packed message under one size rule
   * @tparam Mode 尺寸检查模式 / Size check mode
   * @param buffer 存储打包报文的原始缓冲区 / Raw buffer receiving the packed message
   * @return 操作结果错误码 / Error code indicating the operation result
   */
  template <SizeLimitMode Mode = SizeLimitMode::MORE>
  ErrorCode DumpPacket(RawData buffer);

  /**
   * @brief 执行一次底层发布流程 / Run one low-level publish flow
   * @param addr 数据地址 / Address of the data
   * @param size 数据大小 / Size of the data
   * @param timestamp 本次发布使用的时间戳 / Timestamp used for this publish
   * @param from_callback 是否来自回调发布路径 / Whether the publish comes from a callback path
   * @param in_isr 当前回调是否运行在中断上下文 / Whether the current callback runs in ISR context
   */
  void PublishRaw(void* addr, uint32_t size, MicrosecondTimestamp timestamp,
                  bool from_callback, bool in_isr);

  /**
   * @brief 检查这次发布的字节数能不能发到这个 topic / Check whether this publish size
   *        is acceptable for the topic
   * @param topic 目标主题句柄 / Target topic handle
   * @param size 本次发布数据大小 / Size of the published data
   * @note `check_length=true` 时要求精确等长，否则只要求 `size <= max_length` /
   *       When `check_length=true`, the size must match exactly; otherwise the
   *       only requirement is `size <= max_length`
   */
  static void CheckPublishSize(TopicHandle topic, uint32_t size);

  /**
   * @brief 按这个 topic 的缓存设置，留下这次发布的数据和时间戳 / Keep the data and
   *        timestamp of this publish according to the topic's cache setting
   * @param topic 目标主题句柄 / Target topic handle
   * @param addr 数据地址 / Address of the data
   * @param size 数据大小 / Size of the data
   * @param timestamp 本次发布使用的时间戳 / Timestamp used for this publish
   * @return 当前主题内最终保存的数据视图 / Returns the final data view stored in the topic
   * @note 启用缓存时会把 payload 拷入 topic 自有缓冲区；否则只保留调用者传入的原始地址 /
   *       With cache enabled the payload is copied into topic-owned storage;
   *       otherwise only the caller-provided raw address is retained
   */
  static RawData StorePublishedData(TopicHandle topic, void* addr, uint32_t size,
                                    MicrosecondTimestamp timestamp);
};
}  // namespace LibXR
