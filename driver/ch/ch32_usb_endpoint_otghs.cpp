#include <cstdint>

#include "ch32_usb_endpoint.hpp"
#include "ep.hpp"
#include "libxr_time.hpp"
#include "timebase.hpp"

using namespace LibXR;

#if defined(USBHSD)

// NOLINTBEGIN
static inline volatile uint8_t* GetTxControlAddr(USB::Endpoint::EPNumber ep_num)
{
  return reinterpret_cast<volatile uint8_t*>(
      reinterpret_cast<volatile uint8_t*>(&USBHSD->UEP0_TX_CTRL) +
      static_cast<int>(ep_num) * 4);
}

static inline volatile uint8_t* GetRxControlAddr(USB::Endpoint::EPNumber ep_num)
{
  return reinterpret_cast<volatile uint8_t*>(
      reinterpret_cast<volatile uint8_t*>(&USBHSD->UEP0_TX_CTRL) +
      static_cast<int>(ep_num) * 4 + 1);
}

static inline volatile uint16_t* GetTxLenAddr(USB::Endpoint::EPNumber ep_num)
{
  return reinterpret_cast<volatile uint16_t*>(
      reinterpret_cast<volatile uint8_t*>(&USBHSD->UEP0_TX_LEN) +
      static_cast<int>(ep_num) * 4);
}

static_assert(offsetof(USBHSD_TypeDef, UEP1_MAX_LEN) -
                  offsetof(USBHSD_TypeDef, UEP0_MAX_LEN) ==
              4);

static inline volatile uint16_t* GetRxMaxLenAddr(USB::Endpoint::EPNumber ep_num)
{
  return reinterpret_cast<volatile uint16_t*>(
      reinterpret_cast<volatile uint8_t*>(&USBHSD->UEP0_MAX_LEN) +
      static_cast<int>(ep_num) * 4);
}

static inline volatile uint32_t* GetTxDmaAddr(USB::Endpoint::EPNumber ep_num)
{
  if (ep_num == USB::Endpoint::EPNumber::EP0) return &USBHSD->UEP0_DMA;
  return reinterpret_cast<volatile uint32_t*>(
      reinterpret_cast<volatile uint32_t*>(&USBHSD->UEP1_TX_DMA) +
      (static_cast<int>(ep_num) - 1));
}

static inline volatile uint32_t* GetRxDmaAddr(USB::Endpoint::EPNumber ep_num)
{
  if (ep_num == USB::Endpoint::EPNumber::EP0) return &USBHSD->UEP0_DMA;
  return reinterpret_cast<volatile uint32_t*>(
      reinterpret_cast<volatile uint32_t*>(&USBHSD->UEP1_RX_DMA) +
      (static_cast<int>(ep_num) - 1));
}

static void SetTxLen(USB::Endpoint::EPNumber ep_num, uint32_t value)
{
  *GetTxLenAddr(ep_num) = value;
}

static void SetTxDmaBuffer(USB::Endpoint::EPNumber ep_num, void* buffer,
                           uint32_t buffer_size, bool double_buffer)
{
  uint8_t* buf_base = reinterpret_cast<uint8_t*>(buffer);
  uint8_t* buf_alt = buf_base + buffer_size / 2;

  if (ep_num == USB::Endpoint::EPNumber::EP0)
  {
    USBHSD->UEP0_DMA = (uint32_t)buf_base;
  }
  else
  {
    *GetTxDmaAddr(ep_num) = (uint32_t)buf_base;
    if (double_buffer) *GetRxDmaAddr(ep_num) = (uint32_t)buf_alt;
  }

  int idx = static_cast<int>(ep_num);
  if (double_buffer && ep_num != USB::Endpoint::EPNumber::EP0)
    USBHSD->BUF_MODE |= (1 << idx);
  else
    USBHSD->BUF_MODE &= ~(1 << idx);
}

