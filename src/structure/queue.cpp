#include "queue.hpp"

using namespace LibXR;

BaseQueue::BaseQueue(uint16_t element_size, size_t length, uint8_t *buffer)
    : queue_array_(buffer),
      ELEMENT_SIZE(element_size),
      length_(length),
      own_buffer_(false)
{
}

BaseQueue::BaseQueue(uint16_t element_size, size_t length)
    : queue_array_(new uint8_t[length * element_size]),
      ELEMENT_SIZE(element_size),
      length_(length),
      own_buffer_(true)
{
}

BaseQueue::~BaseQueue()
{
  if (own_buffer_)
  {
    delete[] queue_array_;
  }
}

[[nodiscard]] void *BaseQueue::operator[](uint32_t index)
{
  return &queue_array_[static_cast<size_t>(index * ELEMENT_SIZE)];
}

ErrorCode BaseQueue::Push(const void *data)
{
  ASSERT(data != nullptr);

  if (is_full_)
  {
    return ErrorCode::FULL;
  }

  memcpy(&queue_array_[tail_ * ELEMENT_SIZE], data, ELEMENT_SIZE);

  tail_ = (tail_ + 1) % length_;
  if (head_ == tail_)
  {
    is_full_ = true;
  }

  return ErrorCode::OK;
}

ErrorCode BaseQueue::Peek(void *data)
{
  ASSERT(data != nullptr);

  if (Size() > 0)
  {
    memcpy(data, &queue_array_[head_ * ELEMENT_SIZE], ELEMENT_SIZE);
    return ErrorCode::OK;
  }
  else
  {
    return ErrorCode::EMPTY;
  }
}

ErrorCode BaseQueue::Pop(void *data)
{
  if (Size() == 0)
  {
    return ErrorCode::EMPTY;
  }

  if (data != nullptr)
  {
    memcpy(data, &queue_array_[head_ * ELEMENT_SIZE], ELEMENT_SIZE);
  }
  head_ = (head_ + 1) % length_;
  is_full_ = false;
  return ErrorCode::OK;
}

int BaseQueue::GetLastElementIndex() const
{
  if (Size() == 0)
  {
    return -1;
  }
  return static_cast<int>((tail_ + length_ - 1) % length_);
}

int BaseQueue::GetFirstElementIndex() const
{
  if (Size() == 0)
  {
    return -1;
  }
  return static_cast<int>(head_);
}

ErrorCode BaseQueue::PushBatch(const void *data, size_t size)
{
  ASSERT(data != nullptr);

  auto avail = EmptySize();
  if (avail < size)
  {
    return ErrorCode::FULL;
  }

  auto tmp = reinterpret_cast<const uint8_t *>(data);

  size_t first_part = LibXR::min(size, length_ - tail_);
  memcpy(&queue_array_[tail_ * ELEMENT_SIZE], tmp, first_part * ELEMENT_SIZE);

  if (size > first_part)
  {
    memcpy(queue_array_, &tmp[first_part * ELEMENT_SIZE],
           (size - first_part) * ELEMENT_SIZE);
  }

  tail_ = (tail_ + size) % length_;
  if (head_ == tail_)
  {
    is_full_ = true;
  }
  return ErrorCode::OK;
}

ErrorCode BaseQueue::PopBatch(void *data, size_t size)
{
  if (Size() < size)
  {
    return ErrorCode::EMPTY;
  }

  if (size == 0)
  {
    return ErrorCode::OK;
  }
  is_full_ = false;

  size_t first_part = LibXR::min(size, length_ - head_);
  if (data != nullptr)
  {
    auto tmp = reinterpret_cast<uint8_t *>(data);
    memcpy(tmp, &queue_array_[head_ * ELEMENT_SIZE], first_part * ELEMENT_SIZE);
    if (size > first_part)
    {
      memcpy(&tmp[first_part * ELEMENT_SIZE], queue_array_,
             (size - first_part) * ELEMENT_SIZE);
    }
  }

  head_ = (head_ + size) % length_;
  return ErrorCode::OK;
}

ErrorCode BaseQueue::PeekBatch(void *data, size_t size)
{
  ASSERT(data != nullptr);

  if (Size() < size)
  {
    return ErrorCode::EMPTY;
  }

  auto index = head_;

  auto tmp = reinterpret_cast<uint8_t *>(data);

  size_t first_part = LibXR::min(size, length_ - index);
  memcpy(tmp, &queue_array_[index * ELEMENT_SIZE], first_part * ELEMENT_SIZE);

  if (first_part < size)
  {
    memcpy(&tmp[first_part * ELEMENT_SIZE], queue_array_,
           (size - first_part) * ELEMENT_SIZE);
  }

  return ErrorCode::OK;
}

ErrorCode BaseQueue::Overwrite(const void *data)
{
  ASSERT(data != nullptr);

  head_ = tail_ = 0;
  is_full_ = false;

  memcpy(queue_array_, data, ELEMENT_SIZE * length_);

  tail_ = (tail_ + 1) % length_;
  if (head_ == tail_)
  {
    is_full_ = true;
  }

  return ErrorCode::OK;
}

void BaseQueue::Reset()
{
  head_ = tail_ = 0;
  is_full_ = false;
}

[[nodiscard]] size_t BaseQueue::Size() const
{
  if (is_full_)
  {
    return length_;
  }
  else if (tail_ >= head_)
  {
    return tail_ - head_;
  }
  else
  {
    return length_ + tail_ - head_;
  }
}

[[nodiscard]] size_t BaseQueue::EmptySize() const { return length_ - Size(); }
