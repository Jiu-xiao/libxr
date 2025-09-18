#include "ep_pool.hpp"

using namespace LibXR::USB;

EndpointPool::EndpointPool(size_t endpoint_num)
    : LockFreePool<Endpoint*>(endpoint_num - 2)
{
  ASSERT(endpoint_num >= 2);
}

ErrorCode EndpointPool::Get(Endpoint*& ep_info, Endpoint::Direction direction,
                            Endpoint::EPNumber ep_num)
{
  for (uint32_t i = 0; i < SlotCount(); ++i)
  {
    auto& slot_container = (*this)[i];
    auto state = slot_container.slot.state.load(std::memory_order_acquire);
    if (state == SlotState::READY)
    {
      if (ep_num != Endpoint::EPNumber::EP_AUTO &&
          ep_num != slot_container.slot.data->GetNumber())
      {
        continue;  // 如果指定了端点号，则跳过不匹配的
      }

      if (slot_container.slot.data->AvailableDirection() == direction ||
          slot_container.slot.data->AvailableDirection() == Endpoint::Direction::BOTH)
      {
        LockFreePool<Endpoint*>::Get(ep_info, i);
        return ErrorCode::OK;
      }
    }
  }
  return ErrorCode::NOT_FOUND;
}

ErrorCode EndpointPool::Release(Endpoint* ep_info)
{
  for (uint32_t i = 0; i < SlotCount(); ++i)
  {
    auto& slot = (*this)[i];
    auto state = slot.slot.state.load(std::memory_order_acquire);
    if (state == SlotState::RECYCLE)
    {
      if (slot.slot.data == ep_info)
      {
        slot.slot.state.store(SlotState::READY, std::memory_order_release);
        return ErrorCode::OK;
      }
    }

    if (state == SlotState::FREE)
    {
      break;
    }
  }

  return ErrorCode::NOT_FOUND;
}

ErrorCode EndpointPool::FindEndpoint(uint8_t ep_addr, Endpoint*& ans)
{
  Endpoint::Direction direction = Endpoint::Direction::OUT;

  if (ep_addr & 0x80)
  {
    ep_addr &= 0x7F;  // IN端点地址
    direction = Endpoint::Direction::IN;
  }

  for (uint32_t i = 0; i < SlotCount(); ++i)
  {
    auto& slot_container = (*this)[i];
    auto state = slot_container.slot.state.load(std::memory_order_acquire);
    if (state == SlotState::READY &&
        Endpoint::EPNumberToAddr(slot_container.slot.data->GetNumber(),
                                 slot_container.slot.data->GetDirection()) == ep_addr &&
        slot_container.slot.data->GetDirection() == direction)
    {
      ans = slot_container.slot.data;
      return ErrorCode::OK;
    }
    if (state == SlotState::FREE)
    {
      break;  // 没有找到，直接退出
    }
  }

  return ErrorCode::NOT_FOUND;
}

Endpoint* EndpointPool::GetEndpoint0Out() { return ep0_out_; }

Endpoint* EndpointPool::GetEndpoint0In() { return ep0_in_; }

void EndpointPool::SetEndpoint0(Endpoint* ep0_in, Endpoint* ep0_out)
{
  ep0_in_ = ep0_in;
  ep0_out_ = ep0_out;
}
