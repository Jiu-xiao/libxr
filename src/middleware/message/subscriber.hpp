#pragma once

#include "message/message.hpp"

namespace LibXR
{
/**
 * @enum Topic::SuberType
 * @brief 订阅者类型。Subscriber type.
 */
enum class Topic::SuberType : uint8_t
{
  SYNC,      ///< 同步订阅者。Synchronous subscriber.
  ASYNC,     ///< 异步订阅者。Asynchronous subscriber.
  QUEUE,     ///< 队列订阅者。Queued subscriber.
  CALLBACK,  ///< 回调订阅者。Callback subscriber.
};

/**
 * @struct Topic::SuberBlock
 * @brief 订阅者信息存储结构。Structure storing subscriber information.
 */
struct Topic::SuberBlock
{
  SuberType type;  ///< 订阅者类型。Type of subscriber.
};

/**
 * @struct Topic::SyncBlock
 * @brief 同步订阅者存储结构。Structure storing synchronous subscriber data.
 */
struct Topic::SyncBlock : public SuberBlock
{
  RawData buff;   ///< 存储的数据缓冲区。Data buffer.
  Semaphore sem;  ///< 信号量，用于同步等待数据。Semaphore for data synchronization.
};

/**
 * @class Topic::SyncSubscriber
 * @brief 同步订阅者类，允许同步方式接收数据。Synchronous subscriber class allowing data
 * reception in a synchronous manner.
 * @tparam Data 订阅的数据类型。Type of data being subscribed to.
 */
template <typename Data>
class Topic::SyncSubscriber
{
 public:
  /**
   * @brief 通过主题名称构造同步订阅者。Constructs a synchronous subscriber by topic
   * name.
   * @param name 主题名称。Topic name.
   * @param data 存储接收数据的变量。Variable to store received data.
   * @param domain 可选的主题域。Optional topic domain.
   *
   * @note 包含初始化期动态内存分配，订阅者应长期存在。
   *       Contains initialization-time dynamic allocation; subscribers are expected to
   *       be long-lived.
   */
  SyncSubscriber(const char* name, Data& data, Domain* domain = nullptr)
  {
    *this = SyncSubscriber<Data>(WaitTopic(name, UINT32_MAX, domain), data);
  }

  /**
   * @brief 通过 `Topic` 句柄构造同步订阅者。Constructs a synchronous subscriber using a
   * `Topic` handle.
   * @param topic 订阅的主题。Topic being subscribed to.
   * @param data 存储接收数据的变量。Variable to store received data.
   *
   * @note 包含初始化期动态内存分配，订阅者应长期存在。
   *       Contains initialization-time dynamic allocation; subscribers are expected to
   *       be long-lived.
   */
  SyncSubscriber(Topic topic, Data& data)
  {
    CheckSubscriberDataSize<Data>(topic);

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

  LockFreeList::Node<SyncBlock>* block_;  ///< 订阅者数据块。Subscriber data block.
};

enum class Topic::ASyncSubscriberState : uint32_t
{
  IDLE = 0,
  WAITING = 1,
  DATA_READY = UINT32_MAX
};

/**
 * @struct Topic::ASyncBlock
 * @brief  异步订阅块，继承自 SuberBlock
 *         Asynchronous subscription block, inheriting from SuberBlock
 */
struct Topic::ASyncBlock : public Topic::SuberBlock
{
  RawData buff;                             ///< 缓冲区数据 Buffer data
  std::atomic<ASyncSubscriberState> state =
      ASyncSubscriberState::IDLE;  ///< 订阅者状态 Subscriber state
};

/**
 * @class Topic::ASyncSubscriber
 * @brief  异步订阅者类，用于订阅异步数据
 *         Asynchronous subscriber class for subscribing to asynchronous data
 * @tparam Data 订阅的数据类型 Subscribed data type
 */
template <typename Data>
class Topic::ASyncSubscriber
{
 public:
  /**
   * @brief  构造函数，通过名称和数据创建订阅者
   *         Constructor to create a subscriber with a name and data
   * @param  name 订阅的主题名称 Name of the subscribed topic
   * @param  domain 可选的域指针 Optional domain pointer (default: nullptr)
   *
   * @note 包含初始化期动态内存分配，订阅者应长期存在。
   *       Contains initialization-time dynamic allocation; subscribers are expected to
   *       be long-lived.
   */
  ASyncSubscriber(const char* name, Domain* domain = nullptr)
  {
    *this = ASyncSubscriber(WaitTopic(name, UINT32_MAX, domain));
  }

