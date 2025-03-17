#pragma once

#include "crc.hpp"
#include "libxr_cb.hpp"
#include "libxr_def.hpp"
#include "libxr_type.hpp"
#include "list.hpp"
#include "lock_queue.hpp"
#include "lockfree_queue.hpp"
#include "mutex.hpp"
#include "queue.hpp"
#include "rbt.hpp"
#include "semaphore.hpp"
#include "spin_lock.hpp"
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
  /**
   * @struct Block
   * @brief 存储主题（Topic）数据的结构体。Structure storing topic data.
   */
 public:
  struct Block
  {
    uint32_t max_length;  ///< 数据的最大长度。Maximum length of data.
    uint32_t crc32;       ///< 主题名称的 CRC32 校验码。CRC32 checksum of the topic name.
    Mutex mutex;          ///< 线程同步互斥锁。Mutex for thread synchronization.
    RawData data;         ///< 存储的数据。Stored data.
    bool cache;         ///< 是否启用数据缓存。Indicates whether data caching is enabled.
    bool check_length;  ///< 是否检查数据长度。Indicates whether data length is checked.
    List subers;        ///< 订阅者列表。List of subscribers.
  };
#ifndef __DOXYGEN__
  /**
   * @struct PackedDataHeader
   * @brief 主题数据包头，用于网络传输。Packed data header for network transmission.
   */
  struct __attribute__((packed)) PackedDataHeader
  {
    uint8_t prefix;  ///< 数据包前缀（固定为 0xA5）。Packet prefix (fixed at 0xA5).
    uint32_t
        topic_name_crc32;  ///< 主题名称的 CRC32 校验码。CRC32 checksum of the topic name.
    uint32_t data_len : 24;    ///< 数据长度（最多 16MB）。Data length (up to 16MB).
    uint8_t pack_header_crc8;  ///< 头部 CRC8 校验码。CRC8 checksum of the header.
  };

  /**
   * @class PackedData
   * @brief  主题数据包，包含数据和校验码
   *         Packed data structure containing data and checksum
   * @tparam Data 数据类型
   *         Type of the contained data
   * \addtogroup LibXR
   */
  template <typename Data>
  class __attribute__((packed)) PackedData
  {
   public:
    /**
     * @struct raw
     * @brief 内部数据结构，包含数据包头和实际数据。Internal structure containing data
     * header and actual data.
     */

    struct __attribute__((packed))
    {
      PackedDataHeader header;  ///< 数据包头。Data packet header.
      Data data_;               ///< 主题数据。Topic data.
    } raw;

    uint8_t crc8_;  ///< 数据包的 CRC8 校验码。CRC8 checksum of the data packet.

    /**
     * @brief 赋值运算符，设置数据并计算 CRC8 校验值。Assignment operator setting data and
     * computing CRC8 checksum.
     * @param data 要赋值的数据。Data to be assigned.
     * @return 赋值后的数据。The assigned data.
     */
    const Data &operator=(const Data &data)
    {
      raw.data_ = data;
      crc8_ = CRC8::Calculate(&raw, sizeof(raw));
      return data;
    }

    /**
     * @brief 类型转换运算符，返回数据内容。Type conversion operator returning the data
     * content.
     */
    operator Data() { return raw.data_; }

    /**
     * @brief 指针运算符，访问数据成员。Pointer operator for accessing data members.
     */
    Data *operator->() { return &(raw.data_); }

    /**
     * @brief 指针运算符，访问数据成员（常量版本）。Pointer operator for accessing data
     * members (const version).
     */
    const Data *operator->() const { return &(raw.data_); }
  };

