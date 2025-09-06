#include <cstdint>

#include "ch32_usb_endpoint.hpp"
#include "ch32v30x.h"
#include "ch32v30x_usb.h"
#include "ep.hpp"

using namespace LibXR;

// NOLINTBEGIN
static volatile uint8_t* GetTxControlAddr(USB::Endpoint::EPNumber ep_num)
{
  switch (ep_num)
  {
    case USB::Endpoint::EPNumber::EP0:
      return &(USBFSD->UEP0_TX_CTRL);
    case USB::Endpoint::EPNumber::EP1:
      return &(USBFSD->UEP1_TX_CTRL);
    case USB::Endpoint::EPNumber::EP2:
      return &(USBFSD->UEP2_TX_CTRL);
    case USB::Endpoint::EPNumber::EP3:
      return &(USBFSD->UEP3_TX_CTRL);
    case USB::Endpoint::EPNumber::EP4:
      return &(USBFSD->UEP4_TX_CTRL);
    case USB::Endpoint::EPNumber::EP5:
      return &(USBFSD->UEP5_TX_CTRL);
    case USB::Endpoint::EPNumber::EP6:
      return &(USBFSD->UEP6_TX_CTRL);
    case USB::Endpoint::EPNumber::EP7:
      return &(USBFSD->UEP7_TX_CTRL);
    default:
      break;
  }
  return nullptr;
}

static volatile uint8_t* GetRxControlAddr(USB::Endpoint::EPNumber ep_num)
{
  switch (ep_num)
  {
    case USB::Endpoint::EPNumber::EP0:
      return &(USBFSD->UEP0_RX_CTRL);
    case USB::Endpoint::EPNumber::EP1:
      return &(USBFSD->UEP1_RX_CTRL);
    case USB::Endpoint::EPNumber::EP2:
      return &(USBFSD->UEP2_RX_CTRL);
    case USB::Endpoint::EPNumber::EP3:
      return &(USBFSD->UEP3_RX_CTRL);
    case USB::Endpoint::EPNumber::EP4:
      return &(USBFSD->UEP4_RX_CTRL);
    case USB::Endpoint::EPNumber::EP5:
      return &(USBFSD->UEP5_RX_CTRL);
    case USB::Endpoint::EPNumber::EP6:
      return &(USBFSD->UEP6_RX_CTRL);
    case USB::Endpoint::EPNumber::EP7:
      return &(USBFSD->UEP7_RX_CTRL);
    default:
      break;
  }
  return nullptr;
}

static void SetDmaBuffer(USB::Endpoint::EPNumber ep_num, void* value)
{
  switch (ep_num)
  {
    case USB::Endpoint::EPNumber::EP0:
      USBFSD->UEP0_DMA = (uint32_t)value;
      break;
    case USB::Endpoint::EPNumber::EP1:
      USBFSD->UEP1_DMA = (uint32_t)value;
      USBFSD->UEP4_1_MOD |= USBFS_UEP1_BUF_MOD;
      break;
    case USB::Endpoint::EPNumber::EP2:
      USBFSD->UEP2_DMA = (uint32_t)value;
      USBFSD->UEP2_3_MOD |= USBFS_UEP2_BUF_MOD;
      break;
    case USB::Endpoint::EPNumber::EP3:
      USBFSD->UEP3_DMA = (uint32_t)value;
      USBFSD->UEP2_3_MOD |= USBFS_UEP3_BUF_MOD;
      break;
    case USB::Endpoint::EPNumber::EP4:
      USBFSD->UEP4_DMA = (uint32_t)value;
      USBFSD->UEP4_1_MOD |= USBFS_UEP4_BUF_MOD;
      break;
    case USB::Endpoint::EPNumber::EP5:
      USBFSD->UEP5_DMA = (uint32_t)value;
      USBFSD->UEP5_6_MOD |= USBFS_UEP5_BUF_MOD;
      break;
    case USB::Endpoint::EPNumber::EP6:
      USBFSD->UEP6_DMA = (uint32_t)value;
      USBFSD->UEP5_6_MOD |= USBFS_UEP6_BUF_MOD;
      break;
    case USB::Endpoint::EPNumber::EP7:
      USBFSD->UEP7_DMA = (uint32_t)value;
      USBFSD->UEP7_MOD |= USBFS_UEP7_BUF_MOD;
      break;
    default:
      break;
  }
}

