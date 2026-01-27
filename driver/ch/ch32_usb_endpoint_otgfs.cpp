#include <cstdint>

#include "ch32_usb_endpoint.hpp"
#include "ep.hpp"

using namespace LibXR;

#if defined(USBFSD)

// NOLINTBEGIN

static inline volatile uint8_t* GetTxCtrlAddr(USB::Endpoint::EPNumber ep)
{
  return &USBFSD->UEP0_TX_CTRL + 4 * (USB::Endpoint::EPNumberToInt8(ep));
}
static inline volatile uint8_t* GetRxCtrlAddr(USB::Endpoint::EPNumber ep)
{
  return &USBFSD->UEP0_RX_CTRL + 4 * (USB::Endpoint::EPNumberToInt8(ep));
}
static inline volatile uint16_t* GetTxLenAddr(USB::Endpoint::EPNumber ep)
{
  return &USBFSD->UEP0_TX_LEN + 2 * (USB::Endpoint::EPNumberToInt8(ep));
}
static inline volatile uint32_t* GetDmaAddr(USB::Endpoint::EPNumber ep)
{
  return &USBFSD->UEP0_DMA + USB::Endpoint::EPNumberToInt8(ep);
}

static void SetDmaBuffer(USB::Endpoint::EPNumber ep_num, void* value, bool double_buffer)
{
  *GetDmaAddr(ep_num) = (uint32_t)value;

  if (!double_buffer)
  {
    return;
  }

  switch (ep_num)
  {
    case USB::Endpoint::EPNumber::EP1:
      USBFSD->UEP4_1_MOD |= USBFS_UEP1_BUF_MOD;
      break;
    case USB::Endpoint::EPNumber::EP2:
      USBFSD->UEP2_3_MOD |= USBFS_UEP2_BUF_MOD;
      break;
    case USB::Endpoint::EPNumber::EP3:
      USBFSD->UEP2_3_MOD |= USBFS_UEP3_BUF_MOD;
      break;
    case USB::Endpoint::EPNumber::EP4:
      USBFSD->UEP4_1_MOD |= USBFS_UEP4_BUF_MOD;
      break;
    case USB::Endpoint::EPNumber::EP5:
      USBFSD->UEP5_6_MOD |= USBFS_UEP5_BUF_MOD;
      break;
    case USB::Endpoint::EPNumber::EP6:
      USBFSD->UEP5_6_MOD |= USBFS_UEP6_BUF_MOD;
      break;
    case USB::Endpoint::EPNumber::EP7:
      USBFSD->UEP7_MOD |= USBFS_UEP7_BUF_MOD;
      break;
    default:
      break;
  }
}

static void SetTxLen(USB::Endpoint::EPNumber ep_num, uint32_t value)
{
  *GetTxLenAddr(ep_num) = value;
}