  /**
   * @brief  构造函数，使用 Topic 进行初始化
   *         Constructor using a Topic for initialization
   * @param  topic 订阅的主题 Subscribed topic
   *
   * @note 包含初始化期动态内存分配，订阅者应长期存在。
   *       Contains initialization-time dynamic allocation; subscribers are expected to
   *       be long-lived.
   */
  ASyncSubscriber(Topic topic)
  {
    CheckSubscriberDataSize<Data>(topic);

    block_ = new LockFreeList::Node<ASyncBlock>;
    block_->data_.type = SuberType::ASYNC;
    block_->data_.buff = NewSubscriberBuffer<Data>();
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
  Data& GetData()
  {
    block_->data_.state.store(ASyncSubscriberState::IDLE, std::memory_order_release);
    return *reinterpret_cast<Data*>(block_->data_.buff.addr_);
  }

  /**
   * @brief  开始等待数据更新
   *         Starts waiting for data update
   *
   * @note 异步订阅者只接收 WAITING 状态下的下一次发布；IDLE 或 DATA_READY 状态下的新发布会被忽略。
   *       Async subscribers only capture the next publish while in WAITING state; new
   *       publishes in IDLE or DATA_READY state are ignored.
   */
  void StartWaiting()
  {
    block_->data_.state.store(ASyncSubscriberState::WAITING, std::memory_order_release);
  }

  LockFreeList::Node<ASyncBlock>* block_;  ///< 订阅者数据块。Subscriber data block.
};

/**
 * @struct Topic::QueueBlock
 * @brief  队列订阅块，继承自 SuberBlock
 *         Queue subscription block, inheriting from SuberBlock
 */
struct Topic::QueueBlock : public Topic::SuberBlock
{
  void* queue;  ///< 指向订阅队列的指针 Pointer to the subscribed queue
  void (*fun)(RawData&, void*);  ///< 处理数据的回调函数 Callback function to handle data
};

class Topic::QueuedSubscriber
{
 public:
  /**
   * @brief 构造函数，自动创建队列
   *
   * @tparam Data 队列存储的数据类型 Data type stored in the queue
   * @tparam Length 队列长度 Queue length
   * @param name 订阅的主题名称 Name of the subscribed topic
   * @param queue 订阅的数据队列 Subscribed data queue
   * @param domain 可选的域指针 Optional domain pointer (default: nullptr)
   *
   * @note 包含初始化期动态内存分配，订阅者应长期存在。
   *       Contains initialization-time dynamic allocation; subscribers are expected to
   *       be long-lived.
   */
  template <typename Data, uint32_t Length>
  QueuedSubscriber(const char* name, LockFreeQueue<Data>& queue,
                   Domain* domain = nullptr)
  {
    *this = QueuedSubscriber(WaitTopic(name, UINT32_MAX, domain), queue);
  }

  /**
   * @brief  构造函数，使用 Topic 和无锁队列进行初始化
   *         Constructor using a Topic and a lock-free queue
   * @tparam Data 队列存储的数据类型 Data type stored in the queue
   * @param  topic 订阅的主题 Subscribed topic
   * @param  queue 订阅的数据队列 Subscribed data queue
   *
   * @note 包含初始化期动态内存分配，订阅者应长期存在。
   *       Contains initialization-time dynamic allocation; subscribers are expected to
   *       be long-lived.
   */
  template <typename Data>
  QueuedSubscriber(Topic topic, LockFreeQueue<Data>& queue)
  {
    CheckSubscriberDataSize<Data>(topic);

    auto block = new LockFreeList::Node<QueueBlock>;
    block->data_.type = SuberType::QUEUE;
    block->data_.queue = &queue;
    block->data_.fun = [](RawData& data, void* arg)
    {
      LockFreeQueue<Data>* queue = reinterpret_cast<LockFreeQueue<Data>*>(arg);
      /* Keep the queue subscriber policy non-blocking: FULL means this sample is
       * dropped and later subscribers are still dispatched. */
      queue->Push(*reinterpret_cast<Data*>(data.addr_));
    };

    topic.block_->data_.subers.Add(*block);
  }
};

/**
 * @struct Topic::CallbackBlock
 * @brief  回调订阅块，继承自 SuberBlock
 *         Callback subscription block, inheriting from SuberBlock
 */
struct Topic::CallbackBlock : public Topic::SuberBlock
{
  Callback cb;  ///< 订阅的回调函数 Subscribed callback function
};
}  // namespace LibXR
