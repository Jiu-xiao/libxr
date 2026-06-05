#include "can.hpp"

using namespace LibXR;

template class LibXR::Callback<const CAN::ClassicPack&>;
template class LibXR::Callback<const CAN::ErrorEvent&>;
template class LibXR::Callback<const FDCAN::FDPack&>;

void CAN::Register(Callback cb, IDFormat format, bool remote, FilterMode mode,
                   uint32_t start_id_mask, uint32_t end_id_mask)
{
  auto node = new LockFreeList::Node<Filter>(
      Filter{mode, start_id_mask, end_id_mask, format, remote, cb});
  subscriber_list_[ClassicBucketIndex(format, remote)].Add(*node);
}

void CAN::RegisterError(ErrorCallback cb)
{
  auto node = new LockFreeList::Node<ErrorSubscriber>(ErrorSubscriber{cb});
  error_subscriber_list_.Add(*node);
}

void CAN::OnMessage(const ClassicPack& pack, bool in_isr)
{
  ASSERT(pack.dlc <= 8u);
  subscriber_list_[ClassicBucketIndex(pack.format, pack.remote)].Foreach<Filter>(
      [&](Filter& node)
      {
        switch (node.mode)
        {
          case FilterMode::ID_MASK:
            if ((pack.id & node.start_id_mask) == node.end_id_mask)
            {
              node.cb.Run(in_isr, pack);
            }
            break;
          case FilterMode::ID_RANGE:
            if (pack.id >= node.start_id_mask && pack.id <= node.end_id_mask)
            {
              node.cb.Run(in_isr, pack);
            }
            break;
        }

        return ErrorCode::OK;
      });
}

void CAN::OnError(const ErrorEvent& event, bool in_isr)
{
  error_subscriber_list_.Foreach<ErrorSubscriber>(
      [&](ErrorSubscriber& node)
      {
        node.cb.Run(in_isr, event);
        return ErrorCode::OK;
      });
}

void FDCAN::Register(CallbackFD cb, IDFormat format, FilterMode mode,
                     uint32_t start_id_mask, uint32_t end_id_mask)
{
  auto node = new LockFreeList::Node<Filter>(
      Filter{mode, start_id_mask, end_id_mask, format, cb});
  subscriber_list_fd_[static_cast<size_t>(format)].Add(*node);
}

void FDCAN::OnMessage(const FDPack& pack, bool in_isr)
{
  ASSERT(pack.len <= 64u);
  subscriber_list_fd_[static_cast<size_t>(pack.format)].Foreach<Filter>(
      [&](Filter& node)
      {
        switch (node.mode)
        {
          case FilterMode::ID_MASK:
            if ((pack.id & node.start_id_mask) == node.end_id_mask)
            {
              node.cb.Run(in_isr, pack);
            }
            break;
          case FilterMode::ID_RANGE:
            if (pack.id >= node.start_id_mask && pack.id <= node.end_id_mask)
            {
              node.cb.Run(in_isr, pack);
            }
            break;
        }

        return ErrorCode::OK;
      });
}
