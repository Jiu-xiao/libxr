#include <cstdint>

#include "ch32h41x_usb_endpoint_otghs.hpp"
#include "ep.hpp"
#include "libxr_time.hpp"
#include "timebase.hpp"

using namespace LibXR;

#if defined(USBHSD) && defined(__CH32H417_H)

// NOLINTBEGIN
static constexpr bool normalize_h417_device_double_buffer(bool)
{
  // CH32H417 RM 25.2.1.21/23/24 exposes one RX DMA and one TX DMA register
  // per device endpoint. R32_UEP_AF_MODE selects endpoint 1..7 vs 9..15
  // multiplexing; it is not a device-side double-buffer switch.
  return false;
}

static inline volatile uint8_t* get_tx_control_addr(USB::Endpoint::EPNumber ep_num)
{
  return reinterpret_cast<volatile uint8_t*>(
      reinterpret_cast<volatile uint8_t*>(&USBHSD->UEP0_TX_CTRL) +
      static_cast<int>(ep_num) * 4);
}

static inline volatile uint8_t* get_rx_control_addr(USB::Endpoint::EPNumber ep_num)
{
  return reinterpret_cast<volatile uint8_t*>(
      reinterpret_cast<volatile uint8_t*>(&USBHSD->UEP0_TX_CTRL) +
      static_cast<int>(ep_num) * 4 + 1);
}

static inline volatile uint16_t* get_tx_len_addr(USB::Endpoint::EPNumber ep_num)
{
  return reinterpret_cast<volatile uint16_t*>(
      reinterpret_cast<volatile uint8_t*>(&USBHSD->UEP0_TX_LEN) +
      static_cast<int>(ep_num) * 4);
}

static_assert(offsetof(USBHSD_TypeDef, UEP1_MAX_LEN) -
                  offsetof(USBHSD_TypeDef, UEP0_MAX_LEN) ==
              4);

static inline volatile uint16_t* get_rx_max_len_addr(USB::Endpoint::EPNumber ep_num)
{
  return reinterpret_cast<volatile uint16_t*>(
      reinterpret_cast<volatile uint8_t*>(&USBHSD->UEP0_MAX_LEN) +
      static_cast<int>(ep_num) * 4);
}

static inline volatile uint32_t* get_tx_dma_addr(USB::Endpoint::EPNumber ep_num)
{
  if (ep_num == USB::Endpoint::EPNumber::EP0)
  {
    return &USBHSD->UEP0_DMA;
  }
  return reinterpret_cast<volatile uint32_t*>(
      reinterpret_cast<volatile uint32_t*>(&USBHSD->UEP1_TX_DMA) +
      (static_cast<int>(ep_num) - 1));
}

static inline volatile uint32_t* get_rx_dma_addr(USB::Endpoint::EPNumber ep_num)
{
  if (ep_num == USB::Endpoint::EPNumber::EP0)
  {
    return &USBHSD->UEP0_DMA;
  }
  return reinterpret_cast<volatile uint32_t*>(
      reinterpret_cast<volatile uint32_t*>(&USBHSD->UEP1_RX_DMA) +
      (static_cast<int>(ep_num) - 1));
}

static void set_tx_len(USB::Endpoint::EPNumber ep_num, uint32_t value)
{
  *get_tx_len_addr(ep_num) = value;
}

static void set_tx_dma_buffer(USB::Endpoint::EPNumber ep_num, void* buffer,
                              uint32_t buffer_size, bool double_buffer)
{
  uint8_t* buf_base = reinterpret_cast<uint8_t*>(buffer);
  UNUSED(buffer_size);
  UNUSED(double_buffer);

  if (ep_num == USB::Endpoint::EPNumber::EP0)
  {
    USBHSD->UEP0_DMA = (uint32_t)buf_base;
  }
  else
  {
    *get_tx_dma_addr(ep_num) = (uint32_t)buf_base;
  }
}

static void set_rx_dma_buffer(USB::Endpoint::EPNumber ep_num, void* buffer,
                              uint32_t buffer_size, bool double_buffer)
{
  uint8_t* buf_base = reinterpret_cast<uint8_t*>(buffer);
  UNUSED(buffer_size);
  UNUSED(double_buffer);

  if (ep_num == USB::Endpoint::EPNumber::EP0)
  {
    USBHSD->UEP0_DMA = (uint32_t)buf_base;
  }
  else
  {
    *get_rx_dma_addr(ep_num) = (uint32_t)buf_base;
  }
}

