#include "stm32_usb_ep.hpp"

using namespace LibXR;

static inline bool is_power_of_two(unsigned int n) { return n > 0 && (n & (n - 1)) == 0; }

STM32Endpoint::STM32Endpoint(EPNumber ep_num, stm32_usb_dev_id_t id,
                             PCD_HandleTypeDef* hpcd, Direction dir, size_t fifo_size,
                             LibXR::RawData buffer)
    : Endpoint(ep_num, dir, buffer), hpcd_(hpcd), fifo_size_(fifo_size), id_(id)
{
  ASSERT(fifo_size >= 64);
  ASSERT(is_power_of_two(fifo_size) || fifo_size % 64 == 0);
  ASSERT(is_power_of_two(buffer.size_) || buffer.size_ % 64 == 0);

#if defined(USB_OTG_HS)
  if (id == STM32_USB_OTG_HS)
  {
    map_hs_[EPNumberToInt8(GetNumber())][static_cast<uint8_t>(dir)] = this;
  }
#endif
#if defined(USB_OTG_FS)
  if (id == STM32_USB_OTG_FS)
  {
    map_fs_[EPNumberToInt8(GetNumber())][static_cast<uint8_t>(dir)] = this;
  }
#endif

  if (dir == Direction::IN)
  {
    HAL_PCDEx_SetTxFiFo(hpcd_, EPNumberToInt8(GetNumber()), fifo_size / 4);
  }
  else if (dir == Direction::OUT && ep_num == USB::Endpoint::EPNumber::EP0)
  {
    HAL_PCDEx_SetRxFiFo(hpcd_, fifo_size / 4);
  }
}

bool STM32Endpoint::Configure(const Config& cfg)
{
  ASSERT(cfg.direction == Direction::IN || cfg.direction == Direction::OUT);

  uint8_t addr = EPNumberToAddr(GetNumber(), cfg.direction);
  uint8_t type = static_cast<uint8_t>(cfg.type);
  auto& ep_cfg = GetConfig();
  size_t packet_size_limit = 0;

  ep_cfg = cfg;

  switch (cfg.type)
  {
    case Type::BULK:
      if (hpcd_->Init.speed == PCD_SPEED_FULL ||
          hpcd_->Init.speed == PCD_SPEED_HIGH_IN_FULL)
      {
        packet_size_limit = 64;
      }
      else
      {
        packet_size_limit = 512;
      }
      break;
    case Type::INTERRUPT:
      if (hpcd_->Init.speed == PCD_SPEED_FULL ||
          hpcd_->Init.speed == PCD_SPEED_HIGH_IN_FULL)
      {
        packet_size_limit = 64;
      }
      else
      {
        packet_size_limit = 1024;
      }
      break;
    case Type::ISOCHRONOUS:
      if (hpcd_->Init.speed == PCD_SPEED_FULL ||
          hpcd_->Init.speed == PCD_SPEED_HIGH_IN_FULL)
      {
        packet_size_limit = 1023;
      }
      else
      {
        packet_size_limit = 1024;
      }
      break;
    case Type::CONTROL:
      packet_size_limit = 64;
      break;
    default:
      break;
  }

  if (packet_size_limit > fifo_size_)
  {
    packet_size_limit = fifo_size_;
  }

  auto buffer = GetBuffer();

  if (packet_size_limit > buffer.size_)
  {
    packet_size_limit = buffer.size_;
  }

  size_t max_packet_size = cfg.max_packet_size;

  if (max_packet_size > packet_size_limit)
  {
    max_packet_size = packet_size_limit;
  }

  ep_cfg.max_packet_size = max_packet_size;

  if (max_packet_size < 8)
  {
    max_packet_size = 8;
  }

  ASSERT(is_power_of_two(max_packet_size) && max_packet_size >= 8);

  if (HAL_PCD_EP_Open(hpcd_, addr, max_packet_size, type) == HAL_OK)
  {
    SetState(State::IDLE);
    return true;
  }
  SetState(State::ERROR);
  return false;
}

void STM32Endpoint::Close()
{
  uint8_t addr = EPNumberToAddr(GetNumber(), GetDirection());
  HAL_PCD_EP_Close(hpcd_, addr);
  SetState(State::DISABLED);
}

