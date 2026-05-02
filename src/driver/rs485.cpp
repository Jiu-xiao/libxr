#include "rs485.hpp"

using namespace LibXR;

template class LibXR::Callback<ConstRawData>;

namespace
{

ConstRawData CopyRawData(ConstRawData data)
{
  if (data.addr_ == nullptr || data.size_ == 0)
  {
    return {};
  }

  auto* copy = new uint8_t[data.size_];
  Memory::FastCopy(copy, data.addr_, data.size_);
  return {copy, data.size_};
}

RS485::Filter CopyFilter(const RS485::Filter& filter)
{
  return RS485::Filter{filter.offset, CopyRawData(filter.data), CopyRawData(filter.mask)};
}

bool IsFilterValid(const RS485::Filter& filter)
{
  if (filter.data.addr_ == nullptr && filter.data.size_ != 0)
  {
    return false;
  }

  if (filter.mask.addr_ == nullptr)
  {
    return filter.mask.size_ == 0;
  }

  return filter.mask.size_ == 0 || filter.mask.size_ == filter.data.size_;
}

}  // namespace

bool RS485::Filter::Match(ConstRawData frame) const
{
  if (data.addr_ == nullptr || data.size_ == 0)
  {
    return true;
  }

  if (frame.addr_ == nullptr || frame.size_ < offset || data.size_ > frame.size_ - offset)
  {
    return false;
  }

  if (mask.addr_ != nullptr && mask.size_ != 0 && mask.size_ != data.size_)
  {
    return false;
  }

  auto frame_data = static_cast<const uint8_t*>(frame.addr_) + offset;
  auto expected = static_cast<const uint8_t*>(data.addr_);
  auto bit_mask = (mask.addr_ != nullptr && mask.size_ != 0)
                      ? static_cast<const uint8_t*>(mask.addr_)
                      : nullptr;

  for (size_t i = 0; i < data.size_; ++i)
  {
    const uint8_t MASK = (bit_mask != nullptr) ? bit_mask[i] : 0xFF;
    if ((frame_data[i] & MASK) != (expected[i] & MASK))
    {
      return false;
    }
  }
  return true;
}

ErrorCode RS485::Register(Callback cb) { return Register(cb, Filter{}); }

ErrorCode RS485::Register(Callback cb, const Filter& filter)
{
  if (!IsFilterValid(filter))
  {
    return ErrorCode::ARG_ERR;
  }

  auto node = new LockFreeList::Node<Subscription>(Subscription{CopyFilter(filter), cb});
  subscriber_list_.Add(*node);
  return ErrorCode::OK;
}

void RS485::OnFrame(ConstRawData frame, bool in_isr)
{
  subscriber_list_.Foreach<Subscription>(
      [&](Subscription& node)
      {
        if (node.filter.Match(frame))
        {
          node.cb.Run(in_isr, frame);
        }

        return ErrorCode::OK;
      });
}
