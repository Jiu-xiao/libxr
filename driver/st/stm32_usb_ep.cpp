#include "stm32_usb_ep.hpp"

using namespace LibXR;

STM32Endpoint::STM32Endpoint(uint8_t ep_num, PCD_HandleTypeDef* hpcd, Direction dir,
                             size_t fifo_size)
    : Endpoint(ep_num, dir), hpcd_(hpcd)
{
  ASSERT(ep_num <= EP_MAX_SIZE);
  ASSERT(fifo_size >= 0x40);

  config_.direction = dir;
  map_[number_][static_cast<uint8_t>(dir)] = this;

  if (dir == Direction::IN)
  {
    // IN端点需要指定FIFO大小
    ASSERT(fifo_size > 0);
    HAL_PCDEx_SetTxFiFo(hpcd_, number_, fifo_size);
  }

  else if (dir == Direction::OUT && ep_num == 0)
  {
    // OUT端点不需要FIFO大小
    HAL_PCDEx_SetRxFiFo(hpcd_, fifo_size);
  }
}

bool STM32Endpoint::Configure(const Config& cfg)
{
  ASSERT(cfg.max_packet_size <= 64);
  ASSERT(cfg.direction == Direction::IN || cfg.direction == Direction::OUT);

  config_ = cfg;
  uint8_t addr = (cfg.direction == Direction::IN) ? (number_ | 0x80) : number_;
  uint8_t type = static_cast<uint8_t>(cfg.type);
  // 调用HAL底层API打开端点
  if (HAL_PCD_EP_Open(hpcd_, addr, cfg.max_packet_size, type) == HAL_OK)
  {
    state_ = State::IDLE;
    return true;
  }
  state_ = State::ERROR;
  return false;
}

void STM32Endpoint::Close()
{
  uint8_t addr = (config_.direction == Direction::IN) ? (number_ | 0x80) : number_;
  HAL_PCD_EP_Close(hpcd_, addr);
  state_ = State::DISABLED;
}

ErrorCode STM32Endpoint::Write(ConstRawData& data)
{
  if (config_.direction != Direction::IN || state_ != State::IDLE)
  {
    return ErrorCode::ARG_ERR;
  }
  uint8_t addr = number_ | 0x80;
  // NOLINTBEGIN
  auto ret = HAL_PCD_EP_Transmit(
      hpcd_, addr, reinterpret_cast<uint8_t*>(const_cast<void*>(data.addr_)), data.size_);
  // NOLINTEND
  if (ret == HAL_OK)
  {
    state_ = State::BUSY;
    return ErrorCode::OK;
  }
  else
  {
    state_ = State::ERROR;
    return ErrorCode::FAILED;
  }
}

ErrorCode STM32Endpoint::Read(RawData& data)
{
  if (config_.direction != Direction::OUT || state_ != State::IDLE)
  {
    return ErrorCode::ARG_ERR;
  }
  uint8_t addr = number_;
  auto ret =
      HAL_PCD_EP_Receive(hpcd_, addr, reinterpret_cast<uint8_t*>(data.addr_), data.size_);
  if (ret == HAL_OK)
  {
    state_ = State::BUSY;
    return ErrorCode::OK;
  }
  else
  {
    state_ = State::ERROR;
    return ErrorCode::FAILED;
  }
}

ErrorCode STM32Endpoint::Stall()
{
  if (state_ != State::IDLE)
  {
    return ErrorCode::BUSY;
  }

  uint8_t addr = (config_.direction == Direction::IN) ? (number_ | 0x80) : number_;
  if (HAL_PCD_EP_SetStall(hpcd_, addr) == HAL_OK)
  {
    state_ = State::STALLED;
    return ErrorCode::OK;
  }
  else
  {
    state_ = State::ERROR;
    return ErrorCode::FAILED;
  }
}

ErrorCode STM32Endpoint::ClearStall()
{
  if (state_ != State::STALLED)
  {
    return ErrorCode::FAILED;
  }

  uint8_t addr = (config_.direction == Direction::IN) ? (number_ | 0x80) : number_;
  if (HAL_PCD_EP_ClrStall(hpcd_, addr) == HAL_OK)
  {
    state_ = State::IDLE;
    return ErrorCode::OK;
  }
  else
  {
    state_ = State::ERROR;
    return ErrorCode::FAILED;
  }
}

void STM32Endpoint::OnDataInISR()
{
  state_ = State::IDLE;  // 传输完成，状态回到IDLE
  if (!on_transfer_complete_.Empty())
  {
    ConstRawData tmp;
    on_transfer_complete_.Run(true, tmp);
  }
}

void STM32Endpoint::OnDataOutISR()
{
  state_ = State::IDLE;
  if (!on_transfer_complete_.Empty())
  {
    ConstRawData tmp;
    on_transfer_complete_.Run(true, tmp);
  }
}

// --- HAL C 回调桥接 ---

extern "C" void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef* hpcd, uint8_t epnum)
{
  STM32Endpoint* ep =
      STM32Endpoint::map_[epnum & 0x7F]
                         [static_cast<uint8_t>(USB::Endpoint::Direction::IN)];

  if (ep && ep->hpcd_ == hpcd)
  {
    ep->OnDataInISR();
  }
}

extern "C" void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef* hpcd, uint8_t epnum)
{
  STM32Endpoint* ep =
      STM32Endpoint::map_[epnum & 0x7F]
                         [static_cast<uint8_t>(USB::Endpoint::Direction::OUT)];

  if (ep && ep->hpcd_ == hpcd)
  {
    ep->OnDataOutISR();
  }
}