#endif

  /**
   * @typedef TopicHandle
   * @brief 主题句柄，指向存储数据的红黑树节点。Handle pointing to a red-black tree node
   * storing data.
   */
  typedef RBTree<uint32_t>::Node<Block> *TopicHandle;

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
    Domain(const char *name)
    {
      if (!domain_)
      {
        domain_lock_.Lock();
        if (!domain_)
        {
          domain_ =
              new RBTree<uint32_t>([](const uint32_t &a, const uint32_t &b)
                                   { return static_cast<int>(a) - static_cast<int>(b); });
        }
        domain_lock_.Unlock();
      }

      auto crc32 = CRC32::Calculate(name, strlen(name));

      auto domain = domain_->Search<RBTree<uint32_t>>(crc32);

      if (domain != nullptr)
      {
        node_ = domain;
        return;
      }

      node_ = new LibXR::RBTree<uint32_t>::Node<LibXR::RBTree<uint32_t>>(
          [](const uint32_t &a, const uint32_t &b)
          { return static_cast<int>(a) - static_cast<int>(b); });

      domain_->Insert(*node_, crc32);
    }

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
      *this = SyncSubscriber(WaitTopic(name, UINT32_MAX, domain), data);
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

      block_ = new List::Node<SyncBlock>;
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
      return block_->data_.sem.Wait(timeout);
    }

    List::Node<SyncBlock> *block_;  ///< 订阅者数据块。Subscriber data block.
  };

  /**
   * @struct ASyncBlock
   * @brief  异步订阅块，继承自 SuberBlock
   *         Asynchronous subscription block, inheriting from SuberBlock
   */
  typedef struct ASyncBlock : public SuberBlock
  {
    RawData buff;     ///< 缓冲区数据 Buffer data
    bool data_ready;  ///< 数据就绪标志 Data ready flag
    bool waiting;     ///< 等待中标志 Waiting flag
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
     * @param  data 订阅的数据对象 Subscribed data object
     * @param  domain 可选的域指针 Optional domain pointer (default: nullptr)
     */
    ASyncSubscriber(const char *name, Data &data, Domain *domain = nullptr)
    {
      *this = ASyncSubscriber(WaitTopic(name, UINT32_MAX, domain), data);
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

      block_ = new List::Node<ASyncBlock>;
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
    bool Available() { return block_->data_.data_ready; }

    /**
     * @brief  获取当前数据
     *         Retrieves the current data
     * @return Data& 返回数据引用
     *         Reference to the data
     */
    Data &GetData() { return block_->data_.buff; }

    /**
     * @brief  开始等待数据更新
     *         Starts waiting for data update
     */
    void StartWaiting() { block_->data_.waiting = true; }

    List::Node<ASyncBlock> *block_;  ///< 订阅者数据块。Subscriber data block.
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

      auto block = new List::Node<QueueBlock>;
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

    /**
     * @brief  构造函数，使用名称和带锁队列进行初始化
     *         Constructor using a name and a locked queue
     * @tparam Data 队列存储的数据类型 Data type stored in the queue
     * @param  name 订阅的主题名称 Name of the subscribed topic
     * @param  queue 订阅的数据队列 Subscribed data queue
     * @param  domain 可选的域指针 Optional domain pointer (default: nullptr)
     */
    template <typename Data>
    QueuedSubscriber(const char *name, LockQueue<Data> &queue, Domain *domain = nullptr)
    {
      *this = QueuedSubscriber(WaitTopic(name, UINT32_MAX, domain), queue);
    }

    /**
     * @brief  构造函数，使用 Topic 和带锁队列进行初始化
     *         Constructor using a Topic and a locked queue
     * @tparam Data 队列存储的数据类型 Data type stored in the queue
     * @param  topic 订阅的主题 Subscribed topic
     * @param  queue 订阅的数据队列 Subscribed data queue
     */
    template <typename Data>
    QueuedSubscriber(Topic topic, LockQueue<Data> &queue)
    {
      if (topic.block_->data_.check_length)
      {
        ASSERT(topic.block_->data_.max_length == sizeof(Data));
      }
      else
      {
        ASSERT(topic.block_->data_.max_length <= sizeof(Data));
      }

      auto block = new List::Node<QueueBlock>;
      block->data_.type = SuberType::QUEUE;
      block->data_.queue = &queue;
      block->data_.fun = [](RawData &data, void *arg, bool in_isr)
      {
        LockQueue<Data> *queue = reinterpret_cast<LockQueue<Data> *>(arg);
        queue->PushFromCallback(*reinterpret_cast<const Data *>(data.addr_), in_isr);
      };

      topic.block_->data_.subers.Add(*block);
    }
  };

  /**
   * @struct CallbackBlock
   * @brief  回调订阅块，继承自 SuberBlock
   *         Callback subscription block, inheriting from SuberBlock
   */
  typedef struct CallbackBlock : public SuberBlock
  {
    Callback<RawData &> cb;  ///< 订阅的回调函数 Subscribed callback function
  } CallbackBlock;

  /**
   * @brief  注册回调函数
   *         Registers a callback function
   * @param  cb 需要注册的回调函数 The callback function to register
   */
  void RegisterCallback(Callback<RawData &> &cb)
  {
    CallbackBlock block;
    block.cb = cb;
    block.type = SuberType::CALLBACK;
    auto node = new List::Node<CallbackBlock>(block);
    block_->data_.subers.Add(*node);
  }

  /**
   * @brief  默认构造函数，创建一个空的 Topic 实例
   *         Default constructor, creates an empty Topic instance
   */
  Topic() {}

  /**
   * @brief  构造函数，使用指定名称、最大长度、域及其他选项初始化主题
   *         Constructor to initialize a topic with the specified name, maximum length,
   * domain, and options
   * @param  name 主题名称 Topic name
   * @param  max_length 数据的最大长度 Maximum length of data
   * @param  domain 主题所属的域（默认为 nullptr）Domain to which the topic belongs
   * (default: nullptr)
   * @param  cache 是否启用缓存（默认为 false）Whether to enable caching (default: false)
   * @param  check_length 是否检查数据长度（默认为 false）Whether to check data length
   * (default: false)
   */
  Topic(const char *name, uint32_t max_length, Domain *domain = nullptr,
        bool cache = false, bool check_length = false)
  {
    if (!def_domain_)
    {
      domain_lock_.Lock();
      if (!domain_)
      {
        domain_ =
            new RBTree<uint32_t>([](const uint32_t &a, const uint32_t &b)
                                 { return static_cast<int>(a) - static_cast<int>(b); });
      }
      if (!def_domain_)
      {
        def_domain_ = new Domain("libxr_def_domain");
      }
      domain_lock_.Unlock();
    }

    if (domain == nullptr)
    {
      domain = def_domain_;
    }

    auto crc32 = CRC32::Calculate(name, strlen(name));

    auto topic = domain->node_->data_.Search<Block>(crc32);

    if (topic)
    {
      ASSERT(topic->data_.max_length == max_length);
      ASSERT(topic->data_.check_length == check_length);

      block_ = topic;
    }
    else
    {
      block_ = new RBTree<uint32_t>::Node<Block>;
      block_->data_.max_length = max_length;
      block_->data_.crc32 = crc32;
      block_->data_.data.addr_ = nullptr;
      block_->data_.cache = false;
      block_->data_.check_length = check_length;

      domain->node_->data_.Insert(*block_, crc32);
    }

    if (cache && !block_->data_.cache)
    {
      EnableCache();
    }
  }

  /**
   * @brief  创建一个新的主题
   *         Creates a new topic
   * @tparam Data 主题数据类型 Topic data type
   * @param  name 主题名称 Topic name
   * @param  domain 主题所属的域（默认为 nullptr）Domain to which the topic belongs
   * (default: nullptr)
   * @param  cache 是否启用缓存（默认为 false）Whether to enable caching (default: false)
   * @param  check_length 是否检查数据长度（默认为 true）Whether to check data length
   * (default: true)
   * @return 创建的 Topic 实例 The created Topic instance
   */
  template <typename Data>
  static Topic CreateTopic(const char *name, Domain *domain = nullptr, bool cache = false,
                           bool check_length = true)
  {
    return Topic(name, sizeof(Data), domain, cache, check_length);
  }

  /**
   * @brief  通过句柄构造主题
   *         Constructs a topic from a topic handle
   * @param  topic 主题句柄 Topic handle
   */
  Topic(TopicHandle topic) : block_(topic) {}

  /**
   * @brief  在指定域中查找主题
   *         Finds a topic in the specified domain
   * @param  name 主题名称 Topic name
   * @param  domain 主题所属的域（默认为 nullptr）Domain to search in (default: nullptr)
   * @return 主题句柄，如果找到则返回对应的句柄，否则返回 nullptr
   *         Topic handle if found, otherwise returns nullptr
   */
  static TopicHandle Find(const char *name, Domain *domain = nullptr)
  {
    if (domain == nullptr)
    {
      domain = def_domain_;
    }

    auto crc32 = CRC32::Calculate(name, strlen(name));

    return domain->node_->data_.Search<Block>(crc32);
  }

  /**
   * @brief  启用主题的缓存功能
   *         Enables caching for the topic
   */
  void EnableCache()
  {
    block_->data_.mutex.Lock();
    if (!block_->data_.cache)
    {
      block_->data_.cache = true;
      block_->data_.data.addr_ = new uint8_t[block_->data_.max_length];
    }
    block_->data_.mutex.Unlock();
  }

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
   * @brief  从回调函数发布数据
   *         Publishes data from a callback function
   * @tparam Data 数据类型 Data type
   * @param  data 需要发布的数据 Data to be published
   * @param  in_isr 是否在中断服务程序（ISR）中发布 Whether publishing inside an ISR
   */
  template <typename Data>
  void PublishFromCallback(Data &data, bool in_isr)
  {
    PublishFromCallback(&data, sizeof(Data), in_isr);
  }

  /**
   * @brief  以原始地址和大小发布数据
   *         Publishes data using raw address and size
   * @param  addr 数据的地址 Address of the data
   * @param  size 数据大小 Size of the data
   */
  void Publish(void *addr, uint32_t size)
  {
    block_->data_.mutex.Lock();
    if (block_->data_.check_length)
    {
      ASSERT(size == block_->data_.max_length);
    }
    else
    {
      ASSERT(size <= block_->data_.max_length);
    }

    if (block_->data_.cache)
    {
      memcpy(block_->data_.data.addr_, addr, size);
      block_->data_.data.size_ = size;
    }
    else
    {
      block_->data_.data.addr_ = addr;
      block_->data_.data.size_ = size;
    }

    RawData data = block_->data_.data;

    auto foreach_fun = [&](SuberBlock &block)
    {
      switch (block.type)
      {
        case SuberType::SYNC:
        {
          auto sync = reinterpret_cast<SyncBlock *>(&block);
          memcpy(sync->buff.addr_, data.addr_, data.size_);
          sync->sem.Post();
          break;
        }
        case SuberType::ASYNC:
        {
          auto async = reinterpret_cast<ASyncBlock *>(&block);
          if (async->waiting)
          {
            memcpy(async->buff.addr_, data.addr_, data.size_);
            async->data_ready = true;
          }
          break;
        }
        case SuberType::QUEUE:
        {
          auto queue_block = reinterpret_cast<QueueBlock *>(&block);
          queue_block->fun(data, queue_block->queue, false);
          break;
        }
        case SuberType::CALLBACK:
        {
          auto cb_block = reinterpret_cast<CallbackBlock *>(&block);
          cb_block->cb.Run(false, data);
          break;
        }
      }
      return ErrorCode::OK;
    };

    block_->data_.subers.Foreach<SuberBlock>(foreach_fun);

    block_->data_.mutex.Unlock();
  }

  /**
   * @brief  从回调函数发布数据
   *         Publishes data from a callback function
   * @param  addr 数据的地址 Address of the data
   * @param  size 数据大小 Size of the data
   * @param  in_isr 是否在中断服务程序（ISR）中发布 Whether publishing inside an ISR
   */
  void PublishFromCallback(void *addr, uint32_t size, bool in_isr)
  {
    if (block_->data_.mutex.TryLockInCallback(in_isr) != ErrorCode::OK)
    {
      return;
    }

    if (block_->data_.check_length)
    {
      ASSERT(size == block_->data_.max_length);
    }
    else
    {
      ASSERT(size <= block_->data_.max_length);
    }

    if (block_->data_.cache)
    {
      memcpy(block_->data_.data.addr_, addr, size);
      block_->data_.data.size_ = size;
    }
    else
    {
      block_->data_.data.addr_ = addr;
      block_->data_.data.size_ = size;
    }

    RawData data = block_->data_.data;

    auto foreach_fun = [&](SuberBlock &block)
    {
      switch (block.type)
      {
        case SuberType::SYNC:
        {
          auto sync = reinterpret_cast<SyncBlock *>(&block);
          memcpy(sync->buff.addr_, data.addr_, data.size_);
          sync->sem.PostFromCallback(in_isr);
          break;
        }
        case SuberType::ASYNC:
        {
          auto async = reinterpret_cast<ASyncBlock *>(&block);
          memcpy(async->buff.addr_, data.addr_, data.size_);
          async->data_ready = true;
          break;
        }
        case SuberType::QUEUE:
        {
          auto queue_block = reinterpret_cast<QueueBlock *>(&block);
          queue_block->fun(data, queue_block->queue, in_isr);
          break;
        }
        case SuberType::CALLBACK:
        {
          auto cb_block = reinterpret_cast<CallbackBlock *>(&block);
          cb_block->cb.Run(in_isr, data);
          break;
        }
      }
      return ErrorCode::OK;
    };

    block_->data_.subers.ForeachFromCallback<SuberBlock>(foreach_fun, in_isr);

    block_->data_.mutex.UnlockFromCallback(in_isr);
  }

  /**
   * @brief  转储数据到 PackedData
   *         Dumps data into PackedData format
   * @tparam Data 数据类型 Data type
   * @param  data 存储数据的 PackedData 结构 PackedData structure to store data
   */
  template <typename Data>
  void DumpData(PackedData<Data> &data)
  {
    if (block_->data_.data.addr_ != nullptr)
    {
      if (block_->data_.check_length)
      {
        ASSERT(sizeof(Data) == block_->data_.data.size_);
      }
      else
      {
        ASSERT(sizeof(Data) >= block_->data_.data.size_);
      }

      block_->data_.mutex.Lock();
      data = *reinterpret_cast<Data *>(block_->data_.data.addr_);
      block_->data_.mutex.Unlock();
      data.raw.header.prefix = 0xa5;
      data.raw.header.topic_name_crc32 = block_->data_.crc32;
      data.raw.header.data_len = block_->data_.data.size_;
      data.raw.header.pack_header_crc8 =
          CRC8::Calculate(&data, sizeof(PackedDataHeader) - sizeof(uint8_t));
      data.crc8_ = CRC8::Calculate(&data, sizeof(PackedData<Data>) - sizeof(uint8_t));
    }
  }

  /**
   * @brief  转储数据到普通数据结构
   *         Dumps data into a normal data structure
   * @tparam Data 数据类型 Data type
   * @param  data 存储数据的变量 Variable to store the data
   */
  template <typename Data>
  void DumpData(Data &data)
  {
    if (block_->data_.data.addr_ != nullptr)
    {
      if (block_->data_.check_length)
      {
        ASSERT(sizeof(Data) == block_->data_.data.size_);
      }
      else
      {
        ASSERT(sizeof(Data) >= block_->data_.data.size_);
      }
      block_->data_.mutex.Lock();
      data = *reinterpret_cast<Data *>(block_->data_.data.addr_);
      block_->data_.mutex.Unlock();
    }
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
                               Domain *domain = nullptr)
  {
    TopicHandle topic = nullptr;
    do
    {
      topic = Find(name, domain);
      if (topic == nullptr)
      {
        if (timeout <= Thread::GetTime())
        {
          return nullptr;
        }
        Thread::Sleep(1);
      }
    } while (topic == nullptr);

    return topic;
  }

  /**
   * @brief  将 Topic 转换为 TopicHandle
   *         Converts Topic to TopicHandle
   * @return TopicHandle
   */
  operator TopicHandle() { return block_; }

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
    Server(size_t buffer_length)
        : topic_map_([](const uint32_t &a, const uint32_t &b)
                     { return static_cast<int>(a) - static_cast<int>(b); }),
          queue_(1, buffer_length)
    {
      /* Minimum size: header8 + crc32 + length24 + crc8 + data +  crc8 = 10 */
      ASSERT(buffer_length >= sizeof(PackedData<uint8_t>));
      parse_buff_.size_ = buffer_length;
      parse_buff_.addr_ = new uint8_t[buffer_length];
    }

    /**
     * @brief  注册一个主题
     *         Registers a topic
     * @param  topic 需要注册的主题句柄 The topic handle to register
     */
    void Register(TopicHandle topic)
    {
      auto node = new RBTree<uint32_t>::Node<TopicHandle>(topic);
      topic_map_.Insert(*node, topic->key);
    }

    /**
     * @brief  解析接收到的数据
     *         Parses received data
     * @param  data 接收到的原始数据 Received raw data
     * @return ErrorCode 操作的错误码 Error code of the operation
     */
    ErrorCode ParseData(ConstRawData data)
    {
      auto raw = reinterpret_cast<const uint8_t *>(data.addr_);

      queue_.PushBatch(data.addr_, data.size_);

    check_start:
      /* 1. Check prefix */
      if (status_ == Status::WAIT_START)
      {
        /* Check start frame */
        auto queue_size = queue_.Size();
        for (uint32_t i = 0; i < queue_size; i++)
        {
          uint8_t prefix = 0;
          queue_.Peek(&prefix);
          if (prefix == 0xa5)
          {
            status_ = Status::WAIT_TOPIC;
            break;
          }
          queue_.Pop();
        }
        /* Not found */
        if (status_ == Status::WAIT_START)
        {
          return ErrorCode::NOT_FOUND;
        }
      }

      /* 2. Get topic info */
      if (status_ == Status::WAIT_TOPIC)
      {
        /* Check size&crc*/
        if (queue_.Size() >= sizeof(PackedDataHeader))
        {
          queue_.PeekBatch(parse_buff_.addr_, sizeof(PackedDataHeader));
          if (CRC8::Verify(parse_buff_.addr_, sizeof(PackedDataHeader)))
          {
            auto header = reinterpret_cast<PackedDataHeader *>(parse_buff_.addr_);
            /* Check buffer size */
            if (header->data_len >= queue_.EmptySize())
            {
              queue_.PopBatch(nullptr, sizeof(PackedDataHeader));
              status_ = Status::WAIT_START;
              goto check_start;  // NOLINT
            }

            /* Find topic */
            auto node = topic_map_.Search<TopicHandle>(header->topic_name_crc32);
            if (node)
            {
              data_len_ = header->data_len;
              current_topic_ = *node;
              status_ = Status::WAIT_DATA_CRC;
            }
            else
            {
              queue_.PopBatch(nullptr, sizeof(PackedDataHeader));
              status_ = Status::WAIT_START;
              goto check_start;  // NOLINT
            }
          }
          else
          {
            queue_.PopBatch(nullptr, sizeof(PackedDataHeader));
            status_ = Status::WAIT_START;
            goto check_start;  // NOLINT
          }
        }
        else
        {
          queue_.PushBatch(raw, data.size_);
          return ErrorCode::NOT_FOUND;
        }
      }

      if (status_ == Status::WAIT_DATA_CRC)
      {
        /* Check size&crc */
        if (queue_.Size() > data_len_ + sizeof(PackedDataHeader))
        {
          queue_.PopBatch(parse_buff_.addr_,
                          data_len_ + sizeof(PackedDataHeader) + sizeof(uint8_t));
          if (CRC8::Verify(parse_buff_.addr_,
                           data_len_ + sizeof(PackedDataHeader) + sizeof(uint8_t)))
          {
            status_ = Status::WAIT_START;
            auto data =
                reinterpret_cast<uint8_t *>(parse_buff_.addr_) + sizeof(PackedDataHeader);

            Topic(current_topic_).Publish(data, data_len_);

            goto check_start;  // NOLINT
          }
          else
          {
            queue_.PopBatch(nullptr,
                            data_len_ + sizeof(PackedDataHeader) + sizeof(uint8_t));
            goto check_start;  // NOLINT
          }
        }
        else
        {
          return ErrorCode::NOT_FOUND;
        }
      }

      return ErrorCode::FAILED;
    }

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
  static RBTree<uint32_t> *domain_;

  /**
   * @brief  主题域访问的自旋锁，确保多线程安全
   *         SpinLock for domain access to ensure thread safety
   */
  static SpinLock domain_lock_;

  /**
   * @brief  默认的主题域，所有未指定域的主题都会归入此域
   *         Default domain where all topics without a specified domain are assigned
   */
  static Domain *def_domain_;
};
}  // namespace LibXR
