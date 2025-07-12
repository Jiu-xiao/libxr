#include "message.hpp"

#include <atomic>

#include "libxr_def.hpp"
#include "logger.hpp"
#include "mutex.hpp"

using namespace LibXR;

template class LibXR::RBTree<uint32_t>;
template class LibXR::Callback<LibXR::RawData &>;

void Topic::PackedDataHeader::SetDataLen(uint32_t len)
{
  data_len_raw[0] = static_cast<uint8_t>(len >> 16);
  data_len_raw[1] = static_cast<uint8_t>(len >> 8);
  data_len_raw[2] = static_cast<uint8_t>(len);
}

uint32_t Topic::PackedDataHeader::GetDataLen() const
{
  return static_cast<uint32_t>(data_len_raw[0]) << 16 |
         static_cast<uint32_t>(data_len_raw[1]) << 8 |
         static_cast<uint32_t>(data_len_raw[2]);
}

void Topic::Lock(Topic::TopicHandle topic)
{
  if (topic->data_.mutex)
  {
    topic->data_.mutex->Lock();
  }
  else
  {
    LockState expected = LockState::UNLOCKED;
    if (!topic->data_.busy.compare_exchange_strong(expected, LockState::LOCKED))
    {
      /* Multiple threads are trying to lock the same topic */
      ASSERT(false);
      return;
    }
  }
}

void Topic::Unlock(Topic::TopicHandle topic)
{
  if (topic->data_.mutex)
  {
    topic->data_.mutex->Unlock();
  }
  else
  {
    topic->data_.busy.store(LockState::UNLOCKED, std::memory_order_release);
  }
}

