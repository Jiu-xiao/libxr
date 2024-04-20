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
    List sync_subers, queue_subers, callbacks;
  } Block;

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
        domain_lock_.UnLock();
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

  typedef struct {
    RawData buff;
    Semaphore sem;
  } SyncBlock;

  template <typename Data> class SyncSubscriber {
  public:
    SyncSubscriber(const char *name, Data &data, Domain *domain = NULL) {
      *this = SyncSubscriber(WaitTopic(name, domain), data);
    }

    SyncSubscriber(Topic topic, Data &data) {
      if (topic.block_->data_.check_length) {
        ASSERT(topic.block_->data_.max_length == sizeof(Data));
      } else {
        ASSERT(topic.block_->data_.max_length <= sizeof(Data));
      }

      topic.block_->data_.mutex.Lock();
      block_ = new List::Node<SyncBlock>;
      block_->data_.buff = RawData(data);
      topic.block_->data_.sync_subers.Add(*block_);
      topic.block_->data_.mutex.UnLock();
    }

    ErrorCode Wait(uint32_t timeout = UINT32_MAX) {
      return block_->data_.sem.Wait(timeout);
    }

    List::Node<SyncBlock> *block_;
  };

  typedef struct {
    void *queue;
    void (*fun)(RawData &, void *);
  } QueueBlock;

  class QueuedSubscriber {
  public:
    template <typename Data, uint32_t Length>
    QueuedSubscriber(const char *name, LockFreeQueue<Data, Length> &queue,
                     Domain *domain = NULL) {
      *this = QueuedSubscriber(WaitTopic(name, domain), queue);
    }

    template <typename Data, uint32_t Length>
    QueuedSubscriber(Topic topic, LockFreeQueue<Data, Length> &queue) {
      if (topic.block_->data_.check_length) {
        ASSERT(topic.block_->data_.max_length == sizeof(Data));
      } else {
        ASSERT(topic.block_->data_.max_length <= sizeof(Data));
      }

      auto node = new List::Node<QueueBlock>;
      node->data_.queue = &queue;
      node->data_.fun = [](RawData &data, void *arg) {
        LockFreeQueue<Data, Length> *queue =
            reinterpret_cast<LockFreeQueue<Data, Length>>(arg);
        queue->Push(reinterpret_cast<Data>(data.addr_));
      };

      topic.block_->data_.queue_subers.Add(*node);
    }

    template <typename Data, uint32_t Length>
    QueuedSubscriber(const char *name, Queue<Data, Length> &queue,
                     Domain *domain = NULL) {
      *this = QueuedSubscriber(WaitTopic(name, domain), queue);
    }

    template <typename Data, uint32_t Length>
    QueuedSubscriber(Topic topic, Queue<Data, Length> &queue) {
      if (topic.block_->data_.check_length) {
        ASSERT(topic.block_->data_.max_length == sizeof(Data));
      } else {
        ASSERT(topic.block_->data_.max_length <= sizeof(Data));
      }

      auto node = new List::Node<QueueBlock>;
      node->data_.queue = &queue;
      node->data_.fun = [](RawData &data, void *arg) {
        Queue<Data, Length> *queue =
            reinterpret_cast<Queue<Data, Length> *>(arg);
        queue->Push(*reinterpret_cast<const Data *>(data.addr_));
      };

      topic.block_->data_.queue_subers.Add(*node);
    }
  };

  void RegisterCallback(Callback<void, RawData &> &cb) {
    auto node = new List::Node<Callback<void, RawData &>>(cb);
    block_->data_.callbacks.Add(*node);
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
      domain_lock_.UnLock();
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
    block_->data_.mutex.UnLock();
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

    ErrorCode (*sync_foreach_fun)(SyncBlock & block, Topic & topic) =
        [](SyncBlock &block, Topic &topic) {
          memcpy(block.buff.addr_, topic.block_->data_.data.addr_,
                 topic.block_->data_.data.size_);
          block.sem.Post();

          return NO_ERR;
        };

    block_->data_.sync_subers.Foreach<SyncBlock, Topic>(sync_foreach_fun,
                                                        *this);

    ErrorCode (*queue_foreach_fun)(QueueBlock & block, RawData & data) =
        [](QueueBlock &block, RawData &data) {
          block.fun(data, block.queue);

          return NO_ERR;
        };

    block_->data_.queue_subers.Foreach<QueueBlock, RawData>(queue_foreach_fun,
                                                            block_->data_.data);

    ErrorCode (*cb_foreach_fun)(Callback<void, RawData &> & cb,
                                RawData & data) =
        [](Callback<void, RawData &> &cb, RawData &data) {
          cb.RunFromUser(data);
          return NO_ERR;
        };

    block_->data_.callbacks.Foreach<Callback<void, RawData &>, RawData>(
        cb_foreach_fun, block_->data_.data);

    block_->data_.mutex.UnLock();
  }

private:
  static Topic WaitTopic(const char *name, Domain *domain = NULL) {
    TopicHandle topic = NULL;
    do {
      topic = Find(name, domain);
      if (topic == NULL) {
        Thread::Sleep(1);
      }
    } while (topic == NULL);

    return Topic(topic);
  }

  TopicHandle block_ = NULL;
  static RBTree<uint32_t> *domain_;
  static SpinLock domain_lock_;
  static Domain *def_domain_;
};
} // namespace LibXR