static inline uint16_t h417_otghs_ep_bit(USB::Endpoint::EPNumber ep_num)
{
  return static_cast<uint16_t>(1u << USB::Endpoint::EPNumberToInt8(ep_num));
}

static void enable_tx(USB::Endpoint::EPNumber ep_num)
{
  USBHSD->UEP_TX_EN |= h417_otghs_ep_bit(ep_num);
}

static void disable_tx(USB::Endpoint::EPNumber ep_num)
{
  USBHSD->UEP_TX_EN &= static_cast<uint16_t>(~h417_otghs_ep_bit(ep_num));
}

static void enable_rx(USB::Endpoint::EPNumber ep_num)
{
  USBHSD->UEP_RX_EN |= h417_otghs_ep_bit(ep_num);
}

static void disable_rx(USB::Endpoint::EPNumber ep_num)
{
  USBHSD->UEP_RX_EN &= static_cast<uint16_t>(~h417_otghs_ep_bit(ep_num));
}

static void set_tx_toggle_auto(USB::Endpoint::EPNumber ep_num, bool enable)
{
  const uint16_t ep_bit = h417_otghs_ep_bit(ep_num);
  if (enable)
  {
    USBHSD->UEP_TX_TOG_AUTO |= ep_bit;
  }
  else
  {
    USBHSD->UEP_TX_TOG_AUTO &= static_cast<uint16_t>(~ep_bit);
  }
}

static void set_rx_toggle_auto(USB::Endpoint::EPNumber ep_num, bool enable)
{
  const uint16_t ep_bit = h417_otghs_ep_bit(ep_num);
  if (enable)
  {
    USBHSD->UEP_RX_TOG_AUTO |= ep_bit;
  }
  else
  {
    USBHSD->UEP_RX_TOG_AUTO &= static_cast<uint16_t>(~ep_bit);
  }
}
// NOLINTEND

CH32H41xEndpointOtgHs::CH32H41xEndpointOtgHs(EPNumber ep_num, Direction dir,
                                             LibXR::RawData buffer,
                                             bool double_buffer)
    : Endpoint(ep_num, dir, buffer),
      hw_double_buffer_(normalize_h417_device_double_buffer(double_buffer)),
      dma_buffer_(buffer)
{
  map_otg_hs_[EPNumberToInt8(GetNumber())][static_cast<uint8_t>(dir)] = this;

  if (dir == Direction::IN)
  {
    set_tx_dma_buffer(GetNumber(), dma_buffer_.addr_, dma_buffer_.size_, hw_double_buffer_);
  }
  else
  {
    set_rx_dma_buffer(GetNumber(), dma_buffer_.addr_, dma_buffer_.size_, hw_double_buffer_);
  }

  if (dir == Direction::IN)
  {
    set_tx_len(GetNumber(), 0);
    *get_tx_control_addr(GetNumber()) = USBHS_UEP_T_RES_NAK;
  }
  else
  {
    *get_rx_control_addr(GetNumber()) = USBHS_UEP_R_RES_NAK;
  }
}

