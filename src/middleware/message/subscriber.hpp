#pragma once

#include "message.hpp"

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
  // WAIT_CLAIMED keeps a wakeup owned by the timed-out waiter until it consumes the
  // semaphore post; otherwise a new Wait() could steal that post.
  // WAIT_CLAIMED 表示本次唤醒已经归当前 waiter 所有，直到它消费 semaphore post。
  enum WaitState : uint32_t
  {
    WAIT_IDLE = 0,
    WAITING = 1,
    WAIT_CLAIMED = 2
  };

  RawData buff;                    ///< 存储的数据缓冲区。Data buffer.
  MicrosecondTimestamp timestamp;  ///< 最近接收的消息时间戳。Latest received timestamp.
  std::atomic<uint32_t> wait_state = WAIT_IDLE;  ///< 挂起等待状态。Pending wait state.
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
      : SyncSubscriber(Topic(WaitTopic(name, UINT32_MAX, domain)), data)
  {
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
    block_->data_.timestamp = MicrosecondTimestamp();
    block_->data_.wait_state.store(SyncBlock::WAIT_IDLE, std::memory_order_relaxed);
    block_->data_.buff = RawData(data);
    topic.block_->data_.subers.Add(*block_);
  }

  SyncSubscriber(const SyncSubscriber&) = delete;
  SyncSubscriber& operator=(const SyncSubscriber&) = delete;

  SyncSubscriber(SyncSubscriber&& other) noexcept : block_(other.block_)
  {
    other.block_ = nullptr;
  }

  SyncSubscriber& operator=(SyncSubscriber&& other) noexcept
  {
    if (this != &other)
    {
      block_ = other.block_;
      other.block_ = nullptr;
    }
    return *this;
  }

  /**
   * @brief 等待接收数据。Waits for data reception.
   * @param timeout 超时时间（默认最大值）。Timeout period (default is maximum).
   * @return 操作结果的错误码。Error code indicating the operation result.
   */
  ErrorCode Wait(uint32_t timeout = UINT32_MAX)
  {
    ASSERT(block_ != nullptr);

    auto& data = block_->data_;
    uint32_t expected = SyncBlock::WAIT_IDLE;
    if (!data.wait_state.compare_exchange_strong(expected, SyncBlock::WAITING,
                                                 std::memory_order_acq_rel,
                                                 std::memory_order_acquire))
    {
      return ErrorCode::BUSY;
    }

    auto wait_ans = data.sem.Wait(timeout);
    if (wait_ans == ErrorCode::OK)
    {
      data.wait_state.store(SyncBlock::WAIT_IDLE, std::memory_order_release);
      return ErrorCode::OK;
    }

    expected = SyncBlock::WAITING;
    if (data.wait_state.compare_exchange_strong(expected, SyncBlock::WAIT_IDLE,
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire))
    {
      return wait_ans;
    }

    ASSERT(data.wait_state.load(std::memory_order_acquire) == SyncBlock::WAIT_CLAIMED);

    auto finish_wait_ans = data.sem.Wait(UINT32_MAX);
    UNUSED(finish_wait_ans);
    ASSERT(finish_wait_ans == ErrorCode::OK);
    data.wait_state.store(SyncBlock::WAIT_IDLE, std::memory_order_release);
    return ErrorCode::OK;
  }

  MicrosecondTimestamp GetTimestamp() const { return block_->data_.timestamp; }

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
  RawData buff;                    ///< 缓冲区数据 Buffer data
  MicrosecondTimestamp timestamp;  ///< 最近接收的消息时间戳 Latest timestamp
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
      : ASyncSubscriber(Topic(WaitTopic(name, UINT32_MAX, domain)))
  {
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
    block_->data_.timestamp = MicrosecondTimestamp();
    block_->data_.buff = NewSubscriberBuffer<Data>();
    topic.block_->data_.subers.Add(*block_);
  }

  ASyncSubscriber(const ASyncSubscriber&) = delete;
  ASyncSubscriber& operator=(const ASyncSubscriber&) = delete;

  ASyncSubscriber(ASyncSubscriber&& other) noexcept : block_(other.block_)
  {
    other.block_ = nullptr;
  }

  ASyncSubscriber& operator=(ASyncSubscriber&& other) noexcept
  {
    if (this != &other)
    {
      block_ = other.block_;
      other.block_ = nullptr;
    }
    return *this;
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
    if (block_->data_.state.load(std::memory_order_acquire) ==
        ASyncSubscriberState::DATA_READY)
    {
      block_->data_.state.store(ASyncSubscriberState::IDLE, std::memory_order_release);
    }
    return *reinterpret_cast<Data*>(block_->data_.buff.addr_);
  }

  MicrosecondTimestamp GetTimestamp() const { return block_->data_.timestamp; }

  /**
   * @brief  开始等待数据更新
   *         Starts waiting for data update
   *
   * @note 异步订阅者只接收 WAITING 状态下的下一次发布；
   *       IDLE 或 DATA_READY 状态下的新发布会被忽略。
   *       Async subscribers only capture the next publish while in WAITING state;
   *       new publishes in IDLE or DATA_READY state are ignored.
   */
  void StartWaiting()
  {
    if (block_->data_.state.load(std::memory_order_acquire) ==
        ASyncSubscriberState::IDLE)
    {
      block_->data_.state.store(ASyncSubscriberState::WAITING,
                                std::memory_order_release);
    }
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
  void (*fun)(MicrosecondTimestamp, RawData,
              QueueBlock&);  ///< 处理消息的回调函数 Callback function to handle message
};

