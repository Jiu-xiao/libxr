#include <cstdint>

#include "ch32_usb_endpoint.hpp"
#include "ep.hpp"

using namespace LibXR;

#if defined(USBFSD)

// NOLINTBEGIN

static inline volatile uint8_t* get_tx_ctrl_addr(USB::Endpoint::EPNumber ep)
{
  return &USBFSD->UEP0_TX_CTRL + 4 * (USB::Endpoint::EPNumberToInt8(ep));
}
static inline volatile uint8_t* get_rx_ctrl_addr(USB::Endpoint::EPNumber ep)
{
  return &USBFSD->UEP0_RX_CTRL + 4 * (USB::Endpoint::EPNumberToInt8(ep));
}
static inline volatile uint16_t* get_tx_len_addr(USB::Endpoint::EPNumber ep)
{
  return &USBFSD->UEP0_TX_LEN + 2 * (USB::Endpoint::EPNumberToInt8(ep));
}
static inline volatile uint32_t* get_dma_addr(USB::Endpoint::EPNumber ep)
{
  return &USBFSD->UEP0_DMA + USB::Endpoint::EPNumberToInt8(ep);
}

static void set_dma_buffer(USB::Endpoint::EPNumber ep_num, void* value,
                           bool double_buffer)
{
  *get_dma_addr(ep_num) = (uint32_t)value;

  switch (ep_num)
  {
    case USB::Endpoint::EPNumber::EP1:
      if (double_buffer)
        USBFSD->UEP4_1_MOD |= USBFS_UEP1_BUF_MOD;
      else
        USBFSD->UEP4_1_MOD &= ~USBFS_UEP1_BUF_MOD;
      break;
    case USB::Endpoint::EPNumber::EP2:
      if (double_buffer)
        USBFSD->UEP2_3_MOD |= USBFS_UEP2_BUF_MOD;
      else
        USBFSD->UEP2_3_MOD &= ~USBFS_UEP2_BUF_MOD;
      break;
    case USB::Endpoint::EPNumber::EP3:
      if (double_buffer)
        USBFSD->UEP2_3_MOD |= USBFS_UEP3_BUF_MOD;
      else
        USBFSD->UEP2_3_MOD &= ~USBFS_UEP3_BUF_MOD;
      break;
    case USB::Endpoint::EPNumber::EP4:
      if (double_buffer)
        USBFSD->UEP4_1_MOD |= USBFS_UEP4_BUF_MOD;
      else
        USBFSD->UEP4_1_MOD &= ~USBFS_UEP4_BUF_MOD;
      break;
    case USB::Endpoint::EPNumber::EP5:
      if (double_buffer)
        USBFSD->UEP5_6_MOD |= USBFS_UEP5_BUF_MOD;
      else
        USBFSD->UEP5_6_MOD &= ~USBFS_UEP5_BUF_MOD;
      break;
    case USB::Endpoint::EPNumber::EP6:
      if (double_buffer)
        USBFSD->UEP5_6_MOD |= USBFS_UEP6_BUF_MOD;
      else
        USBFSD->UEP5_6_MOD &= ~USBFS_UEP6_BUF_MOD;
      break;
    case USB::Endpoint::EPNumber::EP7:
      if (double_buffer)
        USBFSD->UEP7_MOD |= USBFS_UEP7_BUF_MOD;
      else
        USBFSD->UEP7_MOD &= ~USBFS_UEP7_BUF_MOD;
      break;
    default:
      break;
  }
}

static void set_tx_len(USB::Endpoint::EPNumber ep_num, uint32_t value)
{
  *get_tx_len_addr(ep_num) = value;
}

