#pragma once

#include <atomic>
#include <cstdint>

#include "crc.hpp"
#include "libxr_cb.hpp"
#include "libxr_def.hpp"
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
 * @class Topic
 * @brief 主题（Topic）管理类 / Topic management class
 *
 * 该类提供了基于发布-订阅模式的主题管理，支持同步、异步、队列和回调订阅者，以及数据的缓存和校验机制。
 * This class provides topic management based on the publish-subscribe model, supporting
 * synchronous, asynchronous, queue-based, and callback subscribers, as well as data
 * caching and validation mechanisms.
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
   * @brief 存储主题（Topic）数据的结构体。Structure storing topic data.
   */
  struct Block
  {
    std::atomic<LockState> busy;  ///< 是否忙碌。Indicates whether it is busy.
    LockFreeList subers;          ///< 订阅者列表。List of subscribers.
    uint32_t max_length;          ///< 数据的最大长度。Maximum length of data.
    uint32_t crc32;     ///< 主题名称的 CRC32 校验码。CRC32 checksum of the topic name.
    Mutex *mutex;       ///< 线程同步互斥锁。Mutex for thread synchronization.
    RawData data;       ///< 存储的数据。Stored data.
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
    uint8_t prefix;  ///< 数据包前缀（固定为 0xA5）。Packet prefix (fixed at 0xA5).
    uint32_t
        topic_name_crc32;  ///< 主题名称的 CRC32 校验码。CRC32 checksum of the topic name.
    uint8_t data_len_raw[3];   ///< 数据长度（最多 16MB）。Data length (up to 16MB).
    uint8_t pack_header_crc8;  ///< 头部 CRC8 校验码。CRC8 checksum of the header.

    void SetDataLen(uint32_t len);

    uint32_t GetDataLen() const;
  };
#pragma pack(pop)
#pragma pack(push, 1)
  /**
   * @class PackedData
   * @brief  主题数据包，包含数据和校验码
   *         Packed data structure containing data and checksum
   * @tparam Data 数据类型
   *         Type of the contained data
   * \addtogroup LibXR
   */
  template <typename Data>
  class PackedData
  {
   public:
#pragma pack(push, 1)
    /**
     * @struct raw
     * @brief 内部数据结构，包含数据包头和实际数据。Internal structure containing data
     * header and actual data.
     */
    struct
    {
      PackedDataHeader header_;     ///< 数据包头。Data packet header.
      uint8_t data_[sizeof(Data)];  ///< 主题数据。Topic data.
    } raw;

    uint8_t crc8_;  ///< 数据包的 CRC8 校验码。CRC8 checksum of the data packet.

#pragma pack(pop)

    /**
     * @brief 赋值运算符，设置数据并计算 CRC8 校验值。Assignment operator setting data and
     * computing CRC8 checksum.
     * @param data 要赋值的数据。Data to be assigned.
     * @return 赋值后的数据。The assigned data.
     */
    PackedData &operator=(const Data &data)
    {
      memcpy(raw.data_, &data, sizeof(Data));
      crc8_ = CRC8::Calculate(&raw, sizeof(raw));
      return *this;
    }

    /**
     * @brief 类型转换运算符，返回数据内容。Type conversion operator returning the data
     * content.
     */
    operator Data() { return *reinterpret_cast<Data *>(raw.data_); }

    /**
     * @brief 指针运算符，访问数据成员。Pointer operator for accessing data members.
     */
    Data *operator->() { return reinterpret_cast<Data *>(raw.data_); }

    /**
     * @brief 指针运算符，访问数据成员（常量版本）。Pointer operator for accessing data
     * members (const version).
     */
    const Data *operator->() const { return reinterpret_cast<const Data *>(raw.data_); }
  };
#pragma pack(pop)

  static constexpr size_t PACK_BASE_SIZE = sizeof(PackedData<uint8_t>) - 1;