class Topic::QueuedSubscriber
{
 public:
  /**
   * @brief 构造函数，自动创建队列
   *
   * @tparam Data 队列存储的数据类型 Data type stored in the queue
   * @param name 订阅的主题名称 Name of the subscribed topic
   * @param queue 订阅的数据队列 Subscribed data queue
   * @param domain 可选的域指针 Optional domain pointer (default: nullptr)
   *
   * @note 包含初始化期动态内存分配，订阅者应长期存在。
   *       Contains initialization-time dynamic allocation; subscribers are expected to
   *       be long-lived.
   */
  template <typename Data>
  QueuedSubscriber(const char* name, LockFreeQueue<Data>& queue, Domain* domain = nullptr)
      : QueuedSubscriber(Topic(WaitTopic(name, UINT32_MAX, domain)), queue)
  {
  }

  /**
   * @brief  构造函数，使用 Topic 和无锁队列进行初始化
   *         Constructor using a Topic and a lock-free queue
   * @tparam Data 队列消息的数据类型 Data type stored in the queue message
   * @param name 订阅的主题名称 Name of the subscribed topic
   * @param queue 订阅的消息队列 Subscribed message queue
   * @param domain 可选的域指针 Optional domain pointer (default: nullptr)
   */
  template <typename Data>
  QueuedSubscriber(const char* name, LockFreeQueue<Message<Data>>& queue,
                   Domain* domain = nullptr)
      : QueuedSubscriber(Topic(WaitTopic(name, UINT32_MAX, domain)), queue)
  {
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

    block_ = new LockFreeList::Node<QueueBlock>;
    block_->data_.type = SuberType::QUEUE;
    block_->data_.queue = &queue;
    block_->data_.fun = [](MicrosecondTimestamp, RawData data, QueueBlock& block)
    {
      LockFreeQueue<Data>* queue = reinterpret_cast<LockFreeQueue<Data>*>(block.queue);
      (void)queue->Push(*reinterpret_cast<Data*>(data.addr_));
    };

    topic.block_->data_.subers.Add(*block_);
  }

  /**
   * @brief  构造函数，使用 Topic 和带时间戳消息队列进行初始化
   *         Constructor using a Topic and a timestamped message queue
   */
  template <typename Data>
  QueuedSubscriber(Topic topic, LockFreeQueue<Message<Data>>& queue)
  {
    CheckSubscriberDataSize<Data>(topic);

    block_ = new LockFreeList::Node<QueueBlock>;
    block_->data_.type = SuberType::QUEUE;
    block_->data_.queue = &queue;
    block_->data_.fun =
        [](MicrosecondTimestamp timestamp, RawData data, QueueBlock& block)
    {
      LockFreeQueue<Message<Data>>* queue =
          reinterpret_cast<LockFreeQueue<Message<Data>>*>(block.queue);
      (void)queue->Push(Message<Data>{timestamp, *reinterpret_cast<Data*>(data.addr_)});
    };

    topic.block_->data_.subers.Add(*block_);
  }

  QueuedSubscriber(const QueuedSubscriber&) = delete;
  QueuedSubscriber& operator=(const QueuedSubscriber&) = delete;

  QueuedSubscriber(QueuedSubscriber&& other) noexcept : block_(other.block_)
  {
    other.block_ = nullptr;
  }

  QueuedSubscriber& operator=(QueuedSubscriber&& other) noexcept
  {
    if (this != &other)
    {
      block_ = other.block_;
      other.block_ = nullptr;
    }
    return *this;
  }

 private:
  LockFreeList::Node<QueueBlock>* block_ = nullptr;
};

class Topic::Callback
{
  template <typename Function>
  struct FunctionTraits;

  template <typename Return, typename... Args>
  struct FunctionTraits<Return (*)(Args...)>
  {
    using ReturnType = Return;
    static constexpr size_t ARITY = sizeof...(Args);

