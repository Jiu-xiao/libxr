#include "queue_base.hpp"

#include "libxr_mem.hpp"

using namespace LibXR;

QueueBase::QueueBase(uint16_t element_size, size_t length, uint8_t* buffer)
    : queue_array_(buffer),
      ELEMENT_SIZE(element_size),
      length_(length),
      own_buffer_(false)
{
}

QueueBase::QueueBase(uint16_t element_size, size_t length)
    : queue_array_(new uint8_t[length * element_size]),
      ELEMENT_SIZE(element_size),
      length_(length),
      own_buffer_(true)
{
}

QueueBase::~QueueBase()
{
  if (own_buffer_)
  {
    delete[] queue_array_;
  }
}

[[nodiscard]] void* QueueBase::operator[](uint32_t index)
{
  return &queue_array_[static_cast<size_t>(index * ELEMENT_SIZE)];
}

ErrorCode QueueBase::PushBytes(const void* data)
{
  ASSERT(data != nullptr);

  if (is_full_)
  {
    return ErrorCode::FULL;
  }

  LibXR::Memory::FastCopy(&queue_array_[tail_ * ELEMENT_SIZE], data, ELEMENT_SIZE);

  tail_ = (tail_ + 1) % length_;
  if (head_ == tail_)
  {
    is_full_ = true;
  }

  return ErrorCode::OK;
}

ErrorCode QueueBase::PeekBytes(void* data)
{
  ASSERT(data != nullptr);

  if (Size() > 0)
  {
    LibXR::Memory::FastCopy(data, &queue_array_[head_ * ELEMENT_SIZE], ELEMENT_SIZE);
    return ErrorCode::OK;
  }
  else
  {
    return ErrorCode::EMPTY;
  }
}

ErrorCode QueueBase::PopBytes(void* data)
{
  if (Size() == 0)
  {
    return ErrorCode::EMPTY;
  }

  if (data != nullptr)
  {
    LibXR::Memory::FastCopy(data, &queue_array_[head_ * ELEMENT_SIZE], ELEMENT_SIZE);
  }
  head_ = (head_ + 1) % length_;
  is_full_ = false;
  return ErrorCode::OK;
}

int QueueBase::GetLastElementIndex() const
{
  if (Size() == 0)
  {
    return -1;
  }
  return static_cast<int>((tail_ + length_ - 1) % length_);
}

int QueueBase::GetFirstElementIndex() const
{
  if (Size() == 0)
  {
    return -1;
  }
  return static_cast<int>(head_);
}

ErrorCode QueueBase::PushBatchBytes(const void* data, size_t size)
{
  ASSERT(data != nullptr);

  auto avail = EmptySize();
  if (avail < size)
  {
    return ErrorCode::FULL;
  }

  auto tmp = reinterpret_cast<const uint8_t*>(data);

  size_t first_part = LibXR::min(size, length_ - tail_);
  LibXR::Memory::FastCopy(&queue_array_[tail_ * ELEMENT_SIZE], tmp,
                          first_part * ELEMENT_SIZE);

  if (size > first_part)
  {
    LibXR::Memory::FastCopy(queue_array_, &tmp[first_part * ELEMENT_SIZE],
                            (size - first_part) * ELEMENT_SIZE);
  }

  tail_ = (tail_ + size) % length_;
  if (head_ == tail_)
  {
    is_full_ = true;
  }
  return ErrorCode::OK;
}

ErrorCode QueueBase::PopBatchBytes(void* data, size_t size)
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
    auto tmp = reinterpret_cast<uint8_t*>(data);
    LibXR::Memory::FastCopy(tmp, &queue_array_[head_ * ELEMENT_SIZE],
                            first_part * ELEMENT_SIZE);
    if (size > first_part)
    {
      LibXR::Memory::FastCopy(&tmp[first_part * ELEMENT_SIZE], queue_array_,
                              (size - first_part) * ELEMENT_SIZE);
    }
  }

  head_ = (head_ + size) % length_;
  return ErrorCode::OK;
}

ErrorCode QueueBase::PeekBatchBytes(void* data, size_t size)
{
  ASSERT(data != nullptr);

  if (Size() < size)
  {
    return ErrorCode::EMPTY;
  }

  auto index = head_;

  auto tmp = reinterpret_cast<uint8_t*>(data);

  size_t first_part = LibXR::min(size, length_ - index);
  LibXR::Memory::FastCopy(tmp, &queue_array_[index * ELEMENT_SIZE],
                          first_part * ELEMENT_SIZE);

  if (first_part < size)
  {
    LibXR::Memory::FastCopy(&tmp[first_part * ELEMENT_SIZE], queue_array_,
                            (size - first_part) * ELEMENT_SIZE);
  }

  return ErrorCode::OK;
}

ErrorCode QueueBase::OverwriteBytes(const void* data)
{
  ASSERT(data != nullptr);

  head_ = tail_ = 0;
  is_full_ = false;

  LibXR::Memory::FastCopy(queue_array_, data, ELEMENT_SIZE * length_);

  tail_ = (tail_ + 1) % length_;
  if (head_ == tail_)
  {
    is_full_ = true;
  }

  return ErrorCode::OK;
}

void QueueBase::Reset()
{
  head_ = tail_ = 0;
  is_full_ = false;
}

[[nodiscard]] size_t QueueBase::Size() const
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

[[nodiscard]] size_t QueueBase::EmptySize() const { return length_ - Size(); }
