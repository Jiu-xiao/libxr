#include "lockfree_list.hpp"

using namespace LibXR;

LockFreeList::BaseNode::BaseNode(size_t size) : size_(size) {}

LockFreeList::BaseNode::~BaseNode() { ASSERT(next_ == nullptr); }

LockFreeList::LockFreeList() noexcept : head_(0)
{
  head_.next_.store(&head_, std::memory_order_relaxed);
}

LockFreeList::~LockFreeList()
{
  for (auto pos = head_.next_.load(); pos != &head_;)
  {
    auto tmp = pos->next_.load();
    pos->next_.store(nullptr);
    pos = tmp;
  }

  head_.next_.store(nullptr);
}

void LockFreeList::Add(BaseNode& data)
{
  BaseNode* current_head = nullptr;
  do
  {
    current_head = head_.next_.load(std::memory_order_acquire);
    data.next_.store(current_head, std::memory_order_relaxed);
  } while (!head_.next_.compare_exchange_weak(
      current_head, &data, std::memory_order_release, std::memory_order_acquire));
}

uint32_t LockFreeList::Size() noexcept
{
  uint32_t size = 0;
  for (auto pos = head_.next_.load(std::memory_order_acquire); pos != &head_;
       pos = pos->next_.load(std::memory_order_relaxed))
  {
    ++size;
  }
  return size;
}