    template <size_t Index>
    using Arg = std::tuple_element_t<Index, std::tuple<Args...>>;
  };

  template <typename T>
  struct MessageViewTraits
  {
    static constexpr bool VALUE = false;
  };

  template <typename Data>
  struct MessageViewTraits<MessageView<Data>>
  {
    static constexpr bool VALUE = true;
    using DataType = Data;
  };

  template <typename T>
  using RemoveCVRef = std::remove_cv_t<std::remove_reference_t<T>>;

  template <typename T>
  static constexpr bool IS_RAW_DATA =
      std::same_as<RemoveCVRef<T>, RawData> || std::same_as<RemoveCVRef<T>, ConstRawData>;

  template <typename T>
  static constexpr bool IS_MESSAGE_VIEW = MessageViewTraits<RemoveCVRef<T>>::VALUE;

  template <typename T>
  static constexpr bool IS_TYPED_DATA =
      !IS_RAW_DATA<T> && !IS_MESSAGE_VIEW<T> && TopicPayload<RemoveCVRef<T>>;

  template <typename T>
  static constexpr bool IS_CALLBACK_PAYLOAD =
      IS_RAW_DATA<T> || IS_MESSAGE_VIEW<T> || IS_TYPED_DATA<T>;

  struct BlockHeader
  {
    using RunFun = void (*)(const BlockHeader*, bool, MicrosecondTimestamp, RawData&);

    RunFun run = nullptr;
    uint32_t payload_size = 0;
  };

  template <typename PayloadArg>
  static consteval uint32_t PayloadSize()
  {
    if constexpr (IS_RAW_DATA<PayloadArg>)
    {
      return 0;
    }
    else if constexpr (IS_MESSAGE_VIEW<PayloadArg>)
    {
      using View = MessageViewTraits<RemoveCVRef<PayloadArg>>;
      return sizeof(typename View::DataType);
    }
    else
    {
      static_assert(IS_TYPED_DATA<PayloadArg>,
                    "LibXR::Topic::Callback payload must be RawData, ConstRawData, "
                    "Topic::MessageView<T>, T, T&, or const T&.");
      return sizeof(RemoveCVRef<PayloadArg>);
    }
  }

  template <typename Function, typename BoundArg, typename PayloadArg>
  static void InvokePayload(Function fun, BoundArg& arg, bool in_isr,
                            MicrosecondTimestamp timestamp, RawData& data)
  {
    if constexpr (std::same_as<RemoveCVRef<PayloadArg>, RawData>)
    {
      fun(in_isr, arg, data);
    }
    else if constexpr (std::same_as<RemoveCVRef<PayloadArg>, ConstRawData>)
    {
      ConstRawData const_data(data);
      fun(in_isr, arg, const_data);
    }
    else if constexpr (IS_MESSAGE_VIEW<PayloadArg>)
    {
      using View = MessageViewTraits<RemoveCVRef<PayloadArg>>;
      using Data = typename View::DataType;
      MessageView<Data> message{timestamp, *reinterpret_cast<Data*>(data.addr_)};
      fun(in_isr, arg, message);
    }
    else
    {
      using Data = RemoveCVRef<PayloadArg>;
      fun(in_isr, arg, *reinterpret_cast<Data*>(data.addr_));
    }
  }

  template <typename Function, typename BoundArg, typename PayloadArg>
  struct PayloadOnlyBlock : public BlockHeader
  {
    PayloadOnlyBlock(Function fun, BoundArg&& arg)
        : BlockHeader{&Run, PayloadSize<PayloadArg>()}, fun_(fun), arg_(std::move(arg))
    {
    }

    static void Run(const BlockHeader* header, bool in_isr,
                    MicrosecondTimestamp timestamp, RawData& data)
    {
      auto* block = static_cast<const PayloadOnlyBlock*>(header);
      InvokePayload<Function, BoundArg, PayloadArg>(
          block->fun_, const_cast<BoundArg&>(block->arg_), in_isr, timestamp, data);
    }

    Function fun_;
    BoundArg arg_;
  };

  template <typename Function, typename BoundArg, typename PayloadArg>
  struct TimestampPayloadBlock : public BlockHeader
  {
    TimestampPayloadBlock(Function fun, BoundArg&& arg)
        : BlockHeader{&Run, PayloadSize<PayloadArg>()}, fun_(fun), arg_(std::move(arg))
    {
    }