static void enable_tx(USB::Endpoint::EPNumber ep_num)
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
static void disable_tx(USB::Endpoint::EPNumber ep_num)
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
static void enable_rx(USB::Endpoint::EPNumber ep_num)
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
static void disable_rx(USB::Endpoint::EPNumber ep_num)
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
static LibXR::RawData select_buffer(USB::Endpoint::EPNumber ep_num,
                                    USB::Endpoint::Direction dir,
                                    const LibXR::RawData& buffer)
{
  if (ep_num == USB::Endpoint::EPNumber::EP0)
  {
    return buffer;
  }

  const size_t half = 64U;
  ASSERT(buffer.size_ >= 128U);

  if (dir == USB::Endpoint::Direction::OUT)
  {
    return LibXR::RawData(buffer.addr_, half);
  }

  return LibXR::RawData(reinterpret_cast<uint8_t*>(buffer.addr_) + half, half);
}

CH32EndpointOtgFs::CH32EndpointOtgFs(EPNumber ep_num, Direction dir,
                                     LibXR::RawData buffer, bool single_direction)
    : Endpoint(ep_num, dir,
               (single_direction || ep_num == EPNumber::EP0)
                   ? buffer
                   : select_buffer(ep_num, dir, buffer),
               (single_direction && ep_num != EPNumber::EP0) ? 2 : 1),
      single_direction_(single_direction),
      dma_buffer_(buffer)
{
  map_otg_fs_[EPNumberToInt8(GetNumber())][static_cast<uint8_t>(dir)] = this;

  set_dma_buffer(GetNumber(), dma_buffer_.addr_, false);

  if (dir == Direction::IN)
  {
    set_tx_len(GetNumber(), 0);
    *get_tx_ctrl_addr(GetNumber()) = USBFS_UEP_T_RES_NAK;
  }
  else
  {
    *get_rx_ctrl_addr(GetNumber()) = USBFS_UEP_R_RES_NAK;
  }
}

ErrorCode CH32EndpointOtgFs::ConfigureHardware(const Config& cfg, Granted& /*granted*/)
{
  // 包长已由核心按 MaxPacketSizeLimit 收敛；状态由基类管理，这里只编程寄存器。
  is_isochronous_ = (cfg.type == Type::ISOCHRONOUS);

  if (IsPlanning())
  {
    return ErrorCode::OK;
  }

  const bool is_ep0 = (GetNumber() == EPNumber::EP0);
  const uint8_t tx_toggle = is_isochronous_ ? 0U : USBFS_UEP_T_AUTO_TOG;
  const uint8_t rx_toggle = is_isochronous_ ? 0U : USBFS_UEP_R_AUTO_TOG;

  if (!single_direction_ && !is_ep0)
  {
    // 单 bank 共享双向：IN 地址固定在 OUT +64，两个使能保持稳定，NAK 是各方向
    // 的激活闸门。
    enable_tx(GetNumber());
    enable_rx(GetNumber());
  }
  if (GetDirection() == Direction::IN)
  {
    set_tx_len(GetNumber(), 0);
    *get_tx_ctrl_addr(GetNumber()) = USBFS_UEP_T_RES_NAK | tx_toggle;
    enable_tx(GetNumber());
    if (single_direction_) disable_rx(GetNumber());
  }
  else
  {
    *get_rx_ctrl_addr(GetNumber()) = USBFS_UEP_R_RES_NAK | rx_toggle;
    enable_rx(GetNumber());
    if (single_direction_) disable_tx(GetNumber());
  }

  set_dma_buffer(GetNumber(), SlotMemory().addr_, false);
  transfer_active_ = false;
  transfer_tag_ = 0;
  transfer_requested_ = 0;
  transfer_accumulated_ = 0;
  transfer_cursor_ = nullptr;
  return ErrorCode::OK;
}

void CH32EndpointOtgFs::CloseHardware(bool /*in_isr*/)
{
  if (GetDirection() == Direction::IN)
  {
    *get_tx_ctrl_addr(GetNumber()) = USBFS_UEP_T_RES_NAK;
    if (single_direction_) disable_tx(GetNumber());
  }
  else
  {
    *get_rx_ctrl_addr(GetNumber()) = USBFS_UEP_R_RES_NAK;
    if (single_direction_) disable_rx(GetNumber());
  }
  transfer_active_ = false;
}

