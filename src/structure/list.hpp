#pragma once

#include <utility>

#include "libxr_assert.hpp"
#include "libxr_def.hpp"
#include "mutex.hpp"

namespace LibXR {
class List {
 public:
  class BaseNode {
   public:
    BaseNode(size_t size) : size_(size) {}

    ~BaseNode() { ASSERT(next_ == nullptr); }

    BaseNode* next_ = nullptr;
    size_t size_;
  };

  template <typename Data>
  class Node : public BaseNode {
   public:
    Node() : BaseNode(sizeof(Data)) {}
    explicit Node(const Data& data) : BaseNode(sizeof(Data)), data_(data) {}

    Node& operator=(const Data& data) {
      data_ = data;
      return *this;
    }

    Data* operator->() noexcept { return &data_; }
    const Data* operator->() const noexcept { return &data_; }
    Data& operator*() noexcept { return data_; }
    operator Data&() noexcept { return data_; }

    Data data_;
  };

  List() noexcept : head_(0) { head_.next_ = &head_; }

  ~List() {
    for (auto pos = head_.next_; pos != &head_;) {
      auto tmp = pos->next_;
      pos->next_ = nullptr;
      pos = tmp;
    }

    head_.next_ = nullptr;
  }

  void Add(BaseNode& data) {
    mutex_.Lock();
    data.next_ = head_.next_;
    head_.next_ = &data;
    mutex_.Unlock();
  }

  uint32_t Size() noexcept {
    uint32_t size = 0;
    mutex_.Lock();

    for (auto pos = head_.next_; pos != &head_; pos = pos->next_) {
      ++size;
    }

    mutex_.Unlock();
    return size;
  }

  ErrorCode Delete(BaseNode& data) noexcept {
    mutex_.Lock();
    for (auto pos = &head_; pos->next_ != &head_; pos = pos->next_) {
      if (pos->next_ == &data) {
        pos->next_ = data.next_;
        data.next_ = nullptr;
        mutex_.Unlock();
        return ErrorCode::OK;
      }
    }
    mutex_.Unlock();

    return ErrorCode::NOT_FOUND;
  }

  template <typename Data, typename ArgType,
            SizeLimitMode LimitMode = SizeLimitMode::MORE>
  ErrorCode Foreach(ErrorCode (*func)(Data&, ArgType), ArgType&& arg) {
    mutex_.Lock();
    for (auto pos = head_.next_; pos != &head_; pos = pos->next_) {
      Assert::SizeLimitCheck<LimitMode>(sizeof(Data), pos->size_);
      if (auto res = func(static_cast<Node<Data>*>(pos)->data_,
                          std::forward<ArgType>(arg));
          res != ErrorCode::OK) {
        mutex_.Unlock();
        return res;
      }
    }
    mutex_.Unlock();

    return ErrorCode::OK;
  }

 private:
  BaseNode head_;
  LibXR::Mutex mutex_;
};
}  // namespace LibXR