    static void Run(const BlockHeader* header, bool in_isr,
                    MicrosecondTimestamp timestamp, RawData& data)
    {
      auto* block = static_cast<const TimestampPayloadBlock*>(header);
      if constexpr (std::same_as<RemoveCVRef<PayloadArg>, RawData>)
      {
        block->fun_(in_isr, block->arg_, timestamp, data);
      }
      else if constexpr (std::same_as<RemoveCVRef<PayloadArg>, ConstRawData>)
      {
        ConstRawData const_data(data);
        block->fun_(in_isr, block->arg_, timestamp, const_data);
      }
      else if constexpr (IS_MESSAGE_VIEW<PayloadArg>)
      {
        using View = MessageViewTraits<RemoveCVRef<PayloadArg>>;
        using Data = typename View::DataType;
        MessageView<Data> message{timestamp, *reinterpret_cast<Data*>(data.addr_)};
        block->fun_(in_isr, block->arg_, timestamp, message);
      }
      else
      {
        using Data = RemoveCVRef<PayloadArg>;
        block->fun_(in_isr, block->arg_, timestamp, *reinterpret_cast<Data*>(data.addr_));
      }
    }

    Function fun_;
    BoundArg arg_;
  };

  template <typename Function, typename BoundArg, size_t Arity>
  struct Factory
  {
    static_assert(Arity == 3 || Arity == 4,
                  "LibXR::Topic::Callback function must be void(bool, Arg, Payload) "
                  "or void(bool, Arg, MicrosecondTimestamp, Payload).");
  };

  template <typename Function, typename BoundArg>
  struct Factory<Function, BoundArg, 3>
  {
    using Traits = FunctionTraits<Function>;
    using PayloadArg = typename Traits::template Arg<2>;

    static BlockHeader* Create(Function fun, BoundArg&& arg)
    {
      static_assert(std::same_as<typename Traits::ReturnType, void>);
      static_assert(std::same_as<typename Traits::template Arg<0>, bool>);
      static_assert(std::same_as<typename Traits::template Arg<1>, BoundArg>);
      static_assert(IS_CALLBACK_PAYLOAD<PayloadArg>);
      return new PayloadOnlyBlock<Function, BoundArg, PayloadArg>(fun, std::move(arg));
    }
  };

  template <typename Function, typename BoundArg>
  struct Factory<Function, BoundArg, 4>
  {
    using Traits = FunctionTraits<Function>;
    using TimestampArg = typename Traits::template Arg<2>;
    using PayloadArg = typename Traits::template Arg<3>;

    static BlockHeader* Create(Function fun, BoundArg&& arg)
    {
      static_assert(std::same_as<typename Traits::ReturnType, void>);
      static_assert(std::same_as<typename Traits::template Arg<0>, bool>);
      static_assert(std::same_as<typename Traits::template Arg<1>, BoundArg>);
      static_assert(std::same_as<RemoveCVRef<TimestampArg>, MicrosecondTimestamp>);
      static_assert(IS_CALLBACK_PAYLOAD<PayloadArg>);
      return new TimestampPayloadBlock<Function, BoundArg, PayloadArg>(fun,
                                                                       std::move(arg));
    }
  };

  static void EmptyRun(const BlockHeader*, bool, MicrosecondTimestamp, RawData&) {}

  inline static BlockHeader empty_block_{&EmptyRun, 0};

 public:
  Callback() = default;
  Callback(const Callback&) = default;
  Callback& operator=(const Callback&) = default;

  template <typename BoundArg, typename Callable>
  [[nodiscard]] static Callback Create(Callable fun, BoundArg arg)
  {
    using Function = decltype(+std::declval<Callable>());
    using Traits = FunctionTraits<Function>;
    static_assert(Traits::ARITY == 3 || Traits::ARITY == 4,
                  "LibXR::Topic::Callback::Create expects a capture-free callable "
                  "with bool and bound-argument parameters.");
    return Callback(
        Factory<Function, BoundArg, Traits::ARITY>::Create(+fun, std::move(arg)));
  }

  void Run(bool in_isr, MicrosecondTimestamp timestamp, RawData& data) const
  {
    block_->run(block_, in_isr, timestamp, data);
  }

  uint32_t PayloadSize() const { return block_->payload_size; }

 private:
  explicit Callback(BlockHeader* block) : block_(block ? block : &empty_block_) {}

  BlockHeader* block_ = &empty_block_;
};

/**
 * @struct Topic::CallbackBlock
 * @brief  回调订阅块，继承自 SuberBlock
 *         Callback subscription block, inheriting from SuberBlock
 */
struct Topic::CallbackBlock : public Topic::SuberBlock
{
  explicit CallbackBlock(Callback& callback) : cb(callback)
  {
    type = SuberType::CALLBACK;
  }

  Callback cb;  ///< 订阅的回调函数 Subscribed callback function
};
}  // namespace LibXR