static void SetRxDmaBuffer(USB::Endpoint::EPNumber ep_num, void* buffer,
                           uint32_t buffer_size, bool double_buffer)
{
  uint8_t* buf_base = reinterpret_cast<uint8_t*>(buffer);
  uint8_t* buf_alt = buf_base + buffer_size / 2;

  if (ep_num == USB::Endpoint::EPNumber::EP0)
  {
    USBHSD->UEP0_DMA = (uint32_t)buf_base;
  }
  else
  {
    *GetRxDmaAddr(ep_num) = (uint32_t)buf_base;
    if (double_buffer) *GetTxDmaAddr(ep_num) = (uint32_t)buf_alt;
  }

  int idx = static_cast<int>(ep_num);
  if (double_buffer && ep_num != USB::Endpoint::EPNumber::EP0)
    USBHSD->BUF_MODE |= (1 << idx);
  else
    USBHSD->BUF_MODE &= ~(1 << idx);
}

static void EnableTx(USB::Endpoint::EPNumber ep_num)
{
  switch (ep_num)
  {
    case USB::Endpoint::EPNumber::EP0:
      USBHSD->ENDP_CONFIG |= USBHS_UEP0_T_EN;
      break;
    case USB::Endpoint::EPNumber::EP1:
      USBHSD->ENDP_CONFIG |= USBHS_UEP1_T_EN;
      break;
    case USB::Endpoint::EPNumber::EP2:
      USBHSD->ENDP_CONFIG |= USBHS_UEP2_T_EN;
      break;
    case USB::Endpoint::EPNumber::EP3:
      USBHSD->ENDP_CONFIG |= USBHS_UEP3_T_EN;
      break;
    case USB::Endpoint::EPNumber::EP4:
      USBHSD->ENDP_CONFIG |= USBHS_UEP4_T_EN;
      break;
    case USB::Endpoint::EPNumber::EP5:
      USBHSD->ENDP_CONFIG |= USBHS_UEP5_T_EN;
      break;
    case USB::Endpoint::EPNumber::EP6:
      USBHSD->ENDP_CONFIG |= USBHS_UEP6_T_EN;
      break;
    case USB::Endpoint::EPNumber::EP7:
      USBHSD->ENDP_CONFIG |= USBHS_UEP7_T_EN;
      break;
    case USB::Endpoint::EPNumber::EP8:
      USBHSD->ENDP_CONFIG |= USBHS_UEP8_T_EN;
      break;
    case USB::Endpoint::EPNumber::EP9:
      USBHSD->ENDP_CONFIG |= USBHS_UEP9_T_EN;
      break;
    case USB::Endpoint::EPNumber::EP10:
      USBHSD->ENDP_CONFIG |= USBHS_UEP10_T_EN;
      break;
    case USB::Endpoint::EPNumber::EP11:
      USBHSD->ENDP_CONFIG |= USBHS_UEP11_T_EN;
      break;
    case USB::Endpoint::EPNumber::EP12:
      USBHSD->ENDP_CONFIG |= USBHS_UEP12_T_EN;
      break;
    case USB::Endpoint::EPNumber::EP13:
      USBHSD->ENDP_CONFIG |= USBHS_UEP13_T_EN;
      break;
    case USB::Endpoint::EPNumber::EP14:
      USBHSD->ENDP_CONFIG |= USBHS_UEP14_T_EN;
      break;
    case USB::Endpoint::EPNumber::EP15:
      USBHSD->ENDP_CONFIG |= USBHS_UEP15_T_EN;
      break;
    default:
      break;
  }
}

