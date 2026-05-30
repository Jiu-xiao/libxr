#include <cstdint>

#include "ch32_usb_endpoint.hpp"

using namespace LibXR;

#if defined(LIBXR_CH32_HAS_USB_OTG_SS)

namespace
{

using Direction = LibXR::USB::Endpoint::Direction;
using EPNumber = LibXR::USB::Endpoint::EPNumber;
using Type = LibXR::USB::Endpoint::Type;

constexpr uint8_t OUT_IDX = static_cast<uint8_t>(Direction::OUT);
constexpr uint8_t IN_IDX = static_cast<uint8_t>(Direction::IN);

static uint16_t DecodeMaxPacketSize(const LibXR::USB::Endpoint::Config& cfg)
{
  if (cfg.type == Type::CONTROL)
  {
    return 512u;
  }
  if (cfg.type == Type::BULK)
  {
    return 1024u;
  }
  return cfg.max_packet_size;
}

static uint8_t ClampBurstCount(uint8_t max_burst)
{
  if (max_burst == 0u)
  {
    return 1u;
  }
  return (max_burst > 16u) ? 16u : max_burst;
}

static size_t GetMaxChainPackets(uint8_t max_burst)
{
  constexpr size_t USBSS_CHAIN_PACKET_LIMIT = 15u;
  const size_t burst_packets = static_cast<size_t>(ClampBurstCount(max_burst));
  return (burst_packets < USBSS_CHAIN_PACKET_LIMIT) ? burst_packets
                                                    : USBSS_CHAIN_PACKET_LIMIT;
}

static uint8_t GetChainPacketCount(size_t size, size_t packet_size, uint8_t max_burst)
{
  if (packet_size == 0u)
  {
    return 1u;
  }

  const size_t max_packets = GetMaxChainPackets(max_burst);
  if (size == 0u)
  {
    return 1u;
  }

  size_t packets = (size + packet_size - 1u) / packet_size;
  if (packets == 0u)
  {
    packets = 1u;
  }
  if (packets > max_packets)
  {
    packets = max_packets;
  }
  return static_cast<uint8_t>(packets);
}

static uint16_t GetLastPacketLength(size_t size, size_t packet_size)
{
  if (size == 0u || packet_size == 0u)
  {
    return 0u;
  }

  const size_t rem = size % packet_size;
  return static_cast<uint16_t>(rem == 0u ? packet_size : rem);
}

static uint8_t DefaultTxCfg(Type type)
{
  uint8_t cfg = USBSS_EP_TX_CHAIN_AUTO | USBSS_EP_TX_ERDY_AUTO | USBSS_EP_TX_SEQ_AUTO;
  if (type == Type::ISOCHRONOUS)
  {
    cfg |= USBSS_EP_TX_ISO_MODE;
  }
  return cfg;
}

static uint8_t DefaultRxCfg(Type type)
{
  uint8_t cfg = USBSS_EP_RX_CHAIN_AUTO | USBSS_EP_RX_ERDY_AUTO | USBSS_EP_RX_SEQ_AUTO;
  if (type == Type::ISOCHRONOUS)
  {
    cfg |= USBSS_EP_RX_ISO_MODE;
  }
  return cfg;
}

static volatile USBSS_EP_TX_TypeDef* GetTxEndpoint(EPNumber ep_num)
{
  ASSERT(ep_num >= EPNumber::EP1 &&
         ep_num < static_cast<EPNumber>(CH32EndpointOtgSs::EP_OTG_SS_MAX_SIZE));
  return reinterpret_cast<volatile USBSS_EP_TX_TypeDef*>(
      reinterpret_cast<volatile uint8_t*>(&USBSSD->EP1_TX) +
      (static_cast<uint8_t>(ep_num) - 1u) *
          (sizeof(USBSS_EP_TX_TypeDef) + sizeof(USBSS_EP_RX_TypeDef)));
}

static volatile USBSS_EP_RX_TypeDef* GetRxEndpoint(EPNumber ep_num)
{
  ASSERT(ep_num >= EPNumber::EP1 &&
         ep_num < static_cast<EPNumber>(CH32EndpointOtgSs::EP_OTG_SS_MAX_SIZE));
  return reinterpret_cast<volatile USBSS_EP_RX_TypeDef*>(
      reinterpret_cast<volatile uint8_t*>(&USBSSD->EP1_RX) +
      (static_cast<uint8_t>(ep_num) - 1u) *
          (sizeof(USBSS_EP_TX_TypeDef) + sizeof(USBSS_EP_RX_TypeDef)));
}

static uint16_t EndpointEnableBit(EPNumber ep_num)
{
  return static_cast<uint16_t>(1u << static_cast<uint8_t>(ep_num));
}

static size_t GetRxTransferSize(EPNumber ep_num)
{
  if (ep_num == EPNumber::EP0)
  {
    return static_cast<size_t>(USBSSD->UEP0_RX_CTRL & USBSS_EP0_RX_LEN_MASK);
  }

  const auto* rx = GetRxEndpoint(ep_num);
  const size_t packets = static_cast<size_t>(rx->UEP_RX_CHAIN_NUMP & 0x0Fu);
  const size_t last_packet_len = static_cast<size_t>(rx->UEP_RX_CHAIN_LEN);
  const size_t packet_size = static_cast<size_t>(rx->UEP_RX_DMA_OFS);
  if (packets <= 1u)
  {
    return last_packet_len;
  }
  return ((packets - 1u) * packet_size) + last_packet_len;
}

static void ArmEp0In(uint8_t seq, size_t size)
{
  uint32_t ctrl = (static_cast<uint32_t>(seq & 0x1Fu) << 16) |
                  USBSS_EP0_TX_DPH | static_cast<uint32_t>(size & USBSS_EP0_TX_LEN_MASK);
  USBSSD->UEP0_TX_CTRL = ctrl | USBSS_EP0_TX_ERDY;
}

static void ArmEp0Out(uint8_t seq)
{
  USBSSD->UEP0_RX_CTRL = (static_cast<uint32_t>(seq & 0x1Fu) << 16) | USBSS_EP0_RX_ERDY |
                         USBSS_EP0_RX_ACK;
}

}  // namespace