void CH32H41xEndpointOtgHs::Configure(const Config& cfg)
{
  auto& ep_cfg = GetConfig();
  ep_cfg = cfg;

  const int EP_IDX = EPNumberToInt8(GetNumber());

  const uint8_t IN_IDX = static_cast<uint8_t>(Direction::IN);
  const uint8_t OUT_IDX = static_cast<uint8_t>(Direction::OUT);

  const auto* in_ep = map_otg_hs_[EP_IDX][IN_IDX];
  const auto* out_ep = map_otg_hs_[EP_IDX][OUT_IDX];
  const bool HAS_IN =
      (in_ep != nullptr) && (in_ep->GetState() != State::DISABLED || in_ep == this);
  const bool HAS_OUT =
      (out_ep != nullptr) && (out_ep->GetState() != State::DISABLED || out_ep == this);

  // 双缓冲策略：EP0 不使用 double buffer。
  // Double-buffer policy: EP0 does not use double buffering.
  // CH32H417 USBHS device mode does not expose a device-side DB selector here:
  // RM 25.2.1.21 defines UEP_AF_MODE as endpoint 1..7 vs 9..15 multiplexing.
  bool enable_double = false;
  if (enable_double && HAS_IN && HAS_OUT)
  {
    ASSERT(false);  // 双缓冲端点必须是单向
                    // / Double-buffer endpoints must be single-direction.
    enable_double = false;
  }
  ep_cfg.double_buffer = enable_double;

  const size_t buffer_size = GetBuffer().size_;
  if (ep_cfg.type == Type::BULK)
  {
    // CH32 USBHS bulk endpoints always use the hardware-fixed 512-byte packet size.
    // CH32 USBHS 的 BULK 端点始终使用硬件固定的 512 字节包长。
    ASSERT(buffer_size >= 512u);
    ep_cfg.max_packet_size = 512u;
  }
  else if (ep_cfg.max_packet_size > buffer_size)
  {
    // 非 BULK 端点的包长按缓冲区大小截断。
    // Clamp non-bulk packet sizes to the endpoint buffer size.
    ep_cfg.max_packet_size = buffer_size;
  }

  // H417 USBHS examples always publish UEPn_MAX_LEN for every active endpoint,
  // including IN-only ones. Keep the hardware packet-size image aligned with the
  // configured endpoint shape before touching direction-specific control bits.
  // H417 USBHS 官方例程会给每个已用端点都写 UEPn_MAX_LEN，包括仅 IN 端点；
  // 这里先把硬件里的包长镜像写齐，再处理方向相关控制位。
  *get_rx_max_len_addr(GetNumber()) = ep_cfg.max_packet_size;

  if (GetDirection() == Direction::IN)
  {
    set_tx_toggle_auto(GetNumber(), enable_double && GetType() != Type::ISOCHRONOUS);
    if (!HAS_OUT)
    {
      set_rx_toggle_auto(GetNumber(), false);
    }
    *get_tx_control_addr(GetNumber()) = USBHS_UEP_T_RES_NAK;
    set_tx_len(GetNumber(), 0);
  }
  else
  {
    set_rx_toggle_auto(GetNumber(), enable_double && GetType() != Type::ISOCHRONOUS);
    if (!HAS_IN)
    {
      set_tx_toggle_auto(GetNumber(), false);
    }
    *get_rx_control_addr(GetNumber()) = USBHS_UEP_R_RES_NAK;
  }

  const int IDX = static_cast<int>(GetNumber());
  UNUSED(IDX);
  if (GetDirection() == Direction::IN)
  {
    if (GetType() == Type::ISOCHRONOUS)
    {
      USBHSD->UEP_TX_ISO |= h417_otghs_ep_bit(GetNumber());
    }
    else
    {
      USBHSD->UEP_TX_ISO &= static_cast<uint16_t>(~h417_otghs_ep_bit(GetNumber()));
    }
  }
  else
  {
    if (GetType() == Type::ISOCHRONOUS)
    {
      USBHSD->UEP_RX_ISO |= h417_otghs_ep_bit(GetNumber());
    }
    else
    {
      USBHSD->UEP_RX_ISO &= static_cast<uint16_t>(~h417_otghs_ep_bit(GetNumber()));
    }
  }

  if (GetDirection() == Direction::IN)
  {
    if (GetType() != Type::ISOCHRONOUS)
    {
      enable_tx(GetNumber());
    }
    else
    {
      disable_tx(GetNumber());
    }

    if (!HAS_OUT)
    {
      disable_rx(GetNumber());
    }

    set_tx_dma_buffer(GetNumber(), dma_buffer_.addr_, dma_buffer_.size_, enable_double);
  }
  else
  {
    enable_rx(GetNumber());

    if (!HAS_IN)
    {
      disable_tx(GetNumber());
    }

    set_rx_dma_buffer(GetNumber(), dma_buffer_.addr_, dma_buffer_.size_, enable_double);
  }

  SetState(State::IDLE);
}

void CH32H41xEndpointOtgHs::Close()
{
  disable_tx(GetNumber());
  disable_rx(GetNumber());
  set_tx_toggle_auto(GetNumber(), false);
  set_rx_toggle_auto(GetNumber(), false);

  *get_tx_control_addr(GetNumber()) = USBHS_UEP_T_RES_NAK;
  *get_rx_control_addr(GetNumber()) = USBHS_UEP_R_RES_NAK;

  SetState(State::DISABLED);
}

