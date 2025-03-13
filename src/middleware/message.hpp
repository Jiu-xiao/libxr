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

namespace LibXR {
class Topic {
 public:
  typedef struct {
    uint32_t max_length;
    uint32_t crc32;
    Mutex mutex;
    RawData data;
    bool cache;
    bool check_length;
    List subers;
  } Block;

  typedef struct __attribute__((packed)) {
    uint8_t prefix;
    uint32_t topic_name_crc32;
    uint32_t data_len : 24;
    uint8_t pack_header_crc8;
  } PackedDataHeader;

  template <typename Data>
  class __attribute__((packed)) PackedData {
   public:
    struct __attribute__((packed)) {
      PackedDataHeader header;
      Data data_;
    } raw;

    uint8_t crc8_;

    const Data &operator=(const Data &data) {  // NOLINT
      raw.data_ = data;
      crc8_ = CRC8::Calculate(&raw, sizeof(raw));
      return data;  // NOLINT
    }

    operator Data() { return raw.data_; }

    Data *operator->() { return &(raw.data_); }

    const Data *operator->() const { return &(raw.data_); }
  };

  typedef RBTree<uint32_t>::Node<Block> *TopicHandle;

  class Domain {
   public:
    Domain(const char *name) {
      if (!domain_) {
        domain_lock_.Lock();
        if (!domain_) {
          domain_ =
              new RBTree<uint32_t>([](const uint32_t &a, const uint32_t &b) {
                return static_cast<int>(a) - static_cast<int>(b);
              });
        }
        domain_lock_.Unlock();
      }

      auto crc32 = CRC32::Calculate(name, strlen(name));

      auto domain = domain_->Search<RBTree<uint32_t>>(crc32);

      if (domain != nullptr) {
        node_ = domain;
        return;
      }

      node_ = new LibXR::RBTree<uint32_t>::Node<LibXR::RBTree<uint32_t>>(
          [](const uint32_t &a, const uint32_t &b) {
            return static_cast<int>(a) - static_cast<int>(b);
          });

      domain_->Insert(*node_, crc32);
    }

    RBTree<uint32_t>::Node<RBTree<uint32_t>> *node_;
  };

  enum class SuberType : uint8_t {
    SYNC,
    ASYNC,
    QUEUE,
    CALLBACK,
  };

  typedef struct {
    SuberType type;
  } SuberBlock;

  typedef struct SyncBlock : public SuberBlock {
    RawData buff;
    Semaphore sem;
  } SyncBlock;

  template <typename Data>
  class SyncSubscriber {
   public:
    SyncSubscriber(const char *name, Data &data, Domain *domain = nullptr) {
      *this = SyncSubscriber(WaitTopic(name, UINT32_MAX, domain), data);
    }

    SyncSubscriber(Topic topic, Data &data) {
      if (topic.block_->data_.check_length) {
        ASSERT(topic.block_->data_.max_length == sizeof(Data));
      } else {
        ASSERT(topic.block_->data_.max_length <= sizeof(Data));
      }

      block_ = new List::Node<SyncBlock>;
      block_->data_.type = SuberType::SYNC;
      block_->data_.buff = RawData(data);
      topic.block_->data_.subers.Add(*block_);
    }

    ErrorCode Wait(uint32_t timeout = UINT32_MAX) {
      return block_->data_.sem.Wait(timeout);
    }

    List::Node<SyncBlock> *block_;
  };

  typedef struct ASyncBlock : public SuberBlock {
    RawData buff;
    bool data_ready;
  } ASyncBlock;

  template <typename Data>
  class ASyncSubscriber {
   public:
    ASyncSubscriber(const char *name, Data &data, Domain *domain = nullptr) {
      *this = ASyncSubscriber(WaitTopic(name, UINT32_MAX, domain), data);
    }

    ASyncSubscriber(Topic topic) {
      if (topic.block_->data_.check_length) {
        ASSERT(topic.block_->data_.max_length == sizeof(Data));
      } else {
        ASSERT(topic.block_->data_.max_length <= sizeof(Data));
      }

      block_ = new List::Node<SyncBlock>;
      block_->data_.type = SuberType::ASYNC;
      block_->data_.buff = *(new Data);
      topic.block_->data_.subers.Add(*block_);
    }

    ErrorCode Wait(uint32_t timeout = UINT32_MAX) {
      return block_->data_.sem.Wait(timeout);
    }

    List::Node<SyncBlock> *block_;
  };

  typedef struct QueueBlock : public SuberBlock {
    void *queue;
    void (*fun)(RawData &, void *, bool);
  } QueueBlock;