static void SetTxLen(USB::Endpoint::EPNumber ep_num, uint32_t value)
{
  switch (ep_num)
  {
    case USB::Endpoint::EPNumber::EP0:
      USBFSD->UEP0_TX_LEN = value;
      break;
    case USB::Endpoint::EPNumber::EP1:
      USBFSD->UEP1_TX_LEN = value;
      break;
    case USB::Endpoint::EPNumber::EP2:
      USBFSD->UEP2_TX_LEN = value;
      break;
    case USB::Endpoint::EPNumber::EP3:
      USBFSD->UEP3_TX_LEN = value;
      break;
    case USB::Endpoint::EPNumber::EP4:
      USBFSD->UEP4_TX_LEN = value;
      break;
    case USB::Endpoint::EPNumber::EP5:
      USBFSD->UEP5_TX_LEN = value;
      break;
    case USB::Endpoint::EPNumber::EP6:
      USBFSD->UEP6_TX_LEN = value;
      break;
    case USB::Endpoint::EPNumber::EP7:
      USBFSD->UEP7_TX_LEN = value;
      break;
    default:
      break;
  }
}

static void EnableTx(USB::Endpoint::EPNumber ep_num)
{
  switch (ep_num)
  {
    case USB::Endpoint::EPNumber::EP0:
      break;
    case USB::Endpoint::EPNumber::EP1:
      USBFSD->UEP4_1_MOD |= USBFS_UEP1_TX_EN;
      break;
    case USB::Endpoint::EPNumber::EP2:
      USBFSD->UEP2_3_MOD |= USBFS_UEP2_TX_EN;
      break;
    case USB::Endpoint::EPNumber::EP3:
      USBFSD->UEP2_3_MOD |= USBFS_UEP3_TX_EN;
      break;
    case USB::Endpoint::EPNumber::EP4:
      USBFSD->UEP4_1_MOD |= USBFS_UEP4_TX_EN;
      break;
    case USB::Endpoint::EPNumber::EP5:
      USBFSD->UEP5_6_MOD |= USBFS_UEP5_TX_EN;
      break;
    case USB::Endpoint::EPNumber::EP6:
      USBFSD->UEP5_6_MOD |= USBFS_UEP6_TX_EN;
      break;
    case USB::Endpoint::EPNumber::EP7:
      USBFSD->UEP7_MOD |= USBFS_UEP7_TX_EN;
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
      break;
    case USB::Endpoint::EPNumber::EP1:
      USBFSD->UEP4_1_MOD &= ~USBFS_UEP1_TX_EN;
      break;
    case USB::Endpoint::EPNumber::EP2:
      USBFSD->UEP2_3_MOD &= ~USBFS_UEP2_TX_EN;
      break;
    case USB::Endpoint::EPNumber::EP3:
      USBFSD->UEP2_3_MOD &= ~USBFS_UEP3_TX_EN;
      break;
    case USB::Endpoint::EPNumber::EP4:
      USBFSD->UEP4_1_MOD &= ~USBFS_UEP4_TX_EN;
      break;
    case USB::Endpoint::EPNumber::EP5:
      USBFSD->UEP5_6_MOD &= ~USBFS_UEP5_TX_EN;
      break;
    case USB::Endpoint::EPNumber::EP6:
      USBFSD->UEP5_6_MOD &= ~USBFS_UEP6_TX_EN;
      break;
    case USB::Endpoint::EPNumber::EP7:
      USBFSD->UEP7_MOD &= ~USBFS_UEP7_TX_EN;
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
      break;
    case USB::Endpoint::EPNumber::EP1:
      USBFSD->UEP4_1_MOD |= USBFS_UEP1_RX_EN;
      break;
    case USB::Endpoint::EPNumber::EP2:
      USBFSD->UEP2_3_MOD |= USBFS_UEP2_RX_EN;
      break;
    case USB::Endpoint::EPNumber::EP3:
      USBFSD->UEP2_3_MOD |= USBFS_UEP3_RX_EN;
      break;
    case USB::Endpoint::EPNumber::EP4:
      USBFSD->UEP4_1_MOD |= USBFS_UEP4_RX_EN;
      break;
    case USB::Endpoint::EPNumber::EP5:
      USBFSD->UEP5_6_MOD |= USBFS_UEP5_RX_EN;
      break;
    case USB::Endpoint::EPNumber::EP6:
      USBFSD->UEP5_6_MOD |= USBFS_UEP6_RX_EN;
      break;
    case USB::Endpoint::EPNumber::EP7:
      USBFSD->UEP7_MOD |= USBFS_UEP7_RX_EN;
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
      break;
    case USB::Endpoint::EPNumber::EP1:
      USBFSD->UEP4_1_MOD &= ~USBFS_UEP1_RX_EN;
      break;
    case USB::Endpoint::EPNumber::EP2:
      USBFSD->UEP2_3_MOD &= ~USBFS_UEP2_RX_EN;
      break;
    case USB::Endpoint::EPNumber::EP3:
      USBFSD->UEP2_3_MOD &= ~USBFS_UEP3_RX_EN;
      break;
    case USB::Endpoint::EPNumber::EP4:
      USBFSD->UEP4_1_MOD &= ~USBFS_UEP4_RX_EN;
      break;
    case USB::Endpoint::EPNumber::EP5:
      USBFSD->UEP5_6_MOD &= ~USBFS_UEP5_RX_EN;
      break;
    case USB::Endpoint::EPNumber::EP6:
      USBFSD->UEP5_6_MOD &= ~USBFS_UEP6_RX_EN;
      break;
    case USB::Endpoint::EPNumber::EP7:
      USBFSD->UEP7_MOD &= ~USBFS_UEP7_RX_EN;
      break;
    default:
      break;
  }
}
// NOLINTEND