ErrorCode STM32Endpoint::Transfer(size_t size)
{
  if (GetState() == State::BUSY)
  {
    return ErrorCode::BUSY;
  }

  bool is_in = GetDirection() == Direction::IN;
  auto ep_addr = EPNumberToAddr(GetNumber(), GetDirection());

  PCD_EPTypeDef* ep = is_in ? &hpcd_->IN_ep[ep_addr & EP_ADDR_MSK]
                            : &hpcd_->OUT_ep[ep_addr & EP_ADDR_MSK];

  auto buffer = GetBuffer();

  if (buffer.size_ < size)
  {
    return ErrorCode::NO_BUFF;
  }

  ep->xfer_buff = reinterpret_cast<uint8_t*>(buffer.addr_);

  if (UseDoubleBuffer() && GetDirection() == Direction::IN && size > 0)
  {
    SwitchBuffer();
  }

  ep->xfer_len = size;
  ep->xfer_count = 0U;
  ep->is_in = is_in ? 1U : 0U;
  ep->num = ep_addr & EP_ADDR_MSK;

  if (hpcd_->Init.dma_enable == 1U)
  {
    ep->dma_addr = reinterpret_cast<uint32_t>(ep->xfer_buff);
  }

  SetLastTransferSize(size);

  SetState(State::BUSY);

  auto ans = USB_EPStartXfer(hpcd_->Instance, ep, hpcd_->Init.dma_enable);

  if (ans == HAL_OK)
  {
    return ErrorCode::OK;
  }
  else
  {
    SetState(State::ERROR);
    return ErrorCode::FAILED;
  }
}

ErrorCode STM32Endpoint::Stall()
{
  if (GetState() != State::IDLE)
  {
    return ErrorCode::BUSY;
  }

  uint8_t addr = EPNumberToAddr(GetNumber(), GetDirection());
  if (HAL_PCD_EP_SetStall(hpcd_, addr) == HAL_OK)
  {
    SetState(State::STALLED);
    return ErrorCode::OK;
  }
  else
  {
    SetState(State::ERROR);
    return ErrorCode::FAILED;
  }
}

ErrorCode STM32Endpoint::ClearStall()
{
  if (GetState() != State::STALLED)
  {
    return ErrorCode::FAILED;
  }

  uint8_t addr = EPNumberToAddr(GetNumber(), GetDirection());
  if (HAL_PCD_EP_ClrStall(hpcd_, addr) == HAL_OK)
  {
    SetState(State::IDLE);
    return ErrorCode::OK;
  }
  else
  {
    SetState(State::ERROR);
    return ErrorCode::FAILED;
  }
}

size_t STM32Endpoint::MaxTransferSize() const
{
  if (GetNumber() == USB::Endpoint::EPNumber::EP0)
  {
    return MaxPacketSize();
  }
  else
  {
    return GetBuffer().size_;
  }
}

// --- HAL C 回调桥接 ---
// NOLINTNEXTLINE
static STM32Endpoint* GetEndpoint(PCD_HandleTypeDef* hpcd, uint8_t epnum, bool is_in)
{
  auto id = STM32USBDeviceGetID(hpcd);
#if defined(USB_OTG_HS)
  if (id == STM32_USB_OTG_HS)
  {
    return STM32Endpoint::map_hs_[epnum & 0x7F][static_cast<uint8_t>(is_in)];
  }
#endif
#if defined(USB_OTG_FS)
  if (id == STM32_USB_OTG_FS)
  {
    return STM32Endpoint::map_fs_[epnum & 0x7F][static_cast<uint8_t>(is_in)];
  }
#endif
  return nullptr;
}

extern "C" void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef* hpcd, uint8_t epnum)
{
  auto id = STM32USBDeviceGetID(hpcd);

  ASSERT(id < STM32_USB_DEV_ID_NUM);

  auto ep = GetEndpoint(hpcd, epnum, true);

  if (!ep || ep->hpcd_ != hpcd)
  {
    return;
  }

  PCD_EPTypeDef* ep_handle = &hpcd->IN_ep[epnum & EP_ADDR_MSK];

  size_t actual_transfer_size = ep_handle->xfer_count;

  ep->OnTransferCompleteISR(true, actual_transfer_size);
}

extern "C" void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef* hpcd, uint8_t epnum)
{
  auto id = STM32USBDeviceGetID(hpcd);

  ASSERT(id < STM32_USB_DEV_ID_NUM);

  auto ep = GetEndpoint(hpcd, epnum, false);

  if (!ep || ep->hpcd_ != hpcd)
  {
    return;
  }

  PCD_EPTypeDef* ep_handle = &hpcd->OUT_ep[epnum & EP_ADDR_MSK];

  size_t actual_transfer_size = ep_handle->xfer_count;

  ep->OnTransferCompleteISR(true, actual_transfer_size);
}