static void EnableTx(USB::Endpoint::EPNumber ep_num)
{
  switch (ep_num)
  {
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
                                     LibXR::RawData buffer, bool is_isochronous)
    : Endpoint(ep_num, dir, is_isochronous ? buffer : SelectBuffer(ep_num, dir, buffer)),
      is_isochronous_(is_isochronous),
      dma_buffer_(buffer)
{
  map_otg_fs_[EPNumberToInt8(GetNumber())][static_cast<uint8_t>(dir)] = this;

  SetDmaBuffer(GetNumber(), dma_buffer_.addr_, is_isochronous ? false : true);

  if (dir == Direction::IN)
  {
    SetTxLen(GetNumber(), 0);
    *GetTxCtrlAddr(GetNumber()) = USBFS_UEP_T_RES_NAK;
  }
  else
  {
    *GetRxCtrlAddr(GetNumber()) = USBFS_UEP_R_RES_NAK;
  }
}

void CH32EndpointOtgFs::Configure(const Config& cfg)
{
  auto& ep_cfg = GetConfig();
  ep_cfg = cfg;

  if (GetNumber() != EPNumber::EP0 && !is_isochronous_)
  {
    ep_cfg.double_buffer = true;
  }
  else
  {
    ep_cfg.double_buffer = false;
  }

  ep_cfg.max_packet_size = GetBuffer().size_;

  SetTxLen(GetNumber(), 0);

  if (!is_isochronous_)
  {
    *GetRxCtrlAddr(GetNumber()) = USBFS_UEP_R_RES_NAK | USBFS_UEP_R_AUTO_TOG;
    *GetTxCtrlAddr(GetNumber()) = USBFS_UEP_T_RES_NAK | USBFS_UEP_T_AUTO_TOG;
    EnableTx(GetNumber());
    EnableRx(GetNumber());
  }
  else
  {
    *GetRxCtrlAddr(GetNumber()) = USBFS_UEP_R_RES_NAK;
    *GetTxCtrlAddr(GetNumber()) = USBFS_UEP_T_RES_NAK;
    if (GetDirection() == Direction::IN)
    {
      EnableTx(GetNumber());
    }
    else
    {
      EnableRx(GetNumber());
    }
  }

  SetDmaBuffer(GetNumber(), dma_buffer_.addr_, is_isochronous_ ? false : true);

  SetState(State::IDLE);
}

void CH32EndpointOtgFs::Close()
{
  DisableTx(GetNumber());
  DisableRx(GetNumber());

  *GetTxCtrlAddr(GetNumber()) = USBFS_UEP_T_RES_NAK;
  *GetRxCtrlAddr(GetNumber()) = USBFS_UEP_R_RES_NAK;

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
    auto addr = GetTxCtrlAddr(GetNumber());

    if (GetNumber() != EPNumber::EP0)
    {
      *addr = (is_isochronous_ ? USBFS_UEP_T_RES_NONE : USBFS_UEP_T_RES_ACK) |
              (*addr & (~USBFS_UEP_T_RES_MASK));
    }
    else
    {
      *addr = USBFS_UEP_T_RES_ACK | (tog_ ? USBFS_UEP_T_TOG : 0);
    }
  }
  else
  {
    auto addr = GetRxCtrlAddr(GetNumber());

    if (GetNumber() != EPNumber::EP0)
    {
      *addr = (is_isochronous_ ? USBFS_UEP_R_RES_NONE : USBFS_UEP_R_RES_ACK) |
              (*addr & (~USBFS_UEP_R_RES_MASK));
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

  last_transfer_size_ = size;
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
    *GetTxCtrlAddr(GetNumber()) |= USBFS_UEP_T_RES_STALL;
  }
  else
  {
    *GetRxCtrlAddr(GetNumber()) |= USBFS_UEP_R_RES_STALL;
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
    *GetTxCtrlAddr(GetNumber()) &= ~USBFS_UEP_T_RES_STALL;
  }
  else
  {
    *GetRxCtrlAddr(GetNumber()) &= ~USBFS_UEP_R_RES_STALL;
  }
  SetState(State::IDLE);
  return ErrorCode::OK;
}

void CH32EndpointOtgFs::TransferComplete(size_t size)
{
  const bool is_in = (GetDirection() == Direction::IN);
  const bool is_out = !is_in;
  const bool is_ep0 = (GetNumber() == EPNumber::EP0);
  const bool is_iso = (GetType() == Type::ISOCHRONOUS);

  // UIF_TRANSFER / INT_FG 由 IRQ handler 统一在“本次处理结束后”清除（不在此处写
  // INT_FG）。

  if (is_in)
  {
    // 收尾置 NAK
    *GetTxCtrlAddr(GetNumber()) =
        (*GetTxCtrlAddr(GetNumber()) & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_RES_NAK;

    size = last_transfer_size_;
  }
  else
  {
    // 非 EP0 的 OUT：收尾置 NAK
    if (!is_ep0)
    {
      *GetRxCtrlAddr(GetNumber()) =
          (*GetRxCtrlAddr(GetNumber()) & ~USBFS_UEP_R_RES_MASK) | USBFS_UEP_R_RES_NAK;
    }
  }

  // 若 TOG 不 OK，说明数据同步失败（RB_UIS_TOG_OK / RB_U_TOG_OK）
  if (is_out)
  {
    const bool tog_ok = ((USBFSD->INT_ST & USBFS_U_TOG_OK) == USBFS_U_TOG_OK);  // NOLINT
    if (!tog_ok)
    {
      SetState(State::IDLE);
      (void)Transfer(last_transfer_size_);
      return;
    }
  }

  // 成功：更新软件 data toggle（非 EP0、非 ISO）
  if (GetState() == State::BUSY && !is_ep0 && !is_iso)
  {
    tog_ = !tog_;
  }

  if (is_ep0 && is_out)
  {
    tog_ = true;
    *GetRxCtrlAddr(GetNumber()) = USBFS_UEP_R_RES_ACK;
  }

  OnTransferCompleteCallback(true, size);
}

void CH32EndpointOtgFs::SwitchBuffer()
{
  if (GetDirection() == Direction::IN)
  {
    tog_ = (*GetTxCtrlAddr(GetNumber()) & USBFS_UEP_T_TOG) == USBFS_UEP_T_TOG;
    SetActiveBlock(!tog_);
  }
  else
  {
    tog_ = (*GetRxCtrlAddr(GetNumber()) & USBFS_UEP_R_TOG) == USBFS_UEP_R_TOG;
    SetActiveBlock(tog_);
  }
}

#endif