CH32EndpointOtgSs::CH32EndpointOtgSs(EPNumber ep_num, Direction dir,
                                     LibXR::RawData buffer, uint8_t max_burst)
    : Endpoint(ep_num, dir, buffer),
      max_burst_(max_burst == 0u ? 1u : max_burst),
      dma_buffer_(buffer),
      bank_size_(buffer.size_)
{
  map_otg_ss_[EPNumberToInt8(GetNumber())][static_cast<uint8_t>(dir)] = this;
}

void CH32EndpointOtgSs::Configure(const Config& cfg)
{
  auto& ep_cfg = GetConfig();
  ep_cfg = cfg;
  ep_cfg.max_packet_size = DecodeMaxPacketSize(ep_cfg);
  ep_cfg.double_buffer =
      (GetNumber() != EPNumber::EP0) && ep_cfg.double_buffer &&
      (dma_buffer_.size_ >= (static_cast<size_t>(ep_cfg.max_packet_size) * 2u));

  bank_size_ = ep_cfg.double_buffer ? (dma_buffer_.size_ / 2u) : dma_buffer_.size_;
  active_bank_ = 0u;
  SetActiveBlock(false);

  ASSERT(GetBuffer().addr_ != nullptr);
  ASSERT(bank_size_ >= ep_cfg.max_packet_size);
  ASSERT((reinterpret_cast<uintptr_t>(GetBuffer().addr_) & 0x3u) == 0u);

  seq_ = 0u;

  if (GetNumber() == EPNumber::EP0)
  {
    if (GetDirection() == Direction::IN)
    {
      USBSSD->UEP0_TX_DMA = reinterpret_cast<uint32_t>(GetBuffer().addr_);
      USBSSD->UEP0_TX_DMA_OFS = ep_cfg.max_packet_size;
      USBSSD->UEP0_TX_CTRL = 0u;
    }
    else
    {
      USBSSD->UEP0_RX_DMA = reinterpret_cast<uint32_t>(GetBuffer().addr_);
      USBSSD->UEP0_RX_DMA_OFS = ep_cfg.max_packet_size;
      USBSSD->UEP0_RX_CTRL = 0u;
    }
    SetState(State::IDLE);
    return;
  }

  const uint16_t enable_bit = EndpointEnableBit(GetNumber());
  if (GetDirection() == Direction::IN)
  {
    USBSSD->UEP_TX_EN |= enable_bit;
    auto* tx = GetTxEndpoint(GetNumber());
    tx->UEP_TX_CFG = DefaultTxCfg(GetType());
    tx->UEP_TX_CR = ClampBurstCount(max_burst_);
    tx->UEP_TX_SEQ = 0u;
    tx->UEP_TX_CHAIN_CR = 0u;
    tx->UEP_TX_CHAIN_ST = 0u;
    tx->UEP_TX_CHAIN_LEN = 0u;
    tx->UEP_TX_CHAIN_EXP_NUMP = 0u;
    tx->UEP_TX_DMA_OFS = ep_cfg.max_packet_size;
    tx->UEP_TX_DMA = reinterpret_cast<uint32_t>(GetBuffer().addr_);
  }
  else
  {
    USBSSD->UEP_RX_EN |= enable_bit;
    auto* rx = GetRxEndpoint(GetNumber());
    rx->UEP_RX_CFG = DefaultRxCfg(GetType());
    rx->UEP_RX_CR = ClampBurstCount(max_burst_);
    rx->UEP_RX_SEQ = 0u;
    rx->UEP_RX_CHAIN_CR = 0u;
    rx->UEP_RX_CHAIN_ST = 0u;
    rx->UEP_RX_CHAIN_LEN = 0u;
    rx->UEP_RX_CHAIN_MAX_NUMP = 0u;
    rx->UEP_RX_DMA_OFS = ep_cfg.max_packet_size;
    rx->UEP_RX_DMA = reinterpret_cast<uint32_t>(GetBuffer().addr_);
  }

  SetState(State::IDLE);
}

