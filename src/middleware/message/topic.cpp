#include <atomic>

#include "libxr_def.hpp"
#include "message/message.hpp"
#include "mutex.hpp"

using namespace LibXR;

template class LibXR::RBTree<uint32_t>;
template class LibXR::Callback<LibXR::RawData&>;

void Topic::EnsureDomainRegistry()
{
  if (!domain_)
  {
    domain_ = new RBTree<uint32_t>([](const uint32_t& a, const uint32_t& b)
                                   { return static_cast<int>(a) - static_cast<int>(b); });
  }
}

Topic::Domain* Topic::EnsureDefaultDomain()
{
  if (!def_domain_)
  {
    def_domain_ = new Domain("libxr_def_domain");
  }

  return def_domain_;
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

void Topic::LockFromCallback(Topic::TopicHandle topic)
{
  if (topic->data_.mutex)
  {
    ASSERT(false);
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

void Topic::UnlockFromCallback(Topic::TopicHandle topic)
{
  if (topic->data_.mutex)
  {
    ASSERT(false);
  }
  else
  {
    topic->data_.busy.store(LockState::UNLOCKED, std::memory_order_release);
  }
}

Topic::Domain::Domain(const char* name)
{
  ASSERT(name != nullptr);

  EnsureDomainRegistry();

  auto crc32 = CRC32::Calculate(name, strlen(name));

  auto domain = domain_->Search<RBTree<uint32_t>>(crc32);

  if (domain != nullptr)
  {
    node_ = domain;
    return;
  }

  node_ = new LibXR::RBTree<uint32_t>::Node<LibXR::RBTree<uint32_t>>(
      [](const uint32_t& a, const uint32_t& b)
      { return static_cast<int>(a) - static_cast<int>(b); });

  domain_->Insert(*node_, crc32);
}

void LibXR::Topic::RegisterCallback(Callback& cb)
{
  CallbackBlock block;
  block.cb = cb;
  block.type = SuberType::CALLBACK;
  auto node = new (std::align_val_t(LibXR::CACHE_LINE_SIZE))
      LockFreeList::Node<CallbackBlock>(block);
  block_->data_.subers.Add(*node);
}

Topic::Topic() {}

Topic::Topic(const char* name, uint32_t max_length, Domain* domain, bool multi_publisher,
             bool cache, bool check_length)
{
  ASSERT(name != nullptr);

  if (domain == nullptr)
  {
    domain = EnsureDefaultDomain();
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

Topic::TopicHandle Topic::Find(const char* name, Domain* domain)
{
  ASSERT(name != nullptr);

  if (domain == nullptr)
  {
    if (def_domain_ == nullptr)
    {
      return nullptr;
    }
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

Topic::TopicHandle Topic::WaitTopic(const char* name, uint32_t timeout, Domain* domain)
{
  const uint32_t start_time = Thread::GetTime();
  TopicHandle topic = nullptr;
  do
  {
    topic = Find(name, domain);
    if (topic == nullptr)
    {
      if (timeout != UINT32_MAX &&
          static_cast<uint32_t>(Thread::GetTime() - start_time) >= timeout)
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
