#pragma once

#include "libxr_def.hpp"
#include "mutex.hpp"

namespace LibXR {
template <typename Data> class List {
public:
  class Node {
  public:
    Node() : next_(NULL) {}

    Node(const Data &data) : data_(data), next_(NULL) {}

    ~Node() {
      /* Should never be deconstructed in the list */
      ASSERT(next_ == NULL);
    }

    const Data &operator=(const Data &data) {
      data_ = data;
      return data_;
    }

    Data data_;
    Node *next_;
  };

  List() { head_.next_ = &head_; }

  ~List() {
    for (Node *pos = head_.next_; pos != &head_;) {
      auto tmp = pos->next_;
      pos->next_ = NULL;
      pos = tmp;
    }
    head_.next_ = NULL;
  }

  void Add(Node &data) {
    mutex_.Lock();
    data.next_ = head_.next_;
    head_.next_ = &data;
    mutex_.UnLock();
  }

  ErrorCode Delete(Node &data) {
    mutex_.Lock();
    for (Node *pos = &head_; pos->next_ != &head_; pos = pos->next_) {
      if (pos->next_ == &data) {
        pos->next_ = data.next_;
        data.next_ = NULL;
        mutex_.UnLock();
        return NO_ERR;
      }
    }
    mutex_.UnLock();

    return ERR_NOT_FOUND;
  }

  template <typename ArgType>
  ErrorCode Foreach(ErrorCode (*func)(Data &, ArgType), ArgType arg) {
    mutex_.Lock();
    for (Node *pos = head_.next_; pos != &head_; pos = pos->next_) {
      auto res = func(pos->data_, arg);
      if (res != NO_ERR) {
        mutex_.UnLock();
        return res;
      }
    }
    mutex_.UnLock();

    return NO_ERR;
  }

private:
  Node head_;
  LibXR::Mutex mutex_;
};
} // namespace LibXR