// NOLINTNEXTLINE
static LibXR::RawData SelectBuffer(USB::Endpoint::EPNumber ep_num,
                                   USB::Endpoint::Direction dir,
                                   const LibXR::RawData& buffer)
{
  if (ep_num == USB::Endpoint::EPNumber::EP0)
  {
    return buffer;
  }
  else
  {
    if (dir == USB::Endpoint::Direction::OUT)
    {
      return LibXR::RawData(buffer.addr_, 128);
    }
    else
    {
      return LibXR::RawData(reinterpret_cast<uint8_t*>(buffer.addr_) + 128, 128);
    }
  }
}

CH32EndpointOtgFs::CH32EndpointOtgFs(EPNumber ep_num, Direction dir,
                                     LibXR::RawData buffer)
    : Endpoint(ep_num, dir, SelectBuffer(ep_num, dir, buffer)), dma_buffer_(buffer)
{
  map_otg_fs_[EPNumberToInt8(GetNumber())][static_cast<uint8_t>(dir)] = this;

  SetDmaBuffer(GetNumber(), dma_buffer_.addr_);

  if (dir == Direction::IN)
  {
    SetTxLen(GetNumber(), 0);
    *GetTxControlAddr(GetNumber()) = USBFS_UEP_T_RES_NAK;
  }
  else
  {
    *GetRxControlAddr(GetNumber()) = USBFS_UEP_R_RES_NAK;
  }
}

void CH32EndpointOtgFs::Configure(const Config& cfg)
{
  auto& ep_cfg = GetConfig();
  ep_cfg = cfg;
  ep_cfg.max_packet_size = 64;

  if (GetNumber() != EPNumber::EP0)
  {
    ep_cfg.double_buffer = true;
  }
  else
  {
    ep_cfg.double_buffer = false;
  }

  *GetRxControlAddr(GetNumber()) = USBFS_UEP_R_RES_NAK | USBFS_UEP_R_AUTO_TOG;
  *GetTxControlAddr(GetNumber()) = USBFS_UEP_T_RES_NAK | USBFS_UEP_T_AUTO_TOG;

  SetTxLen(GetNumber(), 0);

  EnableTx(GetNumber());
  EnableRx(GetNumber());
  SetDmaBuffer(GetNumber(), dma_buffer_.addr_);

  SetState(State::IDLE);
}