#endif

  /**
   * @typedef TopicHandle
   * @brief 主题句柄，指向存储数据的红黑树节点。Handle pointing to a red-black tree node
   * storing data.
   */
  typedef RBTree<uint32_t>::Node<Block> *TopicHandle;

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
     */
    Domain(const char *name);

    /**
     * @brief 指向该域的根节点。Pointer to the root node of the domain.
     */
    RBTree<uint32_t>::Node<RBTree<uint32_t>> *node_;
  };

  /**
   * @enum SuberType
   * @brief 订阅者类型。Subscriber type.
   */
  enum class SuberType : uint8_t
  {
    SYNC,      ///< 同步订阅者。Synchronous subscriber.
    ASYNC,     ///< 异步订阅者。Asynchronous subscriber.
    QUEUE,     ///< 队列订阅者。Queued subscriber.
    CALLBACK,  ///< 回调订阅者。Callback subscriber.
  };

  /**
   * @struct SuberBlock
   * @brief 订阅者信息存储结构。Structure storing subscriber information.
   */
  struct SuberBlock
  {
    SuberType type;  ///< 订阅者类型。Type of subscriber.
  };

  /**
   * @struct SyncBlock
   * @brief 同步订阅者存储结构。Structure storing synchronous subscriber data.
   */
  struct SyncBlock : public SuberBlock
  {
    RawData buff;   ///< 存储的数据缓冲区。Data buffer.
    Semaphore sem;  ///< 信号量，用于同步等待数据。Semaphore for data synchronization.
  };

  /**
   * @class SyncSubscriber
   * @brief 同步订阅者类，允许同步方式接收数据。Synchronous subscriber class allowing data
   * reception in a synchronous manner.
   * @tparam Data 订阅的数据类型。Type of data being subscribed to.
   */
  template <typename Data>
  class SyncSubscriber
  {
   public:
    /**
     * @brief 通过主题名称构造同步订阅者。Constructs a synchronous subscriber by topic
     * name.
     * @param name 主题名称。Topic name.
     * @param data 存储接收数据的变量。Variable to store received data.
     * @param domain 可选的主题域。Optional topic domain.
     */
    SyncSubscriber(const char *name, Data &data, Domain *domain = nullptr)
    {
      *this = SyncSubscriber<Data>(WaitTopic(name, UINT32_MAX, domain), data);
    }

    /**
     * @brief 通过 `Topic` 句柄构造同步订阅者。Constructs a synchronous subscriber using a
     * `Topic` handle.
     * @param topic 订阅的主题。Topic being subscribed to.
     * @param data 存储接收数据的变量。Variable to store received data.
     */
    SyncSubscriber(Topic topic, Data &data)
    {
      if (topic.block_->data_.check_length)
      {
        ASSERT(topic.block_->data_.max_length == sizeof(Data));
      }
      else
      {
        ASSERT(topic.block_->data_.max_length <= sizeof(Data));
      }

      block_ = new LockFreeList::Node<SyncBlock>;
      block_->data_.type = SuberType::SYNC;
      block_->data_.buff = RawData(data);
      topic.block_->data_.subers.Add(*block_);
    }

    /**
     * @brief 等待接收数据。Waits for data reception.
     * @param timeout 超时时间（默认最大值）。Timeout period (default is maximum).
     * @return 操作结果的错误码。Error code indicating the operation result.
     */
    ErrorCode Wait(uint32_t timeout = UINT32_MAX)
    {
      // TODO: Reset sem
      return block_->data_.sem.Wait(timeout);
    }

    LockFreeList::Node<SyncBlock> *block_;  ///< 订阅者数据块。Subscriber data block.
  };

  enum class ASyncSubscriberState : uint32_t
  {
    IDLE = 0,
    WAITING = 1,
    DATA_READY = UINT32_MAX
  };

  /**
   * @struct ASyncBlock
   * @brief  异步订阅块，继承自 SuberBlock
   *         Asynchronous subscription block, inheriting from SuberBlock
   */
  typedef struct ASyncBlock : public SuberBlock
  {
    RawData buff;                             ///< 缓冲区数据 Buffer data
    std::atomic<ASyncSubscriberState> state;  ///< 订阅者状态 Subscriber state
  } ASyncBlock;

  /**
   * @class ASyncSubscriber
   * @brief  异步订阅者类，用于订阅异步数据
   *         Asynchronous subscriber class for subscribing to asynchronous data
   * @tparam Data 订阅的数据类型 Subscribed data type
   */
  template <typename Data>
  class ASyncSubscriber
  {
   public:
    /**
     * @brief  构造函数，通过名称和数据创建订阅者
     *         Constructor to create a subscriber with a name and data
     * @param  name 订阅的主题名称 Name of the subscribed topic
     * @param  domain 可选的域指针 Optional domain pointer (default: nullptr)
     */
    ASyncSubscriber(const char *name, Domain *domain = nullptr)
    {
      *this = ASyncSubscriber(WaitTopic(name, UINT32_MAX, domain));
    }

    /**
     * @brief  构造函数，使用 Topic 进行初始化
     *         Constructor using a Topic for initialization
     * @param  topic 订阅的主题 Subscribed topic
     */
    ASyncSubscriber(Topic topic)
    {
      if (topic.block_->data_.check_length)
      {
        ASSERT(topic.block_->data_.max_length == sizeof(Data));
      }
      else
      {
        ASSERT(topic.block_->data_.max_length <= sizeof(Data));
      }

      block_ = new LockFreeList::Node<ASyncBlock>;
      block_->data_.type = SuberType::ASYNC;
      block_->data_.buff = *(new Data);
      topic.block_->data_.subers.Add(*block_);
    }

    /**
     * @brief  检查数据是否可用
     *         Checks if data is available
     * @return true 如果数据已准备好，则返回 true
     *         true if data is ready
     * @return false 如果数据不可用，则返回 false
     *         false if data is not available
     */
    bool Available()
    {
      return block_->data_.state.load(std::memory_order_acquire) ==
             ASyncSubscriberState::DATA_READY;
    }

    /**
     * @brief  获取当前数据
     *         Retrieves the current data
     * @return Data& 返回数据引用
     *         Reference to the data
     */
    Data &GetData()
    {
      block_->data_.state.store(ASyncSubscriberState::IDLE, std::memory_order_release);
      return *reinterpret_cast<Data *>(block_->data_.buff.addr_);
    }

    /**
     * @brief  开始等待数据更新
     *         Starts waiting for data update
     */
    void StartWaiting()
    {
      block_->data_.state.store(ASyncSubscriberState::WAITING, std::memory_order_release);
    }

    LockFreeList::Node<ASyncBlock> *block_;  ///< 订阅者数据块。Subscriber data block.
  };

  /**
   * @struct QueueBlock
   * @brief  队列订阅块，继承自 SuberBlock
   *         Queue subscription block, inheriting from SuberBlock
   */
  typedef struct QueueBlock : public SuberBlock
  {
    void *queue;  ///< 指向订阅队列的指针 Pointer to the subscribed queue
    void (*fun)(RawData &, void *,
                bool);  ///< 处理数据的回调函数 Callback function to handle data
  } QueueBlock;

  /**
   * @brief  构造函数，使用名称和无锁队列进行初始化
   *         Constructor using a name and a lock-free queue
   * @tparam Data 队列存储的数据类型 Data type stored in the queue
   * @tparam Length 队列长度 Queue length
   * @param  name 订阅的主题名称 Name of the subscribed topic
   * @param  queue 订阅的数据队列 Subscribed data queue
   * @param  domain 可选的域指针 Optional domain pointer (default: nullptr)
   */
  class QueuedSubscriber
  {
   public:
    template <typename Data, uint32_t Length>
    QueuedSubscriber(const char *name, LockFreeQueue<Data> &queue,
                     Domain *domain = nullptr)
    {
      *this = QueuedSubscriber(WaitTopic(name, UINT32_MAX, domain), queue);
    }

    /**
     * @brief  构造函数，使用 Topic 和无锁队列进行初始化
     *         Constructor using a Topic and a lock-free queue
     * @tparam Data 队列存储的数据类型 Data type stored in the queue
     * @param  topic 订阅的主题 Subscribed topic
     * @param  queue 订阅的数据队列 Subscribed data queue
     */
    template <typename Data>
    QueuedSubscriber(Topic topic, LockFreeQueue<Data> &queue)
    {
      if (topic.block_->data_.check_length)
      {
        ASSERT(topic.block_->data_.max_length == sizeof(Data));
      }
      else
      {
        ASSERT(topic.block_->data_.max_length <= sizeof(Data));
      }

      auto block = new LockFreeList::Node<QueueBlock>;
      block->data_.type = SuberType::QUEUE;
      block->data_.queue = &queue;
      block->data_.fun = [](RawData &data, void *arg, bool in_isr)
      {
        UNUSED(in_isr);
        LockFreeQueue<Data> *queue = reinterpret_cast<LockFreeQueue<Data>>(arg);
        queue->Push(reinterpret_cast<Data>(data.addr_));
      };

      topic.block_->data_.subers.Add(*block);
    }
  };

  using Callback = LibXR::Callback<LibXR::RawData &>;

  /**
   * @struct CallbackBlock
   * @brief  回调订阅块，继承自 SuberBlock
   *         Callback subscription block, inheriting from SuberBlock
   */
  typedef struct CallbackBlock : public SuberBlock
  {
    Callback cb;  ///< 订阅的回调函数 Subscribed callback function
  } CallbackBlock;

  /**
   * @brief  注册回调函数
   *         Registers a callback function
   * @param  cb 需要注册的回调函数 The callback function to register
   */
  void RegisterCallback(Callback &cb);

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
   */
  Topic(const char *name, uint32_t max_length, Domain *domain = nullptr,
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
   * @param  check_length 是否检查数据长度（默认为 true）Whether to check data length
   * (default: true)
   * @return 创建的 Topic 实例 The created Topic instance
   */
  template <typename Data>
  static Topic CreateTopic(const char *name, Domain *domain = nullptr,
                           bool multi_publisher = false, bool cache = false,
                           bool check_length = true)
  {
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
   */
  static TopicHandle Find(const char *name, Domain *domain = nullptr);

  /**
   * @brief  在指定域中查找或创建主题
   *         Finds or creates a topic in the specified domain
   *
   * @tparam Data 数据类型 Data type
   * @param name 话题名称 Topic name
   * @param domain 可选的域指针 Optional domain pointer (default: nullptr)
   * @param cache  可选的缓存标志位 Optional cache flag (default: false)
   * @param check_length 可选的数据长度检查标志位 Optional data length check flag
   * (default: true)
   * @return TopicHandle 主题句柄 Topic handle
   */
  template <typename Data>
  static TopicHandle FindOrCreate(const char *name, Domain *domain = nullptr,
                                  bool cache = false, bool check_length = true)
  {
    auto topic = Find(name, domain);
    if (topic == nullptr)
    {
      topic = CreateTopic<Data>(name, domain, cache, check_length).block_;
    }
    return topic;
  }

  /**
   * @brief  启用主题的缓存功能
   *         Enables caching for the topic
   */
  void EnableCache();

  /**
   * @brief  发布数据
   *         Publishes data
   * @tparam Data 数据类型 Data type
   * @param  data 需要发布的数据 Data to be published
   */
  template <typename Data>
  void Publish(Data &data)
  {
    Publish(&data, sizeof(Data));
  }

  /**
   * @brief  以原始地址和大小发布数据
   *         Publishes data using raw address and size
   * @param  addr 数据的地址 Address of the data
   * @param  size 数据大小 Size of the data
   */
  void Publish(void *addr, uint32_t size);

  /**
   * @brief  在回调函数中发布数据
   *         Publishes data in a callback
   * @tparam Data 数据类型 Data type
   * @param  data 需要发布的数据 Data to be published
   * @param  in_isr 是否在中断中发布数据 Whether to publish data in an interrupt
   */
  template <typename Data>
  void PublishFromCallback(Data &data, bool in_isr)
  {
    PublishFromCallback(&data, sizeof(Data), in_isr);
  }

  /**
   * @brief  在回调函数中以原始地址和大小发布数据
   *         Publishes data using raw address and size in a callback
   * @param  addr 数据的地址 Address of the data
   * @param  size 数据大小 Size of the data
   * @param  in_isr 是否在中断中发布数据 Whether to publish data in an interrupt
   */
  void PublishFromCallback(void *addr, uint32_t size, bool in_isr);

  /**
   * @brief 转储数据
   *        Dump data
   *
   * @tparam Mode 数据大小检查模式 Size limit check mode
   * @param data  需要转储的数据 Data to be dumped
   * @param pack  是否打包数据 Pack data
   * @return ErrorCode
   */
  template <SizeLimitMode Mode = SizeLimitMode::MORE>
  ErrorCode DumpData(RawData data, bool pack = false)
  {
    if (block_->data_.data.addr_ == nullptr)
    {
      return ErrorCode::EMPTY;
    }

    if (!pack)
    {
      Assert::SizeLimitCheck<Mode>(block_->data_.data.size_, data.size_);
      Lock(block_);
      memcpy(data.addr_, block_->data_.data.addr_, block_->data_.data.size_);
      Unlock(block_);
    }
    else
    {
      Assert::SizeLimitCheck<Mode>(PACK_BASE_SIZE + block_->data_.data.size_, data.size_);

      Lock(block_);
      PackData(block_->data_.crc32, data, block_->data_.data);
      Unlock(block_);
    }

    return ErrorCode::OK;
  }

  /**
   * @brief 打包数据
   *
   * @param topic_name_crc32 话题名称的 CRC32 校验码
   * @param buffer 等待写入的包 Packed data to be written
   * @param source 需要打包的数据 Data to be packed
   */
  static void PackData(uint32_t topic_name_crc32, RawData buffer, RawData source);

  /**
   * @brief  转储数据到 PackedData
   *         Dumps data into PackedData format
   * @tparam Data 数据类型 Data type
   * @param  data 存储数据的 PackedData 结构 PackedData structure to store data
   */
  template <typename Data>
  ErrorCode DumpData(PackedData<Data> &data)
  {
    if (block_->data_.data.addr_ == nullptr)
    {
      return ErrorCode::EMPTY;
    }

    ASSERT(sizeof(Data) == block_->data_.data.size_);

    return DumpData<SizeLimitMode::NONE>(RawData(data), true);
  }

  /**
   * @brief  转储数据到普通数据结构
   *         Dumps data into a normal data structure
   * @tparam Data 数据类型 Data type
   * @param  data 存储数据的变量 Variable to store the data
   */
  template <typename Data>
  ErrorCode DumpData(Data &data)
  {
    if (block_->data_.data.addr_ == nullptr)
    {
      return ErrorCode::EMPTY;
    }

    ASSERT(sizeof(Data) == block_->data_.data.size_);

    return DumpData<SizeLimitMode::NONE>(data, false);
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
  static TopicHandle WaitTopic(const char *name, uint32_t timeout = UINT32_MAX,
                               Domain *domain = nullptr);

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

  /**
   * @class Server
   * @brief  服务器类，负责解析数据并将其分发到相应的主题
   *         Server class responsible for parsing data and distributing it to
   * corresponding topics
   */
  class Server
  {
   public:
    /**
     * @enum Status
     * @brief  服务器解析状态枚举
     *         Enumeration of server parsing states
     */
    enum class Status : uint8_t
    {
      WAIT_START,    ///< 等待起始标志 Waiting for start flag
      WAIT_TOPIC,    ///< 等待主题信息 Waiting for topic information
      WAIT_DATA_CRC  ///< 等待数据校验 Waiting for data CRC validation
    };

    /**
     * @brief  构造函数，初始化服务器并分配缓冲区
     *         Constructor to initialize the server and allocate buffer
     * @param  buffer_length 缓冲区长度 Buffer length
     */
    Server(size_t buffer_length);

    /**
     * @brief  注册一个主题
     *         Registers a topic
     * @param  topic 需要注册的主题句柄 The topic handle to register
     */
    void Register(TopicHandle topic);

    /**
     * @brief  解析接收到的数据
     *         Parses received data
     * @param  data 接收到的原始数据 Received raw data
     * @return 接收到的话题数量 Received topic count
     */
    size_t ParseData(ConstRawData data);

   private:
    Status status_ =
        Status::WAIT_START;  ///< 服务器的当前解析状态 Current parsing state of the server
    uint32_t data_len_ = 0;  ///< 当前数据长度 Current data length
    RBTree<uint32_t> topic_map_;           ///< 主题映射表 Topic mapping table
    BaseQueue queue_;                      ///< 数据队列 Data queue
    RawData parse_buff_;                   ///< 解析数据缓冲区 Data buffer for parsing
    TopicHandle current_topic_ = nullptr;  ///< 当前主题句柄 Current topic handle
  };

 private:
  /**
   * @brief  主题句柄，指向当前主题的内存块
   *         Topic handle pointing to the memory block of the current topic
   */
  TopicHandle block_ = nullptr;

  /**
   * @brief  主题域的红黑树结构，存储不同的主题
   *         Red-Black Tree structure for storing different topics in the domain
   */
  static inline RBTree<uint32_t> *domain_ = nullptr;

  /**
   * @brief  默认的主题域，所有未指定域的主题都会归入此域
   *         Default domain where all topics without a specified domain are assigned
   */
  static inline Domain *def_domain_ = nullptr;
};
}  // namespace LibXR
