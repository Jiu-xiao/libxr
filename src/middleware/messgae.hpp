#pragma once

#include "crc.hpp"
#include "libxr_cb.hpp"
#include "libxr_def.hpp"
#include "libxr_time.hpp"
#include "libxr_type.hpp"
#include "list.hpp"
#include "lockfree_queue.hpp"
#include "mutex.hpp"
#include "queue.hpp"
#include "rbt.hpp"
#include "semaphore.hpp"
#include "spin_lock.hpp"
#include "thread.hpp"
#include <cstdint>

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
  } RemoteDataHeader;

  template <typename Data> class __attribute__((packed)) RemoteData {
  public:
    struct __attribute__((packed)) {
      RemoteDataHeader header;
      Data data_;
    } raw;

    uint8_t crc8_;

    const Data &operator=(const Data &data) {
      raw.data_ = data;
      crc8_ = CRC8::Calculate(&raw, sizeof(raw));
    }

    operator Data &() { return raw.data_; }

    Data &GetData() { return raw.data_; }
  };

  typedef RBTree<uint32_t>::Node<Block> *TopicHandle;

  class Domain {
  public:
    Domain(const char *name) {
      if (!domain_) {
        domain_lock_.Lock();
        if (!domain_) {
          domain_ = new RBTree<uint32_t>(
              [](const uint32_t &a, const uint32_t &b) { return int(a - b); });
        }
        domain_lock_.Unlock();
      }

      auto crc32 = CRC32::Calculate(name, strlen(name));

      auto domain = domain_->Search<RBTree<uint32_t>>(crc32);

      if (domain != NULL) {
        node_ = domain;
        return;
      }

      node_ = new LibXR::RBTree<uint32_t>::Node<LibXR::RBTree<uint32_t>>(
          RBTree<uint32_t>(
              [](const uint32_t &a, const uint32_t &b) { return int(a - b); }));

      domain_->Insert(*node_, crc32);
    }

    RBTree<uint32_t>::Node<RBTree<uint32_t>> *node_;
  };

  enum class SuberType {
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

  template <typename Data> class SyncSubscriber {
  public:
    SyncSubscriber(const char *name, Data &data, Domain *domain = NULL) {
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

  template <typename Data> class ASyncSubscriber {
  public:
    ASyncSubscriber(const char *name, Data &data, Domain *domain = NULL) {
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
    void (*fun)(RawData &, void *);
  } QueueBlock;

  class QueuedSubscriber {
  public:
    template <typename Data, uint32_t Length>
    QueuedSubscriber(const char *name, LockFreeQueue<Data> &queue,
                     Domain *domain = NULL) {
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
      block->data_.fun = [](RawData &data, void *arg) {
        LockFreeQueue<Data> *queue = reinterpret_cast<LockFreeQueue<Data>>(arg);
        queue->Push(reinterpret_cast<Data>(data.addr_));
      };

      topic.block_->data_.subers.Add(*block);
    }

    template <typename Data>
    QueuedSubscriber(const char *name, Queue<Data> &queue,
                     Domain *domain = NULL) {
      *this = QueuedSubscriber(WaitTopic(name, UINT32_MAX, domain), queue);
    }

    template <typename Data> QueuedSubscriber(Topic topic, Queue<Data> &queue) {
      if (topic.block_->data_.check_length) {
        ASSERT(topic.block_->data_.max_length == sizeof(Data));
      } else {
        ASSERT(topic.block_->data_.max_length <= sizeof(Data));
      }

      auto block = new List::Node<QueueBlock>;
      block->data_.type = SuberType::QUEUE;
      block->data_.queue = &queue;
      block->data_.fun = [](RawData &data, void *arg) {
        Queue<Data> *queue = reinterpret_cast<Queue<Data> *>(arg);
        queue->Push(*reinterpret_cast<const Data *>(data.addr_));
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

  Topic(const char *name, uint32_t max_length, Domain *domain = NULL,
        bool cache = false, bool check_length = false) {
    if (!def_domain_) {
      domain_lock_.Lock();
      if (!domain_) {
        domain_ = new RBTree<uint32_t>(
            [](const uint32_t &a, const uint32_t &b) { return int(a - b); });
      }
      if (!def_domain_) {
        def_domain_ = new Domain("libxr_def_domain");
      }
      domain_lock_.Unlock();
    }

    if (domain == NULL) {
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
      block_->data_.data.addr_ = NULL;
      block_->data_.cache = false;
      block_->data_.check_length = check_length;

      domain->node_->data_.Insert(*block_, crc32);
    }

    if (cache && !block_->data_.cache) {
      EnableCache();
    }
  }

  template <typename Data>
  static Topic CreateTopic(const char *name, Domain *domain = NULL,
                           bool cache = false, bool check_length = true) {
    return Topic(name, sizeof(Data), domain, cache, check_length);
  }

  Topic(TopicHandle topic) : block_(topic) {}

  static TopicHandle Find(const char *name, Domain *domain = NULL) {
    if (domain == NULL) {
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

  template <typename Data> void Publish(Data &data) {
    block_->data_.mutex.Lock();
    if (block_->data_.check_length) {
      ASSERT(sizeof(Data) == block_->data_.max_length);
    } else {
      ASSERT(sizeof(Data) <= block_->data_.max_length);
    }

    if (block_->data_.cache) {
      memcpy(block_->data_.data.addr_, &data, sizeof(Data));
      block_->data_.data.size_ = sizeof(Data);
    } else {
      block_->data_.data = data;
    }

    auto foreach_fun = [](SuberBlock &block, RawData &data) {
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
        queue_block->fun(data, queue_block->queue);
        break;
      }
      case SuberType::CALLBACK: {
        auto cb_block = reinterpret_cast<CallbackBlock *>(&block);
        cb_block->cb.RunFromUser(data);
        break;
      }
      }
      return ErrorCode::OK;
    };

    block_->data_.subers.Foreach<SuberBlock, RawData>(foreach_fun,
                                                      block_->data_.data);

    block_->data_.mutex.Unlock();
  }

  template <typename Data> void DumpData(Data &data) {
    if (block_->data_.data.addr_ != NULL) {
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

  template <typename Data> void DumpData(RemoteData<Data> &data) {
    if (block_->data_.data.addr_ != NULL) {
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

private:
  static TopicHandle WaitTopic(const char *name, uint32_t timeout = UINT32_MAX,
                               Domain *domain = NULL) {
    TopicHandle topic = NULL;
    do {
      topic = Find(name, domain);
      if (topic == NULL) {
        if (timeout <= Thread::GetTime()) {
          return NULL;
        }
        Thread::Sleep(1);
      }
    } while (topic == NULL);

    return topic;
  }

  TopicHandle block_ = NULL;
  static RBTree<uint32_t> *domain_;
  static SpinLock domain_lock_;
  static Domain *def_domain_;
};
} // namespace LibXR