static void DisableTx(USB::Endpoint::EPNumber ep_num)
{
  switch (ep_num)
  {
    case USB::Endpoint::EPNumber::EP0:
      USBHSD->ENDP_CONFIG &= ~USBHS_UEP0_T_EN;
      break;
    case USB::Endpoint::EPNumber::EP1:
      USBHSD->ENDP_CONFIG &= ~USBHS_UEP1_T_EN;
      break;
    case USB::Endpoint::EPNumber::EP2:
      USBHSD->ENDP_CONFIG &= ~USBHS_UEP2_T_EN;
      break;
    case USB::Endpoint::EPNumber::EP3:
      USBHSD->ENDP_CONFIG &= ~USBHS_UEP3_T_EN;
      break;
    case USB::Endpoint::EPNumber::EP4:
      USBHSD->ENDP_CONFIG &= ~USBHS_UEP4_T_EN;
      break;
    case USB::Endpoint::EPNumber::EP5:
      USBHSD->ENDP_CONFIG &= ~USBHS_UEP5_T_EN;
      break;
    case USB::Endpoint::EPNumber::EP6:
      USBHSD->ENDP_CONFIG &= ~USBHS_UEP6_T_EN;
      break;
    case USB::Endpoint::EPNumber::EP7:
      USBHSD->ENDP_CONFIG &= ~USBHS_UEP7_T_EN;
      break;
    case USB::Endpoint::EPNumber::EP8:
      USBHSD->ENDP_CONFIG &= ~USBHS_UEP8_T_EN;
      break;
    case USB::Endpoint::EPNumber::EP9:
      USBHSD->ENDP_CONFIG &= ~USBHS_UEP9_T_EN;
      break;
    case USB::Endpoint::EPNumber::EP10:
      USBHSD->ENDP_CONFIG &= ~USBHS_UEP10_T_EN;
      break;
    case USB::Endpoint::EPNumber::EP11:
      USBHSD->ENDP_CONFIG &= ~USBHS_UEP11_T_EN;
      break;
    case USB::Endpoint::EPNumber::EP12:
      USBHSD->ENDP_CONFIG &= ~USBHS_UEP12_T_EN;
      break;
    case USB::Endpoint::EPNumber::EP13:
      USBHSD->ENDP_CONFIG &= ~USBHS_UEP13_T_EN;
      break;
    case USB::Endpoint::EPNumber::EP14:
      USBHSD->ENDP_CONFIG &= ~USBHS_UEP14_T_EN;
      break;
    case USB::Endpoint::EPNumber::EP15:
      USBHSD->ENDP_CONFIG &= ~USBHS_UEP15_T_EN;
      break;
    default:
      break;
  }
}

static void EnableRx(USB::Endpoint::EPNumber ep_num)
{
  switch (ep_num)
  {
    case USB::Endpoint::EPNumber::EP0:
      USBHSD->ENDP_CONFIG |= USBHS_UEP0_R_EN;
      break;
    case USB::Endpoint::EPNumber::EP1:
      USBHSD->ENDP_CONFIG |= USBHS_UEP1_R_EN;
      break;
    case USB::Endpoint::EPNumber::EP2:
      USBHSD->ENDP_CONFIG |= USBHS_UEP2_R_EN;
      break;
    case USB::Endpoint::EPNumber::EP3:
      USBHSD->ENDP_CONFIG |= USBHS_UEP3_R_EN;
      break;
    case USB::Endpoint::EPNumber::EP4:
      USBHSD->ENDP_CONFIG |= USBHS_UEP4_R_EN;
      break;
    case USB::Endpoint::EPNumber::EP5:
      USBHSD->ENDP_CONFIG |= USBHS_UEP5_R_EN;
      break;
    case USB::Endpoint::EPNumber::EP6:
      USBHSD->ENDP_CONFIG |= USBHS_UEP6_R_EN;
      break;
    case USB::Endpoint::EPNumber::EP7:
      USBHSD->ENDP_CONFIG |= USBHS_UEP7_R_EN;
      break;
    case USB::Endpoint::EPNumber::EP8:
      USBHSD->ENDP_CONFIG |= USBHS_UEP8_R_EN;
      break;
    case USB::Endpoint::EPNumber::EP9:
      USBHSD->ENDP_CONFIG |= USBHS_UEP9_R_EN;
      break;
    case USB::Endpoint::EPNumber::EP10:
      USBHSD->ENDP_CONFIG |= USBHS_UEP10_R_EN;
      break;
    case USB::Endpoint::EPNumber::EP11:
      USBHSD->ENDP_CONFIG |= USBHS_UEP11_R_EN;
      break;
    case USB::Endpoint::EPNumber::EP12:
      USBHSD->ENDP_CONFIG |= USBHS_UEP12_R_EN;
      break;
    case USB::Endpoint::EPNumber::EP13:
      USBHSD->ENDP_CONFIG |= USBHS_UEP13_R_EN;
      break;
    case USB::Endpoint::EPNumber::EP14:
      USBHSD->ENDP_CONFIG |= USBHS_UEP14_R_EN;
      break;
    case USB::Endpoint::EPNumber::EP15:
      USBHSD->ENDP_CONFIG |= USBHS_UEP15_R_EN;
      break;
    default:
      break;
  }
}