void CH32EndpointOtgFs::Close()
{
  DisableTx(GetNumber());
  DisableRx(GetNumber());

  *GetTxControlAddr(GetNumber()) = USBFS_UEP_T_RES_NAK;
  *GetRxControlAddr(GetNumber()) = USBFS_UEP_R_RES_NAK;

  SetState(State::DISABLED);
}

ErrorCode CH32EndpointOtgFs::Transfer(size_t size)
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

  if (is_in)
  {
    SetTxLen(GetNumber(), size);
    auto addr = GetTxControlAddr(GetNumber());

    if (GetNumber() != EPNumber::EP0)
    {
      *addr = USBFS_UEP_T_RES_ACK | (*addr & (~USBFS_UEP_T_RES_MASK));
    }
    else
    {
      *addr = USBFS_UEP_T_RES_ACK | (tog_ ? USBFS_UEP_T_TOG : 0);
    }
  }
  else
  {
    auto addr = GetRxControlAddr(GetNumber());

    if (GetNumber() != EPNumber::EP0)
    {
      *addr = USBFS_UEP_R_RES_ACK | (*addr & (~USBFS_UEP_R_RES_MASK));
    }
    else
    {
      *addr = USBFS_UEP_R_RES_ACK | (tog_ ? USBFS_UEP_R_TOG : 0);
    }
  }

  if (GetNumber() == EPNumber::EP0)
  {
    tog_ = !tog_;
  }

  SetLastTransferSize(size);
  SetState(State::BUSY);
  return ErrorCode::OK;
}

ErrorCode CH32EndpointOtgFs::Stall()
{
  if (GetState() != State::IDLE)
  {
    return ErrorCode::BUSY;
  }

  bool is_in = (GetDirection() == Direction::IN);
  if (is_in)
  {
    *GetTxControlAddr(GetNumber()) |= USBFS_UEP_T_RES_STALL;
  }
  else
  {
    *GetRxControlAddr(GetNumber()) |= USBFS_UEP_R_RES_STALL;
  }
  SetState(State::STALLED);
  return ErrorCode::OK;
}

ErrorCode CH32EndpointOtgFs::ClearStall()
{
  if (GetState() != State::STALLED)
  {
    return ErrorCode::FAILED;
  }

  bool is_in = (GetDirection() == Direction::IN);
  if (is_in)
  {
    *GetTxControlAddr(GetNumber()) &= ~USBFS_UEP_T_RES_STALL;
  }
  else
  {
    *GetRxControlAddr(GetNumber()) &= ~USBFS_UEP_R_RES_STALL;
  }
  SetState(State::IDLE);
  return ErrorCode::OK;
}

void CH32EndpointOtgFs::TransferComplete(size_t size)
{
  if (GetDirection() == Direction::IN)
  {
    size = GetLastTransferSize();
  }

  std::atomic_signal_fence(std::memory_order_seq_cst);

  if (GetDirection() == Direction::OUT &&
      (USBFSD->INT_FG & USBFS_U_TOG_OK) != USBFS_U_TOG_OK)  // NOLINT
  {
    return;
  }

  std::atomic_signal_fence(std::memory_order_seq_cst);

  if (GetNumber() == EPNumber::EP0 && GetDirection() == Direction::OUT)
  {
    tog_ = true;
    *GetRxControlAddr(GetNumber()) = USBFS_UEP_R_RES_ACK;
  }
  if (GetDirection() == Direction::IN)
  {
    *GetTxControlAddr(GetNumber()) =
        (*GetTxControlAddr(GetNumber()) & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_RES_NAK;
  }
  OnTransferCompleteCallback(false, size);
}

void CH32EndpointOtgFs::SwitchBuffer()
{
  if (GetDirection() == Direction::IN)
  {
    tog_ = (*GetTxControlAddr(GetNumber()) & USBFS_UEP_T_TOG) == USBFS_UEP_T_TOG;
    SetActiveBlock(!tog_);
  }
  else
  {
    tog_ = (*GetRxControlAddr(GetNumber()) & USBFS_UEP_R_TOG) == USBFS_UEP_R_TOG;
    SetActiveBlock(tog_);
  }
}
