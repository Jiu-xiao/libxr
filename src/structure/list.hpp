#pragma once

#include "libxr_def.hpp"
#include "mutex.hpp"
#include <utility>

namespace LibXR {
class List {
public:
  class BaseNode {
  public:
    BaseNode(size_t size) : next_(NULL), size_(size) {}

    ~BaseNode() {
      /* Should never be deconstructed in the list */
      ASSERT(next_ == NULL);
    }

    BaseNode *next_;
    size_t size_;
  };

  template <typename Data> class Node : public BaseNode {
  public:
    Node() : BaseNode(sizeof(Data)), data_() {}

    Node(const Data &data) : BaseNode(sizeof(Data)), data_(data) {}

    const Node &operator=(const Data &data) {
      data_ = data;
      return data_;
    }

    Data &GetData() { return data_; }

    operator Data &() { return data_; }

    Data data_;
  };

  List() : head_(0) { head_.next_ = &head_; }

  ~List() {
    for (BaseNode *pos = head_.next_; pos != &head_;) {
      auto tmp = pos->next_;
      pos->next_ = NULL;
      pos = tmp;
    }
    head_.next_ = NULL;
  }

  void Add(BaseNode &data) {
    mutex_.Lock();
    data.next_ = head_.next_;
    head_.next_ = &data;
    mutex_.Unlock();
  }

  uint32_t Size() {
    uint32_t size = 0;
    mutex_.Lock();
    for (BaseNode *pos = head_.next_; pos != &head_; pos = pos->next_) {
      size++;
    }
    mutex_.Unlock();

    return size;
  }

  ErrorCode Delete(BaseNode &data) {
    mutex_.Lock();
    for (BaseNode *pos = &head_; pos->next_ != &head_; pos = pos->next_) {
      if (pos->next_ == &data) {
        pos->next_ = data.next_;
        data.next_ = NULL;
        mutex_.Unlock();
        return NO_ERR;
      }
    }
    mutex_.Unlock();

    return ERR_NOT_FOUND;
  }

  template <typename Data, typename ArgType>
  ErrorCode Foreach(ErrorCode (*func)(Data &, ArgType &), ArgType &arg) {
    mutex_.Lock();
    for (BaseNode *pos = head_.next_; pos != &head_; pos = pos->next_) {
      ASSERT(pos->size_ == sizeof(Data));
      auto res = func(reinterpret_cast<Node<Data> *>(pos)->data_, arg);
      if (res != NO_ERR) {
        mutex_.Unlock();
        return res;
      }
    }
    mutex_.Unlock();

    return NO_ERR;
  }

private:
  BaseNode head_;
  LibXR::Mutex mutex_;
};
} // namespace LibXR