static void DisableRx(USB::Endpoint::EPNumber ep_num)
{
  switch (ep_num)
  {
    case USB::Endpoint::EPNumber::EP0:
      USBHSD->ENDP_CONFIG &= ~USBHS_UEP0_R_EN;
      break;
    case USB::Endpoint::EPNumber::EP1:
      USBHSD->ENDP_CONFIG &= ~USBHS_UEP1_R_EN;
      break;
    case USB::Endpoint::EPNumber::EP2:
      USBHSD->ENDP_CONFIG &= ~USBHS_UEP2_R_EN;
      break;
    case USB::Endpoint::EPNumber::EP3:
      USBHSD->ENDP_CONFIG &= ~USBHS_UEP3_R_EN;
      break;
    case USB::Endpoint::EPNumber::EP4:
      USBHSD->ENDP_CONFIG &= ~USBHS_UEP4_R_EN;
      break;
    case USB::Endpoint::EPNumber::EP5:
      USBHSD->ENDP_CONFIG &= ~USBHS_UEP5_R_EN;
      break;
    case USB::Endpoint::EPNumber::EP6:
      USBHSD->ENDP_CONFIG &= ~USBHS_UEP6_R_EN;
      break;
    case USB::Endpoint::EPNumber::EP7:
      USBHSD->ENDP_CONFIG &= ~USBHS_UEP7_R_EN;
      break;
    case USB::Endpoint::EPNumber::EP8:
      USBHSD->ENDP_CONFIG &= ~USBHS_UEP8_R_EN;
      break;
    case USB::Endpoint::EPNumber::EP9:
      USBHSD->ENDP_CONFIG &= ~USBHS_UEP9_R_EN;
      break;
    case USB::Endpoint::EPNumber::EP10:
      USBHSD->ENDP_CONFIG &= ~USBHS_UEP10_R_EN;
      break;
    case USB::Endpoint::EPNumber::EP11:
      USBHSD->ENDP_CONFIG &= ~USBHS_UEP11_R_EN;
      break;
    case USB::Endpoint::EPNumber::EP12:
      USBHSD->ENDP_CONFIG &= ~USBHS_UEP12_R_EN;
      break;
    case USB::Endpoint::EPNumber::EP13:
      USBHSD->ENDP_CONFIG &= ~USBHS_UEP13_R_EN;
      break;
    case USB::Endpoint::EPNumber::EP14:
      USBHSD->ENDP_CONFIG &= ~USBHS_UEP14_R_EN;
      break;
    case USB::Endpoint::EPNumber::EP15:
      USBHSD->ENDP_CONFIG &= ~USBHS_UEP15_R_EN;
      break;
    default:
      break;
  }
}
// NOLINTEND

CH32EndpointOtgHs::CH32EndpointOtgHs(EPNumber ep_num, Direction dir,
                                     LibXR::RawData buffer, bool double_buffer)
    : Endpoint(ep_num, dir, buffer), hw_double_buffer_(double_buffer), dma_buffer_(buffer)
{
  map_otg_hs_[EPNumberToInt8(GetNumber())][static_cast<uint8_t>(dir)] = this;

  if (dir == Direction::IN)
  {
    SetTxDmaBuffer(GetNumber(), dma_buffer_.addr_, dma_buffer_.size_, double_buffer);
  }
  else
  {
    SetRxDmaBuffer(GetNumber(), dma_buffer_.addr_, dma_buffer_.size_, double_buffer);
  }

  if (dir == Direction::IN)
  {
    SetTxLen(GetNumber(), 0);
    *GetTxControlAddr(GetNumber()) = USBHS_UEP_T_RES_NAK;
  }
  else
  {
    *GetRxControlAddr(GetNumber()) = USBHS_UEP_R_RES_NAK;
  }
}