ErrorCode CH32EndpointOtgFs::StartHardware(const HwRequest& request, bool /*in_isr*/)
{
  const bool is_in = (GetDirection() == Direction::IN);
  const bool is_ep0 = (GetNumber() == EPNumber::EP0);

  transfer_tag_ = request.tag;
  transfer_requested_ = request.length;
  transfer_accumulated_ = 0;
  transfer_active_ = true;
  transfer_dma_ = request.slot;

  if (is_in)
  {
    // 槽内存就是 DMA 缓冲：类在 Submit 前已把数据写进槽，无需再拷贝。
    set_dma_buffer(GetNumber(), request.slot.addr_, false);
    set_tx_len(GetNumber(), request.length);
    auto addr = get_tx_ctrl_addr(GetNumber());

    if (!is_ep0)
    {
      *addr = (is_isochronous_ ? USBFS_UEP_T_RES_NONE : USBFS_UEP_T_RES_ACK) |
              (*addr & (~USBFS_UEP_T_RES_MASK));
    }
    else
    {
      // EP0 IN 按 start 次数翻转 toggle（HS 已上板方案）。
      *addr = USBFS_UEP_T_RES_ACK | (tog_ ? USBFS_UEP_T_TOG : 0);
      tog_ = !tog_;
    }
    return ErrorCode::OK;
  }

  // OUT：共享模式每包完成且不推进 DMA（UEPn_DMA 收发共用，推进会破坏 IN 固定
  // +64 的硬件布局）；单向模式按包推进 DMA，多包请求一次完成。
  if (single_direction_)
  {
    transfer_cursor_ = static_cast<uint8_t*>(request.slot.addr_);
  }
  set_dma_buffer(GetNumber(), request.slot.addr_, false);
  auto addr = get_rx_ctrl_addr(GetNumber());
  if (!is_ep0)
  {
    *addr = (is_isochronous_ ? USBFS_UEP_R_RES_NONE : USBFS_UEP_R_RES_ACK) |
            (*addr & (~USBFS_UEP_R_RES_MASK));
  }
  else
  {
    // EP0 OUT 不在 start 时翻转：状态阶段强制 DATA1，由完成路径置位。
    *addr = USBFS_UEP_R_RES_ACK | (tog_ ? USBFS_UEP_R_TOG : 0);
  }
  return ErrorCode::OK;
}