  class QueuedSubscriber {
   public:
    template <typename Data, uint32_t Length>
    QueuedSubscriber(const char *name, LockFreeQueue<Data> &queue,
                     Domain *domain = nullptr) {
      *this = QueuedSubscriber(WaitTopic(name, UINT32_MAX, domain), queue);
    }

    template <typename Data>
    QueuedSubscriber(Topic topic, LockFreeQueue<Data> &queue) {
      if (topic.block_->data_.check_length) {
        ASSERT(topic.block_->data_.max_length == sizeof(Data));
      } else {
        ASSERT(topic.block_->data_.max_length <= sizeof(Data));
      }

      auto block = new List::Node<QueueBlock>;
      block->data_.type = SuberType::QUEUE;
      block->data_.queue = &queue;
      block->data_.fun = [](RawData &data, void *arg, bool in_isr) {
        UNUSED(in_isr);
        LockFreeQueue<Data> *queue = reinterpret_cast<LockFreeQueue<Data>>(arg);
        queue->Push(reinterpret_cast<Data>(data.addr_));
      };

      topic.block_->data_.subers.Add(*block);
    }

    template <typename Data>
    QueuedSubscriber(const char *name, LockQueue<Data> &queue,
                     Domain *domain = nullptr) {
      *this = QueuedSubscriber(WaitTopic(name, UINT32_MAX, domain), queue);
    }

    template <typename Data>
    QueuedSubscriber(Topic topic, LockQueue<Data> &queue) {
      if (topic.block_->data_.check_length) {
        ASSERT(topic.block_->data_.max_length == sizeof(Data));
      } else {
        ASSERT(topic.block_->data_.max_length <= sizeof(Data));
      }

      auto block = new List::Node<QueueBlock>;
      block->data_.type = SuberType::QUEUE;
      block->data_.queue = &queue;
      block->data_.fun = [](RawData &data, void *arg, bool in_isr) {
        LockQueue<Data> *queue = reinterpret_cast<LockQueue<Data> *>(arg);
        queue->PushFromCallback(*reinterpret_cast<const Data *>(data.addr_),
                                in_isr);
      };

      topic.block_->data_.subers.Add(*block);
    }
  };

  typedef struct CallbackBlock : public SuberBlock {
    Callback<RawData &> cb;
  } CallbackBlock;

  void RegisterCallback(Callback<RawData &> &cb) {
    CallbackBlock block;
    block.cb = cb;
    block.type = SuberType::CALLBACK;
    auto node = new List::Node<CallbackBlock>(block);
    block_->data_.subers.Add(*node);
  }

  Topic() {}

  Topic(const char *name, uint32_t max_length, Domain *domain = nullptr,
        bool cache = false, bool check_length = false) {
    if (!def_domain_) {
      domain_lock_.Lock();
      if (!domain_) {
        domain_ =
            new RBTree<uint32_t>([](const uint32_t &a, const uint32_t &b) {
              return static_cast<int>(a) - static_cast<int>(b);
            });
      }
      if (!def_domain_) {
        def_domain_ = new Domain("libxr_def_domain");
      }
      domain_lock_.Unlock();
    }

    if (domain == nullptr) {
      domain = def_domain_;
    }

    auto crc32 = CRC32::Calculate(name, strlen(name));

    auto topic = domain->node_->data_.Search<Block>(crc32);

    if (topic) {
      ASSERT(topic->data_.max_length == max_length);
      ASSERT(topic->data_.check_length == check_length);

      block_ = topic;
    } else {
      block_ = new RBTree<uint32_t>::Node<Block>;
      block_->data_.max_length = max_length;
      block_->data_.crc32 = crc32;
      block_->data_.data.addr_ = nullptr;
      block_->data_.cache = false;
      block_->data_.check_length = check_length;

      domain->node_->data_.Insert(*block_, crc32);
    }

    if (cache && !block_->data_.cache) {
      EnableCache();
    }
  }

  template <typename Data>
  static Topic CreateTopic(const char *name, Domain *domain = nullptr,
                           bool cache = false, bool check_length = true) {
    return Topic(name, sizeof(Data), domain, cache, check_length);
  }

  Topic(TopicHandle topic) : block_(topic) {}

  static TopicHandle Find(const char *name, Domain *domain = nullptr) {
    if (domain == nullptr) {
      domain = def_domain_;
    }

    auto crc32 = CRC32::Calculate(name, strlen(name));

    return domain->node_->data_.Search<Block>(crc32);
  }