void CH32EndpointOtgSs::Close()
{
  if (GetNumber() == EPNumber::EP0)
  {
    if (GetDirection() == Direction::IN)
    {
      USBSSD->UEP0_TX_CTRL = 0u;
    }
    else
    {
      USBSSD->UEP0_RX_CTRL = 0u;
    }
    SetState(State::DISABLED);
    return;
  }

  const uint16_t enable_bit = EndpointEnableBit(GetNumber());
  if (GetDirection() == Direction::IN)
  {
    USBSSD->UEP_TX_EN &= static_cast<uint16_t>(~enable_bit);
    auto* tx = GetTxEndpoint(GetNumber());
    tx->UEP_TX_CR = USBSS_EP_TX_CLR | USBSS_EP_TX_CHAIN_CLR;
    tx->UEP_TX_CHAIN_ST = USBSS_EP_TX_CHAIN_IF;
  }
  else
  {
    USBSSD->UEP_RX_EN &= static_cast<uint16_t>(~enable_bit);
    auto* rx = GetRxEndpoint(GetNumber());
    rx->UEP_RX_CR = USBSS_EP_RX_CLR | USBSS_EP_RX_CHAIN_CLR;
    rx->UEP_RX_CHAIN_ST = USBSS_EP_RX_CHAIN_IF;
  }

  active_bank_ = 0u;
  SetActiveBlock(false);
  SetState(State::DISABLED);
}

size_t CH32EndpointOtgSs::MaxTransferSize() const
{
  if (GetNumber() == EPNumber::EP0)
  {
    return MaxPacketSize();
  }

  const size_t packet_size = MaxPacketSize();
  const size_t buffer_size = bank_size_;
  const size_t chain_window = packet_size * GetMaxChainPackets(max_burst_);
  return (buffer_size < chain_window) ? buffer_size : chain_window;
}

uint8_t CH32EndpointOtgSs::MaxBurst() const
{
  if (GetNumber() == EPNumber::EP0)
  {
    return 1u;
  }

  return static_cast<uint8_t>(GetMaxChainPackets(max_burst_));
}

ErrorCode CH32EndpointOtgSs::Transfer(size_t size)
{
  if (GetState() == State::BUSY)
  {
    return ErrorCode::BUSY;
  }

  auto buffer = GetBuffer();
  if (buffer.addr_ == nullptr || size > buffer.size_)
  {
    return ErrorCode::NO_BUFF;
  }
  if (size > MaxTransferSize())
  {
    return ErrorCode::ARG_ERR;
  }

  last_transfer_size_ = size;
  SetState(State::BUSY);

  if (GetNumber() == EPNumber::EP0)
  {
    if (GetDirection() == Direction::IN)
    {
      ArmEp0In(seq_, size);
      if (size == 0u)
      {
        // On CH32 USBSS, EP0 status-IN for control-write requests needs RX
        // ack armed as well, otherwise the host SET_ADDRESS flow can stop
        // after SETUP with no STATUS completion interrupt.
        USBSSD->UEP0_RX_CTRL = USBSS_EP0_RX_ERDY | USBSS_EP0_RX_ACK;
      }
    }
    else
    {
      ArmEp0Out(seq_);
    }
    return ErrorCode::OK;
  }

  if (GetDirection() == Direction::IN)
  {
    auto* tx = GetTxEndpoint(GetNumber());
    tx->UEP_TX_DMA = reinterpret_cast<uint32_t>(buffer.addr_);
    tx->UEP_TX_DMA_OFS = MaxPacketSize();
    if (size == 0u)
    {
      tx->UEP_TX_CHAIN_LEN = 0u;
      tx->UEP_TX_CHAIN_EXP_NUMP = 0u;
    }
    else
    {
      const uint8_t packet_count = GetChainPacketCount(size, MaxPacketSize(), max_burst_);
      tx->UEP_TX_CHAIN_LEN = GetLastPacketLength(size, MaxPacketSize());
      tx->UEP_TX_CHAIN_EXP_NUMP = packet_count;
    }
  }
  else
  {
    auto* rx = GetRxEndpoint(GetNumber());
    const uint8_t packet_count = GetChainPacketCount(size, MaxPacketSize(), max_burst_);
    rx->UEP_RX_DMA = reinterpret_cast<uint32_t>(buffer.addr_);
    rx->UEP_RX_DMA_OFS = MaxPacketSize();
    rx->UEP_RX_CR = ClampBurstCount(max_burst_);
    rx->UEP_RX_CHAIN_MAX_NUMP = packet_count;
    rx->UEP_RX_CHAIN_ST = USBSS_EP_RX_CHAIN_IF;
  }

  return ErrorCode::OK;
}

