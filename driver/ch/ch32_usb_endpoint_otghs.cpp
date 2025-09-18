#include <cstdint>

#include "ch32_usb_endpoint.hpp"
#include "ch32v30x.h"
#include "ch32v30x_usb.h"
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

  // EP0 特殊
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

  if (GetNumber() != EPNumber::EP0 && hw_double_buffer_)
  {
    ep_cfg.double_buffer = true;
  }
  else
  {
    if (ep_cfg.double_buffer)
    {
      // Please enable double buffer for this endpoint
      ASSERT(false);
    }
    ep_cfg.double_buffer = false;
  }

  if (ep_cfg.max_packet_size > GetBuffer().size_)
  {
    ep_cfg.max_packet_size = GetBuffer().size_;
  }

  *GetRxControlAddr(GetNumber()) = USBHS_UEP_R_RES_NAK;
  *GetTxControlAddr(GetNumber()) = USBHS_UEP_T_RES_NAK;

  SetTxLen(GetNumber(), 0);

  if (GetDirection() == Direction::IN)
  {
    EnableTx(GetNumber());
    DisableRx(GetNumber());
    SetTxDmaBuffer(GetNumber(), dma_buffer_.addr_, dma_buffer_.size_, hw_double_buffer_);
  }
  else
  {
    DisableTx(GetNumber());
    EnableRx(GetNumber());
    SetRxDmaBuffer(GetNumber(), dma_buffer_.addr_, dma_buffer_.size_, hw_double_buffer_);
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

  if (is_in && UseDoubleBuffer())
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

  SetLastTransferSize(size);
  SetState(State::BUSY);

  if (is_in)
  {
    SetTxLen(GetNumber(), size);
    auto addr = GetTxControlAddr(GetNumber());

    *addr = USBHS_UEP_T_RES_ACK |
            (*addr & (~(USBHS_UEP_T_RES_MASK | USBHS_UEP_T_TOG_MDATA))) |
            (tog0_ ? USBHS_UEP_T_TOG_DATA1 : 0);
  }
  else
  {
    auto addr = GetRxControlAddr(GetNumber());

    *addr = USBHS_UEP_R_RES_ACK |
            (*addr & (~(USBHS_UEP_R_RES_MASK | USBHS_UEP_R_TOG_MDATA))) |
            (tog0_ ? USBHS_UEP_R_TOG_DATA1 : 0);
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
  if (GetState() == State::BUSY && GetNumber() != EPNumber::EP0)
  {
    tog0_ = !tog0_;
  }

  if (GetDirection() == Direction::IN)
  {
    *GetTxControlAddr(GetNumber()) =
        (*GetTxControlAddr(GetNumber()) & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_NAK;

    USBHSD->INT_FG = USBHS_UIF_TRANSFER;  // NOLINT

    size = GetLastTransferSize();
  }

  if (GetDirection() == Direction::OUT &&
      (USBHSD->INT_ST & USBHS_UIS_TOG_OK) != USBHS_UIS_TOG_OK)  // NOLINT
  {
    return;
  }

  if (GetNumber() == EPNumber::EP0 && GetDirection() == Direction::OUT)
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
