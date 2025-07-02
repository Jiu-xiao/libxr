#include "list.hpp"

using namespace LibXR;

List::BaseNode::BaseNode(size_t size) : size_(size) {}

List::BaseNode::~BaseNode() { ASSERT(next_ == nullptr); }

List::List() noexcept : head_(0) { head_.next_ = &head_; }

List::~List()
{
  for (auto pos = head_.next_; pos != &head_;)
  {
    auto tmp = pos->next_;
    pos->next_ = nullptr;
    pos = tmp;
  }

  head_.next_ = nullptr;
}

void List::Add(BaseNode& data)
{
  mutex_.Lock();
  data.next_ = head_.next_;
  head_.next_ = &data;
  mutex_.Unlock();
}

uint32_t List::Size() noexcept
{
  uint32_t size = 0;
  mutex_.Lock();

  for (auto pos = head_.next_; pos != &head_; pos = pos->next_)
  {
    ++size;
  }

  mutex_.Unlock();
  return size;
}

ErrorCode List::Delete(BaseNode& data) noexcept
{
  mutex_.Lock();
  for (auto pos = &head_; pos->next_ != &head_; pos = pos->next_)
  {
    if (pos->next_ == &data)
    {
      pos->next_ = data.next_;
      data.next_ = nullptr;
      mutex_.Unlock();
      return ErrorCode::OK;
    }
  }
  mutex_.Unlock();
  return ErrorCode::NOT_FOUND;
}