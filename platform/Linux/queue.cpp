#pragma once

#include "queue.hpp"
#include "libxr_def.hpp"
#include "libxr_platform.hpp"
#include <optional>

using namespace LibXR;

template <typename Data, unsigned int Length> Queue<Data, Length>::Queue() {}

template <typename Data, unsigned int Length>
ErrorCode Queue<Data, Length>::Push(const Data &data) {
  queue_handle_.mutex.Lock();
  if (queue_handle_.is_full) {
    queue_handle_.mutex.UnLock();
    return ERR_FULL;
  }
  queue_handle_.data[queue_handle_.tail] = data;
  queue_handle_.tail = (queue_handle_.tail + 1) % Length;
  if (queue_handle_.head == queue_handle_.tail) {
    queue_handle_.is_full = true;
  }
  queue_handle_.mutex.UnLock();
  queue_handle_.sem.Post();

  return NO_ERR;
}

template <typename Data, unsigned int Length>
ErrorCode Queue<Data, Length>::PushFromCallback(const Data &data, bool in_isr) {
  UNUSED(in_isr);
  return Push(data);
}

template <typename Data, unsigned int Length>
ErrorCode Queue<Data, Length>::Pop(Data &data, uint32_t timeout) {
  if (queue_handle_.sem.Wait(timeout) == NO_ERR) {
    queue_handle_.mutex.Lock();
    data = queue_handle_.data[queue_handle_.head];
    queue_handle_.head = (queue_handle_.head + 1) % Length;
    queue_handle_.is_full = false;
    queue_handle_.mutex.UnLock();
    return NO_ERR;
  } else {
    return ERR_EMPTY;
  }
}

template <typename Data, unsigned int Length>
ErrorCode Queue<Data, Length>::Overwrite(const Data &data) {
  queue_handle_.mutex.Lock();
  if (Size() > 0) {
    while (queue_handle_.sem.Wait(0) == NO_ERR) {
    }
  }
  queue_handle_.head = queue_handle_.tail = 0;
  queue_handle_.is_full = false;

  queue_handle_.data[queue_handle_.tail] = data;
  queue_handle_.tail = (queue_handle_.tail + 1) % Length;
  if (queue_handle_.head == queue_handle_.tail) {
    queue_handle_.is_full = true;
  }
  queue_handle_.mutex.UnLock();

  return NO_ERR;
}

template <typename Data, unsigned int Length>
ErrorCode Queue<Data, Length>::OverwriteFromCallback(const Data &data,
                                                     bool in_isr) {
  UNUSED(in_isr);
  return Overwrite(data);
}

template <typename Data, unsigned int Length>
void Queue<Data, Length>::Reset() {
  queue_handle_.mutex.Lock();
  if (Size() > 0) {
    while (queue_handle_.sem.Wait(0) == NO_ERR) {
    }
  }
  queue_handle_.head = queue_handle_.tail = 0;
  queue_handle_.is_full = false;
  queue_handle_.mutex.UnLock();
}

template <typename Data, unsigned int Length>
size_t Queue<Data, Length>::Size() {
  queue_handle_.mutex.Lock();

  if (queue_handle_.is_full) {
    queue_handle_.mutex.UnLock();
    return Length;
  } else if (queue_handle_.tail >= queue_handle_.head) {
    queue_handle_.mutex.UnLock();
    return queue_handle_.tail - queue_handle_.head;
  } else {
    queue_handle_.mutex.UnLock();
    return Length + queue_handle_.tail - queue_handle_.head;
  }
}

template <typename Data, unsigned int Length>
size_t Queue<Data, Length>::EmptySize() {
  return Length - Size();
}