void CH32EndpointOtgHs::Configure(const Config& cfg)
{
  auto& ep_cfg = GetConfig();
  ep_cfg = cfg;

  const int ep_idx = EPNumberToInt8(GetNumber());

  const uint8_t in_idx = static_cast<uint8_t>(Direction::IN);
  const uint8_t out_idx = static_cast<uint8_t>(Direction::OUT);

  const bool has_in = (map_otg_hs_[ep_idx][in_idx] != nullptr);
  const bool has_out = (map_otg_hs_[ep_idx][out_idx] != nullptr);

  // 双缓冲策略：EP0 禁止双缓冲；若硬件配置为双缓冲则开启
  bool enable_double = (GetNumber() != EPNumber::EP0) && hw_double_buffer_;
  if (enable_double && has_in && has_out)
  {
    ASSERT(false);  // 双缓冲模式下端点只能单方向
    enable_double = false;
  }
  ep_cfg.double_buffer = enable_double;

  // 限制 max_packet_size 不超过 buffer
  if (ep_cfg.max_packet_size > GetBuffer().size_)
  {
    ep_cfg.max_packet_size = GetBuffer().size_;
  }

  if (GetDirection() == Direction::IN)
  {
    *GetTxControlAddr(GetNumber()) = USBHS_UEP_T_RES_NAK;
    SetTxLen(GetNumber(), 0);
  }
  else
  {
    *GetRxControlAddr(GetNumber()) = USBHS_UEP_R_RES_NAK;

    if (GetNumber() != EPNumber::EP0)
    {
      *GetRxMaxLenAddr(GetNumber()) = ep_cfg.max_packet_size;
    }
  }

  const int idx = static_cast<int>(GetNumber());
  if (GetDirection() == Direction::IN)
  {
    if (GetType() == Type::ISOCHRONOUS)
      USBHSD->ENDP_TYPE |= (USBHS_UEP0_T_TYPE << idx);
    else
      USBHSD->ENDP_TYPE &= ~(USBHS_UEP0_T_TYPE << idx);
  }
  else
  {
    if (GetType() == Type::ISOCHRONOUS)
      USBHSD->ENDP_TYPE |= (USBHS_UEP0_R_TYPE << idx);
    else
      USBHSD->ENDP_TYPE &= ~(USBHS_UEP0_R_TYPE << idx);
  }

  if (GetDirection() == Direction::IN)
  {
    if (GetType() != Type::ISOCHRONOUS)
      EnableTx(GetNumber());
    else
      DisableTx(GetNumber());

    if (!has_out)
    {
      DisableRx(GetNumber());
    }

    SetTxDmaBuffer(GetNumber(), dma_buffer_.addr_, dma_buffer_.size_, enable_double);
  }
  else
  {
    EnableRx(GetNumber());

    if (!has_in)
    {
      DisableTx(GetNumber());
    }

    SetRxDmaBuffer(GetNumber(), dma_buffer_.addr_, dma_buffer_.size_, enable_double);
  }

  SetState(State::IDLE);
}

void CH32EndpointOtgHs::Close()
{
  DisableTx(GetNumber());
  DisableRx(GetNumber());

  *GetTxControlAddr(GetNumber()) = USBHS_UEP_T_RES_NAK;
  *GetRxControlAddr(GetNumber()) = USBHS_UEP_R_RES_NAK;

  SetState(State::DISABLED);
}

ErrorCode CH32EndpointOtgHs::Transfer(size_t size)
{
  if (GetState() == State::BUSY)
  {
    return ErrorCode::BUSY;
  }

  auto buffer = GetBuffer();
  if (buffer.size_ < size)
  {
    return ErrorCode::NO_BUFF;
  }

  bool is_in = (GetDirection() == Direction::IN);

  if (is_in && UseDoubleBuffer() && GetType() != Type::ISOCHRONOUS)
  {
    SwitchBuffer();
  }

  if (GetNumber() == EPNumber::EP0)
  {
    if (size == 0)
    {
      tog0_ = true;
      tog1_ = false;
    }
  }

  last_transfer_size_ = size;
  SetState(State::BUSY);

  if (is_in)
  {
    if (GetType() == Type::ISOCHRONOUS)
    {
      EnableTx(GetNumber());
    }

    SetTxLen(GetNumber(), size);
    auto addr = GetTxControlAddr(GetNumber());

    if (GetType() != Type::ISOCHRONOUS)
    {
      *addr = USBHS_UEP_T_RES_ACK |
              (*addr & (~(USBHS_UEP_T_RES_MASK | USBHS_UEP_T_TOG_MDATA))) |
              (tog0_ ? USBHS_UEP_T_TOG_DATA1 : 0);
    }
    else
    {
      *addr = (uint8_t)((*addr & ~(USBHS_UEP_T_RES_MASK | USBHS_UEP_T_TOG_MASK)) |
                        USBHS_UEP_T_TOG_AUTO);
    }
  }
  else
  {
    auto addr = GetRxControlAddr(GetNumber());

    if (GetType() != Type::ISOCHRONOUS)
    {
      *addr = USBHS_UEP_R_RES_ACK |
              (*addr & (~(USBHS_UEP_R_RES_MASK | USBHS_UEP_R_TOG_MDATA))) |
              (tog0_ ? USBHS_UEP_R_TOG_DATA1 : 0);
    }
    else
    {
      *addr = USBHS_UEP_R_RES_ACK |
              (*addr & (~(USBHS_UEP_R_RES_MASK | USBHS_UEP_R_TOG_MDATA)));
    }
  }

  if (GetNumber() == EPNumber::EP0)
  {
    tog0_ = !tog0_;
  }

  return ErrorCode::OK;
}