ErrorCode CH32EndpointOtgSs::Stall()
{
  if (GetState() != State::IDLE && !(GetNumber() == EPNumber::EP0 && GetState() == State::BUSY))
  {
    return ErrorCode::BUSY;
  }

  if (GetNumber() == EPNumber::EP0)
  {
    if (GetDirection() == Direction::IN)
    {
      USBSSD->UEP0_TX_CTRL = USBSS_EP0_TX_STALL;
    }
    else
    {
      USBSSD->UEP0_RX_CTRL = USBSS_EP0_RX_ERDY | USBSS_EP0_RX_STALL;
    }
  }
  else if (GetDirection() == Direction::IN)
  {
    GetTxEndpoint(GetNumber())->UEP_TX_CR |= USBSS_EP_TX_HALT;
  }
  else
  {
    GetRxEndpoint(GetNumber())->UEP_RX_CR |= USBSS_EP_RX_HALT;
  }

  SetState(State::STALLED);
  return ErrorCode::OK;
}

ErrorCode CH32EndpointOtgSs::ClearStall()
{
  if (GetState() != State::STALLED)
  {
    return ErrorCode::FAILED;
  }

  seq_ = 0u;

  if (GetNumber() == EPNumber::EP0)
  {
    if (GetDirection() == Direction::IN)
    {
      USBSSD->UEP0_TX_CTRL = 0u;
    }
    else
    {
      USBSSD->UEP0_RX_CTRL = 0u;
    }
  }
  else if (GetDirection() == Direction::IN)
  {
    auto* tx = GetTxEndpoint(GetNumber());
    tx->UEP_TX_CR = USBSS_EP_TX_CLR | USBSS_EP_TX_CHAIN_CLR;
    tx->UEP_TX_CR = 0u;
  }
  else
  {
    auto* rx = GetRxEndpoint(GetNumber());
    rx->UEP_RX_CR = USBSS_EP_RX_CLR | USBSS_EP_RX_CHAIN_CLR;
    rx->UEP_RX_CR = 0u;
    rx->UEP_RX_CHAIN_MAX_NUMP = 0u;
  }

  active_bank_ = 0u;
  SetActiveBlock(false);
  SetState(State::IDLE);
  return ErrorCode::OK;
}

void CH32EndpointOtgSs::TransferComplete(size_t size)
{
  if (GetDirection() == Direction::IN)
  {
    size = last_transfer_size_;
  }
  else if (size == 0u)
  {
    size = GetRxTransferSize(GetNumber());
  }

  if (GetNumber() == EPNumber::EP0)
  {
    if (GetDirection() == Direction::IN)
    {
      USBSSD->UEP0_TX_CTRL = 0u;
    }
    else
    {
      USBSSD->UEP0_RX_CTRL = 0u;
    }
    seq_ = static_cast<uint8_t>((seq_ + 1u) & 0x1Fu);
    OnTransferCompleteCallback(true, size);
    return;
  }

  if (GetDirection() == Direction::IN)
  {
    auto* tx = GetTxEndpoint(GetNumber());
    tx->UEP_TX_CHAIN_ST |= USBSS_EP_TX_CHAIN_IF;
    if (UseDoubleBuffer())
    {
      active_bank_ ^= 0x01u;
      SetActiveBlock(active_bank_ != 0u);
    }
  }
  else
  {
    auto* rx = GetRxEndpoint(GetNumber());
    seq_ = static_cast<uint8_t>(rx->UEP_RX_SEQ & USBSS_EP_RX_SEQ_NUM_MASK);
  }

  OnTransferCompleteCallback(true, size);
}

void CH32EndpointOtgSs::SwitchBuffer()
{
  if (!UseDoubleBuffer() || GetNumber() == EPNumber::EP0)
  {
    return;
  }

  active_bank_ ^= 0x01u;
  SetActiveBlock(active_bank_ != 0u);
}

#endif  // defined(LIBXR_CH32_HAS_USB_OTG_SS)
