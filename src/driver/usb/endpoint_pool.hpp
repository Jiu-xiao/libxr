#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "endpoint.hpp"
#include "lockfree_pool.hpp"

namespace LibXR::USB
{

/**
 * @brief USB端点池 = LockFreePool<Endpoint*> 的专用派生
 *        增加端点号查找、遍历等常用管理接口
 */
class EndpointPool : protected LockFreePool<Endpoint*>
{
 public:
  using LockFreePool<Endpoint*>::Put;

  EndpointPool(size_t endpoint_num) : LockFreePool<Endpoint*>(endpoint_num - 2)
  {
    ASSERT(endpoint_num >= 2);
  }

  ErrorCode Get(Endpoint*& ep_info, Endpoint::Direction direction, size_t ep_num = 0)
  {
    for (uint32_t i = 0; i < SlotCount(); ++i)
    {
      auto& slot_container = (*this)[i];
      auto state = slot_container.slot.state.load(std::memory_order_acquire);
      if (state == SlotState::READY)
      {
        if (ep_num != 0 && ep_num != slot_container.slot.data->Number())
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

  ErrorCode Release(Endpoint* ep_info)
  {
    for (int i = 0; i < SlotCount(); ++i)
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

  ErrorCode GetEndpoint(uint8_t ep_addr, Endpoint*& ans)
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
      if (state == SlotState::READY && slot_container.slot.data->Number() == ep_addr &&
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

  Endpoint* GetEndpoint0Out() { return ep0_out_; }

  Endpoint* GetEndpoint0In() { return ep0_in_; }

  void SetEndpoint0(Endpoint* ep0_in, Endpoint* ep0_out)
  {
    ep0_in_ = ep0_in;
    ep0_out_ = ep0_out;
  }

 private:
  Endpoint* ep0_in_ = nullptr;
  Endpoint* ep0_out_ = nullptr;
};

}  // namespace LibXR::USB