Topic::Domain::Domain(const char *name)
{
  if (!domain_)
  {
    if (!domain_)
    {
      domain_ =
          new RBTree<uint32_t>([](const uint32_t &a, const uint32_t &b)
                               { return static_cast<int>(a) - static_cast<int>(b); });
    }
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

void LibXR::Topic::RegisterCallback(Callback &cb)
{
  CallbackBlock block;
  block.cb = cb;
  block.type = SuberType::CALLBACK;
  auto node = new (std::align_val_t(LIBXR_CACHE_LINE_SIZE))
      LockFreeList::Node<CallbackBlock>(block);
  block_->data_.subers.Add(*node);
}

Topic::Topic() {}

Topic::Topic(const char *name, uint32_t max_length, Domain *domain, bool multi_publisher,
             bool cache, bool check_length)
{
  if (!def_domain_)
  {
    if (!def_domain_)
    {
      def_domain_ = new Domain("libxr_def_domain");
    }
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

    if (multi_publisher && !topic->data_.mutex)
    {
      ASSERT(false);
    }

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

    if (multi_publisher)
    {
      block_->data_.mutex = new Mutex();
      block_->data_.busy.store(LockState::USE_MUTEX, std::memory_order_release);
    }
    else
    {
      block_->data_.mutex = nullptr;
      block_->data_.busy.store(LockState::UNLOCKED, std::memory_order_release);
    }

    domain->node_->data_.Insert(*block_, crc32);
  }

  if (cache && !block_->data_.cache)
  {
    EnableCache();
  }
}

Topic::Topic(TopicHandle topic) : block_(topic) {}

Topic::TopicHandle Topic::Find(const char *name, Domain *domain)
{
  if (domain == nullptr)
  {
    domain = def_domain_;
  }

  auto crc32 = CRC32::Calculate(name, strlen(name));

  return domain->node_->data_.Search<Block>(crc32);
}

void Topic::EnableCache()
{
  Lock(block_);

  if (!block_->data_.cache)
  {
    block_->data_.cache = true;
    block_->data_.data.addr_ = new uint8_t[block_->data_.max_length];
  }

  Unlock(block_);
}

void Topic::Publish(void *addr, uint32_t size)
{
  Lock(block_);
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

  Unlock(block_);
}

void Topic::PackData(uint32_t topic_name_crc32, RawData buffer, RawData source)
{
  PackedData<uint8_t> *pack = reinterpret_cast<PackedData<uint8_t> *>(buffer.addr_);

  memcpy(&pack->raw.data_, source.addr_, source.size_);

  pack->raw.header_.prefix = 0xa5;
  pack->raw.header_.topic_name_crc32 = topic_name_crc32;
  pack->raw.header_.SetDataLen(source.size_);
  pack->raw.header_.pack_header_crc8 =
      CRC8::Calculate(&pack->raw, sizeof(PackedDataHeader) - sizeof(uint8_t));
  uint8_t *crc8_pack =
      reinterpret_cast<uint8_t *>(reinterpret_cast<uint8_t *>(pack) + PACK_BASE_SIZE +
                                  source.size_ - sizeof(uint8_t));
  *crc8_pack = CRC8::Calculate(pack, PACK_BASE_SIZE - sizeof(uint8_t) + source.size_);
}

Topic::TopicHandle Topic::WaitTopic(const char *name, uint32_t timeout, Domain *domain)
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

uint32_t Topic::GetKey() const
{
  if (block_)
  {
    return block_->key;
  }
  else
  {
    return 0;
  }
}

Topic::Server::Server(size_t buffer_length)
    : topic_map_([](const uint32_t &a, const uint32_t &b)
                 { return static_cast<int>(a) - static_cast<int>(b); }),
      queue_(1, buffer_length)
{
  /* Minimum size: header8 + crc32 + length24 + crc8 + data +  crc8 = 10 */
  ASSERT(buffer_length > PACK_BASE_SIZE);
  parse_buff_.size_ = buffer_length;
  parse_buff_.addr_ = new uint8_t[buffer_length];
}

void Topic::Server::Register(TopicHandle topic)
{
  auto node = new RBTree<uint32_t>::Node<TopicHandle>(topic);
  topic_map_.Insert(*node, topic->key);
}

size_t Topic::Server::ParseData(ConstRawData data)
{
  size_t count = 0;

  queue_.PushBatch(data.addr_, data.size_);

  while (true)
  { /* 1. Check prefix */
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
        return count;
      }
    }

    /* 2. Get topic info */
    if (status_ == Status::WAIT_TOPIC)
    {
      /* Check size&crc */
      if (queue_.Size() >= sizeof(PackedDataHeader))
      {
        queue_.PopBatch(parse_buff_.addr_, sizeof(PackedDataHeader));
        if (CRC8::Verify(parse_buff_.addr_, sizeof(PackedDataHeader)))
        {
          auto header = reinterpret_cast<PackedDataHeader *>(parse_buff_.addr_);
          /* Find topic */
          auto node = topic_map_.Search<TopicHandle>(header->topic_name_crc32);
          if (node)
          {
            data_len_ = header->GetDataLen();
            current_topic_ = *node;
            if (data_len_ + PACK_BASE_SIZE >= queue_.length_)
            {
              status_ = Status::WAIT_START;
              continue;
            }
            status_ = Status::WAIT_DATA_CRC;
          }
          else
          {
            status_ = Status::WAIT_START;
            continue;
          }
        }
        else
        {
          status_ = Status::WAIT_START;
          continue;
        }
      }
      else
      {
        return count;
      }
    }

    /* 3. Get data */
    if (status_ == Status::WAIT_DATA_CRC)
    {
      /* Check size&crc */
      if (queue_.Size() >= data_len_ + sizeof(uint8_t))
      {
        uint8_t *data =
            reinterpret_cast<uint8_t *>(parse_buff_.addr_) + sizeof(PackedDataHeader);
        queue_.PopBatch(data, data_len_ + sizeof(uint8_t));
        if (CRC8::Verify(parse_buff_.addr_,
                         data_len_ + sizeof(PackedDataHeader) + sizeof(uint8_t)))
        {
          status_ = Status::WAIT_START;
          auto data =
              reinterpret_cast<uint8_t *>(parse_buff_.addr_) + sizeof(PackedDataHeader);
          if (data_len_ > current_topic_->data_.max_length)
          {
            data_len_ = current_topic_->data_.max_length;
          }
          Topic(current_topic_).Publish(data, data_len_);

          count++;

          continue;
        }
        else
        {
          status_ = Status::WAIT_START;
          continue;
        }
      }
      else
      {
        return count;
      }
    }
  }
  return count;
}
