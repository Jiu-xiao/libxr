#include "rs485.hpp"

using namespace LibXR;

template class LibXR::Callback<ConstRawData>;

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

void RS485::Register(Callback cb) { Register(cb, Filter{}); }

void RS485::Register(Callback cb, const Filter& filter)
{
  auto node = new (std::align_val_t(LibXR::CACHE_LINE_SIZE))
      LockFreeList::Node<Subscription>(Subscription{filter, cb});
  subscriber_list_.Add(*node);
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