USB::Endpoint::StopResult CH32EndpointOtgFs::StopHardware(bool /*in_isr*/)
{
  // RES=NAK 立即生效：控制器从下一个事务起不再访问槽；在途的最后一个事务仍会
  // 完成（最多一个包落进槽），此后 INT_BUSY=1 自动 NAK 全部事务。无法也无须
  // 中止在途事务，迟到的完成由核心按标签丢弃（与 FSDEV 先例一致）。
  if (GetDirection() == Direction::IN)
  {
    *get_tx_ctrl_addr(GetNumber()) =
        (*get_tx_ctrl_addr(GetNumber()) & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_RES_NAK;
  }
  else
  {
    *get_rx_ctrl_addr(GetNumber()) =
        (*get_rx_ctrl_addr(GetNumber()) & ~USBFS_UEP_R_RES_MASK) | USBFS_UEP_R_RES_NAK;
  }
  transfer_active_ = false;
  return StopResult::DONE;
}

size_t CH32EndpointOtgFs::ReapProgress(const HwRequest& /*request*/, bool& retired)
{
  retired = true;
  return transfer_accumulated_;
}

ErrorCode CH32EndpointOtgFs::StallHardware(bool /*in_isr*/)
{
  if (GetDirection() == Direction::IN)
  {
    *get_tx_ctrl_addr(GetNumber()) |= USBFS_UEP_T_RES_STALL;
  }
  else
  {
    *get_rx_ctrl_addr(GetNumber()) |= USBFS_UEP_R_RES_STALL;
  }
  return ErrorCode::OK;
}

ErrorCode CH32EndpointOtgFs::ClearStallHardware(bool /*in_isr*/)
{
  if (GetDirection() == Direction::IN)
    *get_tx_ctrl_addr(GetNumber()) = USBFS_UEP_T_RES_NAK | USBFS_UEP_T_AUTO_TOG;
  else
    *get_rx_ctrl_addr(GetNumber()) = USBFS_UEP_R_RES_NAK | USBFS_UEP_R_AUTO_TOG;
  tog_ = false;
  return ErrorCode::OK;
}

size_t CH32EndpointOtgFs::MaxPacketSizeLimit(Type type) const
{
  // 手册表 23-4（p.359）/EP0（p.355）：EP1-15 除 EP3 外最大 64 B；EP3 仅同步
  // 传输且单缓冲时最大 1023 B；EP0 收发共用 64 B。
  if (type == Type::ISOCHRONOUS)
  {
    return 1023U;
  }
  return 64U;
}

void CH32EndpointOtgFs::TransferComplete(size_t size)
{
  const bool IS_IN = (GetDirection() == Direction::IN);
  const bool IS_EP0 = (GetNumber() == EPNumber::EP0);
  const bool IS_ISO = (GetType() == Type::ISOCHRONOUS);

  // UIF_TRANSFER/INT_FG 会在 IRQ handler 完成分发后统一清掉。

  if (IS_IN)
  {
    // 完成后恢复 NAK；IN 一次请求一次完成，actual = 请求长度。
    *get_tx_ctrl_addr(GetNumber()) =
        (*get_tx_ctrl_addr(GetNumber()) & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_RES_NAK;
    transfer_active_ = false;
    OnHardwareComplete(true, transfer_tag_, transfer_requested_);
    return;
  }

  // TOG 不匹配表示数据同步失败：丢弃本包并按原请求重新挂接收。
  if (!IS_ISO)
  {
    const bool TOG_OK = ((USBFSD->INT_ST & USBFS_U_TOG_OK) == USBFS_U_TOG_OK);  // NOLINT
    if (!TOG_OK)
    {
      HwRequest retry{};
      retry.slot = transfer_dma_;
      retry.length = transfer_requested_;
      retry.tag = transfer_tag_;
      (void)StartHardware(retry, true);
      return;
    }
  }

  if (!IS_EP0 && single_direction_ && !IS_ISO)
  {
    // 单向：按包推进 DMA，累计到请求长度或收到短包才一次完成。
    transfer_accumulated_ += size;
    if (transfer_accumulated_ < transfer_requested_ && size == MaxPacketSize())
    {
      transfer_cursor_ += size;
      *get_dma_addr(GetNumber()) = reinterpret_cast<uint32_t>(transfer_cursor_);
      return;  // RES 仍为 ACK，继续收下一包
    }
    *get_rx_ctrl_addr(GetNumber()) =
        (*get_rx_ctrl_addr(GetNumber()) & ~USBFS_UEP_R_RES_MASK) | USBFS_UEP_R_RES_NAK;
    transfer_active_ = false;
    OnHardwareComplete(true, transfer_tag_, transfer_accumulated_);
    return;
  }

  // 共享模式（含 EP0 与 ISO）：每包完成，actual = RX_LEN，不推进 DMA；包间由
  // INT_BUSY 自动 NAK 挡住，类消费槽后重新挂接收。
  if (!IS_EP0)
  {
    *get_rx_ctrl_addr(GetNumber()) =
        (*get_rx_ctrl_addr(GetNumber()) & ~USBFS_UEP_R_RES_MASK) | USBFS_UEP_R_RES_NAK;
  }
  transfer_accumulated_ = size;
  transfer_active_ = false;
  if (IS_EP0)
  {
    // 每个 EP0 OUT 包完成即是一个阶段的收尾（FS 下 MPS0 ≤ 64 = 单包）：置
    // DATA1，保证状态阶段 OUT 与 SETUP 后数据阶段首包为 DATA1（HS 已上板方案）。
    tog_ = true;
  }
  OnHardwareComplete(true, transfer_tag_, size);
}

#endif