  void EnableCache() {
    block_->data_.mutex.Lock();
    if (!block_->data_.cache) {
      block_->data_.cache = true;
      block_->data_.data.addr_ = new uint8_t[block_->data_.max_length];
    }
    block_->data_.mutex.Unlock();
  }
  template <typename Data>
  void Publish(Data &data) {
    Publish(&data, sizeof(Data));
  }

  template <typename Data>
  void PublishFromCallback(Data &data, bool in_isr) {
    PublishFromCallback(&data, sizeof(Data), in_isr);
  }

  void Publish(void *addr, uint32_t size) {
    block_->data_.mutex.Lock();
    if (block_->data_.check_length) {
      ASSERT(size == block_->data_.max_length);
    } else {
      ASSERT(size <= block_->data_.max_length);
    }

    if (block_->data_.cache) {
      memcpy(block_->data_.data.addr_, addr, size);
      block_->data_.data.size_ = size;
    } else {
      block_->data_.data.addr_ = addr;
      block_->data_.data.size_ = size;
    }

    RawData data = block_->data_.data;

    auto foreach_fun = [&](SuberBlock &block) {
      switch (block.type) {
        case SuberType::SYNC: {
          auto sync = reinterpret_cast<SyncBlock *>(&block);
          memcpy(sync->buff.addr_, data.addr_, data.size_);
          sync->sem.Post();
          break;
        }
        case SuberType::ASYNC: {
          auto async = reinterpret_cast<ASyncBlock *>(&block);
          memcpy(async->buff.addr_, data.addr_, data.size_);
          async->data_ready = true;
          break;
        }
        case SuberType::QUEUE: {
          auto queue_block = reinterpret_cast<QueueBlock *>(&block);
          queue_block->fun(data, queue_block->queue, false);
          break;
        }
        case SuberType::CALLBACK: {
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

  void PublishFromCallback(void *addr, uint32_t size, bool in_isr) {
    if (block_->data_.mutex.TryLockInCallback(in_isr) != ErrorCode::OK) {
      return;
    }

    if (block_->data_.check_length) {
      ASSERT(size == block_->data_.max_length);
    } else {
      ASSERT(size <= block_->data_.max_length);
    }

    if (block_->data_.cache) {
      memcpy(block_->data_.data.addr_, addr, size);
      block_->data_.data.size_ = size;
    } else {
      block_->data_.data.addr_ = addr;
      block_->data_.data.size_ = size;
    }

    RawData data = block_->data_.data;

    auto foreach_fun = [&](SuberBlock &block) {
      switch (block.type) {
        case SuberType::SYNC: {
          auto sync = reinterpret_cast<SyncBlock *>(&block);
          memcpy(sync->buff.addr_, data.addr_, data.size_);
          sync->sem.PostFromCallback(in_isr);
          break;
        }
        case SuberType::ASYNC: {
          auto async = reinterpret_cast<ASyncBlock *>(&block);
          memcpy(async->buff.addr_, data.addr_, data.size_);
          async->data_ready = true;
          break;
        }
        case SuberType::QUEUE: {
          auto queue_block = reinterpret_cast<QueueBlock *>(&block);
          queue_block->fun(data, queue_block->queue, in_isr);
          break;
        }
        case SuberType::CALLBACK: {
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

  template <typename Data>
  void DumpData(PackedData<Data> &data) {
    if (block_->data_.data.addr_ != nullptr) {
      if (block_->data_.check_length) {
        ASSERT(sizeof(Data) == block_->data_.data.size_);
      } else {
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
      data.crc8_ =
          CRC8::Calculate(&data, sizeof(PackedData<Data>) - sizeof(uint8_t));
    }
  }

  template <typename Data>
  void DumpData(Data &data) {
    if (block_->data_.data.addr_ != nullptr) {
      if (block_->data_.check_length) {
        ASSERT(sizeof(Data) == block_->data_.data.size_);
      } else {
        ASSERT(sizeof(Data) >= block_->data_.data.size_);
      }
      block_->data_.mutex.Lock();
      data = *reinterpret_cast<Data *>(block_->data_.data.addr_);
      block_->data_.mutex.Unlock();
    }
  }

  static TopicHandle WaitTopic(const char *name, uint32_t timeout = UINT32_MAX,
                               Domain *domain = nullptr) {
    TopicHandle topic = nullptr;
    do {
      topic = Find(name, domain);
      if (topic == nullptr) {
        if (timeout <= Thread::GetTime()) {
          return nullptr;
        }
        Thread::Sleep(1);
      }
    } while (topic == nullptr);

    return topic;
  }

  operator TopicHandle() { return block_; }

  class Server {
   public:
    enum class Status : uint8_t { WAIT_START, WAIT_TOPIC, WAIT_DATA_CRC };

    Server(size_t buffer_length)
        : topic_map_([](const uint32_t &a, const uint32_t &b) {
            return static_cast<int>(a) - static_cast<int>(b);
          }),
          queue_(1, buffer_length) {
      /* Minimum size: header8 + crc32 + length24 + crc8 + data +  crc8 = 10 */
      ASSERT(buffer_length >= sizeof(PackedData<uint8_t>));
      parse_buff_.size_ = buffer_length;
      parse_buff_.addr_ = new uint8_t[buffer_length];
    }

    void Register(TopicHandle topic) {
      auto node = new RBTree<uint32_t>::Node<TopicHandle>(topic);
      topic_map_.Insert(*node, topic->key);
    }

    ErrorCode ParseData(ConstRawData data) {
      auto raw = reinterpret_cast<const uint8_t *>(data.addr_);

      queue_.PushBatch(data.addr_, data.size_);

    check_start:
      /* 1. Check prefix */
      if (status_ == Status::WAIT_START) {
        /* Check start frame */
        auto queue_size = queue_.Size();
        for (uint32_t i = 0; i < queue_size; i++) {
          uint8_t prefix = 0;
          queue_.Peek(&prefix);
          if (prefix == 0xa5) {
            status_ = Status::WAIT_TOPIC;
            break;
          }
          queue_.Pop();
        }
        /* Not found */
        if (status_ == Status::WAIT_START) {
          return ErrorCode::NOT_FOUND;
        }
      }

      /* 2. Get topic info */
      if (status_ == Status::WAIT_TOPIC) {
        /* Check size&crc*/
        if (queue_.Size() >= sizeof(PackedDataHeader)) {
          queue_.PeekBatch(parse_buff_.addr_, sizeof(PackedDataHeader));
          if (CRC8::Verify(parse_buff_.addr_, sizeof(PackedDataHeader))) {
            auto header =
                reinterpret_cast<PackedDataHeader *>(parse_buff_.addr_);
            /* Check buffer size */
            if (header->data_len >= queue_.EmptySize()) {
              queue_.PopBatch(nullptr, sizeof(PackedDataHeader));
              status_ = Status::WAIT_START;
              goto check_start;  // NOLINT
            }

            /* Find topic */
            auto node =
                topic_map_.Search<TopicHandle>(header->topic_name_crc32);
            if (node) {
              data_len_ = header->data_len;
              current_topic_ = *node;
              status_ = Status::WAIT_DATA_CRC;
            } else {
              queue_.PopBatch(nullptr, sizeof(PackedDataHeader));
              status_ = Status::WAIT_START;
              goto check_start;  // NOLINT
            }
          } else {
            queue_.PopBatch(nullptr, sizeof(PackedDataHeader));
            status_ = Status::WAIT_START;
            goto check_start;  // NOLINT
          }
        } else {
          queue_.PushBatch(raw, data.size_);
          return ErrorCode::NOT_FOUND;
        }
      }

      if (status_ == Status::WAIT_DATA_CRC) {
        /* Check size&crc */
        if (queue_.Size() > data_len_ + sizeof(PackedDataHeader)) {
          queue_.PopBatch(
              parse_buff_.addr_,
              data_len_ + sizeof(PackedDataHeader) + sizeof(uint8_t));
          if (CRC8::Verify(
                  parse_buff_.addr_,
                  data_len_ + sizeof(PackedDataHeader) + sizeof(uint8_t))) {
            status_ = Status::WAIT_START;
            auto data = reinterpret_cast<uint8_t *>(parse_buff_.addr_) +
                        sizeof(PackedDataHeader);

            Topic(current_topic_).Publish(data, data_len_);

            goto check_start;  // NOLINT
          } else {
            queue_.PopBatch(nullptr, data_len_ + sizeof(PackedDataHeader) +
                                         sizeof(uint8_t));
            goto check_start;  // NOLINT
          }
        } else {
          return ErrorCode::NOT_FOUND;
        }
      }

      return ErrorCode::FAILED;
    }

   private:
    Status status_ = Status::WAIT_START;
    uint32_t data_len_ = 0;
    RBTree<uint32_t> topic_map_;
    BaseQueue queue_;
    RawData parse_buff_;
    TopicHandle current_topic_ = nullptr;
  };

 private:
  TopicHandle block_ = nullptr;
  static RBTree<uint32_t> *domain_;
  static SpinLock domain_lock_;
  static Domain *def_domain_;
};
}  // namespace LibXR