ErrorCode CH32EndpointOtgHs::Stall()
{
  if (GetState() != State::IDLE)
  {
    return ErrorCode::BUSY;
  }

  bool is_in = (GetDirection() == Direction::IN);
  if (is_in)
  {
    *GetTxControlAddr(GetNumber()) |= USBHS_UEP_T_RES_STALL;
  }
  else
  {
    *GetRxControlAddr(GetNumber()) |= USBHS_UEP_R_RES_STALL;
  }
  SetState(State::STALLED);
  return ErrorCode::OK;
}

ErrorCode CH32EndpointOtgHs::ClearStall()
{
  if (GetState() != State::STALLED)
  {
    return ErrorCode::FAILED;
  }

  bool is_in = (GetDirection() == Direction::IN);
  if (is_in)
  {
    *GetTxControlAddr(GetNumber()) &= ~USBHS_UEP_T_RES_STALL;
  }
  else
  {
    *GetRxControlAddr(GetNumber()) &= ~USBHS_UEP_R_RES_STALL;
  }
  SetState(State::IDLE);
  return ErrorCode::OK;
}

void CH32EndpointOtgHs::TransferComplete(size_t size)
{
  const bool is_in = (GetDirection() == Direction::IN);
  const bool is_out = !is_in;
  const bool is_ep0 = (GetNumber() == EPNumber::EP0);
  const bool is_iso = (GetType() == Type::ISOCHRONOUS);

  // UIF_TRANSFER / INT_FG 由 IRQ handler 统一在“本次处理结束后”清除；

  if (is_in)
  {
    auto* tx_ctrl = GetTxControlAddr(GetNumber());
    *tx_ctrl = (*tx_ctrl & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_NAK;

    size = last_transfer_size_;

    if (is_iso)
    {
      SetTxLen(GetNumber(), 0);
      DisableTx(GetNumber());
    }
  }
  else
  {
    // 非 EP0 的 OUT：收尾置 NAK
    if (!is_ep0)
    {
      auto* rx_ctrl = GetRxControlAddr(GetNumber());
      *rx_ctrl = (*rx_ctrl & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_NAK;
    }
  }

  // 若 TOG 不 OK，说明数据同步失败
  if (is_out)
  {
    const bool tog_ok =
        ((USBHSD->INT_ST & USBHS_UIS_TOG_OK) == USBHS_UIS_TOG_OK);  // NOLINT
    if (!tog_ok)
    {
      SetState(State::IDLE);
      (void)Transfer(last_transfer_size_);
      return;
    }
  }

  // 成功：更新软件 data toggle
  if (GetState() == State::BUSY && !is_ep0 && !is_iso)
  {
    tog0_ = !tog0_;
  }

  if (is_ep0 && is_out)
  {
    tog0_ = true;
    tog1_ = false;
    *GetRxControlAddr(GetNumber()) = USBHS_UEP_R_RES_ACK;
  }

  OnTransferCompleteCallback(true, size);
}

void CH32EndpointOtgHs::SwitchBuffer()
{
  if (GetDirection() == Direction::IN)
  {
    SetActiveBlock(!tog0_);
  }
  else
  {
    SetActiveBlock(tog0_);
  }
}

#endif