ErrorCode CH32H41xEndpointOtgHs::Transfer(size_t size)
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

  bool IS_IN = (GetDirection() == Direction::IN);

  if (IS_IN && UseDoubleBuffer() && GetType() != Type::ISOCHRONOUS)
  {
    SwitchBuffer();
  }

  if (GetNumber() == EPNumber::EP0)
  {
    if (!IS_IN && size == 0)
    {
      tog0_ = true;
      tog1_ = false;
    }
  }

  last_transfer_size_ = size;
  SetState(State::BUSY);

  if (IS_IN)
  {
    if (GetType() == Type::ISOCHRONOUS)
    {
      enable_tx(GetNumber());
    }

    set_tx_len(GetNumber(), size);
    auto addr = get_tx_control_addr(GetNumber());

    if (GetType() != Type::ISOCHRONOUS)
    {
      if (GetNumber() != EPNumber::EP0)
      {
        *addr = (*addr & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_ACK;
      }
      else
      {
        *addr = USBHS_UEP_T_RES_ACK |
                (*addr & (~(USBHS_UEP_T_RES_MASK | USBHS_UEP_T_TOG_MDATA))) |
                (tog0_ ? USBHS_UEP_T_TOG_DATA1 : 0);
      }
    }
    else
    {
      *addr = static_cast<uint8_t>((*addr & ~USBHS_UEP_T_TOG_MASK) | USBHS_UEP_T_RES_ACK);
    }
  }
  else
  {
    auto addr = get_rx_control_addr(GetNumber());

    if (GetType() != Type::ISOCHRONOUS)
    {
      if (GetNumber() != EPNumber::EP0)
      {
        *addr = (*addr & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_ACK;
      }
      else
      {
        if (size == 0u)
        {
          // 控制传输的 status OUT 固定从 DATA1 开始。
          // Control status OUT always starts from DATA1.
          *addr = USBHS_UEP_R_RES_ACK |
                  (*addr & (~(USBHS_UEP_R_RES_MASK | USBHS_UEP_R_TOG_MDATA))) |
                  USBHS_UEP_R_TOG_DATA1;
        }
        else
        {
          // EP0 多包 OUT 续挂时，这里只重新打开 ACK，不再用软件重算 DATA0/DATA1。
          // 每个包成功收完后，由完成路径推进一次硬件 RX toggle。
          // For EP0 multi-packet OUT, only reopen ACK here and keep the current
          // hardware DATA0/DATA1 phase; the packet-complete path advances RX toggle.
          *addr = (*addr & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_ACK;
        }
      }
    }
    else
    {
      *addr = USBHS_UEP_R_RES_ACK |
              (*addr & (~(USBHS_UEP_R_RES_MASK | USBHS_UEP_R_TOG_MDATA)));
    }
  }

  if (GetNumber() == EPNumber::EP0 && IS_IN)
  {
    tog0_ = !tog0_;
  }

  return ErrorCode::OK;
}

ErrorCode CH32H41xEndpointOtgHs::Stall()
{
  const bool IS_IN = (GetDirection() == Direction::IN);
  if (GetState() != State::IDLE && !(GetState() == State::BUSY && !IS_IN))
  {
    return ErrorCode::BUSY;
  }

  if (IS_IN)
  {
    *get_tx_control_addr(GetNumber()) |= USBHS_UEP_T_RES_STALL;
  }
  else
  {
    *get_rx_control_addr(GetNumber()) |= USBHS_UEP_R_RES_STALL;
  }
  SetState(State::STALLED);
  return ErrorCode::OK;
}

ErrorCode CH32H41xEndpointOtgHs::ClearStall()
{
  if (GetState() != State::STALLED)
  {
    return ErrorCode::FAILED;
  }

  bool IS_IN = (GetDirection() == Direction::IN);
  if (IS_IN)
  {
    *get_tx_control_addr(GetNumber()) &= ~USBHS_UEP_T_RES_STALL;
  }
  else
  {
    *get_rx_control_addr(GetNumber()) &= ~USBHS_UEP_R_RES_STALL;
  }
  SetState(State::IDLE);
  return ErrorCode::OK;
}

void CH32H41xEndpointOtgHs::TransferComplete(size_t size)
{
  const bool IS_IN = (GetDirection() == Direction::IN);
  const bool IS_OUT = !IS_IN;
  const bool IS_EP0 = (GetNumber() == EPNumber::EP0);
  const bool IS_ISO = (GetType() == Type::ISOCHRONOUS);

  // UIF_TRANSFER/INT_FG 会在 IRQ handler 完成分发后统一清掉。
  // UIF_TRANSFER/INT_FG are cleared by the IRQ handler after dispatch.

  if (IS_IN)
  {
    auto* tx_ctrl = get_tx_control_addr(GetNumber());
    *tx_ctrl &= static_cast<uint8_t>(~USBHS_UEP_T_DONE);
    *tx_ctrl = (*tx_ctrl & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_NAK;

    size = last_transfer_size_;

    if (IS_ISO)
    {
      set_tx_len(GetNumber(), 0);
      disable_tx(GetNumber());
    }
  }
  else
  {
    // 对 non-EP0 OUT 端点，完成后恢复到 NAK。
    // For non-EP0 OUT endpoints, restore NAK on completion.
    auto* rx_ctrl = get_rx_control_addr(GetNumber());
    *rx_ctrl &= static_cast<uint8_t>(~USBHS_UEP_R_DONE);
    if (!IS_EP0)
    {
      *rx_ctrl = (*rx_ctrl & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_NAK;
    }
  }

  // TOG 不匹配表示数据同步失败。
  // TOG mismatch indicates data synchronization failure.
  if (IS_OUT && !IS_EP0)
  {
    auto* rx_ctrl = get_rx_control_addr(GetNumber());
    const bool tog_match = ((*rx_ctrl & USBHS_UEP_R_TOG_MATCH) == USBHS_UEP_R_TOG_MATCH);
    if (!tog_match)
    {
      *rx_ctrl = (*rx_ctrl & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_ACK;
      SetState(State::IDLE);
      return;
    }
    if (!UseDoubleBuffer())
    {
      *rx_ctrl ^= USBHS_UEP_R_TOG_DATA1;
    }
  }

  if (IS_EP0 && IS_OUT)
  {
    auto* rx_ctrl = get_rx_control_addr(GetNumber());
    // Do not leave EP0 OUT open here. The upper control stack will re-arm it after the
    // current packet has been fully consumed; otherwise the next session may race stale
    // EP0 software state and duplicate/skip DFU payload blocks.
    // 这里不要直接把 EP0 OUT 留在 ACK。
    // 上层控制传输栈会在“当前包已经被完整消费”后再重新挂接收；
    // 否则下一轮 session 可能撞上陈旧的 EP0 软件状态，导致 DFU 数据块重复或丢失。
    if (size > 0u)
    {
      // 每成功收完一个 EP0 OUT 包，都要推进一次 RX DATA0/DATA1 相位。
      // CH32 USBHS 在多包 EP0 OUT 上，如果这里只是重新开 ACK 而不消费当前 toggle，
      // 后续包会停在这里。
      // Advance the RX DATA0/DATA1 phase once per successfully received EP0 OUT packet.
      // On CH32 USBHS, EP0 multi-packet OUT will stall if we only reopen ACK
      // without consuming the current hardware toggle.
      *rx_ctrl = static_cast<uint8_t>(
          ((*rx_ctrl ^ USBHS_UEP_R_TOG_DATA1) & ~USBHS_UEP_R_RES_MASK) |
          USBHS_UEP_R_RES_NAK);
    }
    else
    {
      *rx_ctrl =
          static_cast<uint8_t>((*rx_ctrl & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_NAK);
    }
  }
  else if (IS_IN && !IS_EP0)
  {
    if (!UseDoubleBuffer())
    {
      auto* tx_ctrl = get_tx_control_addr(GetNumber());
      *tx_ctrl ^= USBHS_UEP_T_TOG_DATA1;
    }
  }
  OnTransferCompleteCallback(true, size);
}

void CH32H41xEndpointOtgHs::SwitchBuffer()
{
  if (!UseDoubleBuffer())
  {
    return;
  }
  if (GetDirection() == Direction::IN)
  {
    const auto* tx_ctrl = get_tx_control_addr(GetNumber());
    const bool tog_is_data1 =
        ((*tx_ctrl & USBHS_UEP_T_TOG_MASK) == USBHS_UEP_T_TOG_DATA1);
    SetActiveBlock(!tog_is_data1);
  }
  else
  {
    const auto* rx_ctrl = get_rx_control_addr(GetNumber());
    const bool tog_is_data1 =
        ((*rx_ctrl & USBHS_UEP_R_TOG_MASK) == USBHS_UEP_R_TOG_DATA1);
    SetActiveBlock(tog_is_data1);
  }
}

#endif
