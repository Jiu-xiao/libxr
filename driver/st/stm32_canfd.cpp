#include "stm32_canfd.hpp"

#ifdef HAL_FDCAN_MODULE_ENABLED

using namespace LibXR;

STM32CANFD* STM32CANFD::map[STM32_FDCAN_NUMBER] = {nullptr};

/**
 * @brief 获取 FDCAN ID Get FDCAN ID
 *
 * @param addr FDCAN外设地址 FDCAN device address
 * @return stm32_fdcan_id_t
 */
stm32_fdcan_id_t STM32_FDCAN_GetID(FDCAN_GlobalTypeDef* addr)
{
  if (addr == nullptr)
  {  // NOLINT
    return stm32_fdcan_id_t::STM32_FDCAN_ID_ERROR;
  }
#ifdef FDCAN1
  else if (addr == FDCAN1)
  {  // NOLINT
    return stm32_fdcan_id_t::STM32_FDCAN1;
  }
#endif
#ifdef FDCAN2
  else if (addr == FDCAN2)
  {  // NOLINT
    return stm32_fdcan_id_t::STM32_FDCAN2;
  }
#endif
#ifdef FDCAN3
  else if (addr == FDCAN3)
  {  // NOLINT
    return stm32_fdcan_id_t::STM32_FDCAN3;
  }
#endif
  else
  {
    return stm32_fdcan_id_t::STM32_FDCAN_ID_ERROR;
  }
}

STM32CANFD::STM32CANFD(FDCAN_HandleTypeDef* hcan, uint32_t queue_size)
    : FDCAN(),
      hcan_(hcan),
      id_(STM32_FDCAN_GetID(hcan->Instance)),
      tx_pool_(queue_size),
      tx_pool_fd_(queue_size)
{
  map[id_] = this;
  Init();
}

ErrorCode STM32CANFD::Init(void)
{
  FDCAN_FilterTypeDef can_filter = {};
  can_filter.IdType = FDCAN_STANDARD_ID;
  can_filter.FilterType = FDCAN_FILTER_MASK;
  can_filter.FilterID1 = 0x0000;
  can_filter.FilterID2 = 0x0000;
  can_filter.FilterIndex = 0;

#ifdef FDCAN3
  if (id_ == STM32_FDCAN1)
  {
    can_filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
  }
  else if (id_ == STM32_FDCAN2)
  {
    can_filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO1;
  }
  else if (id_ == STM32_FDCAN3)
  {
    can_filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO1;
  }
#else
#ifdef FDCAN2
  if (id_ == STM32_FDCAN1)
  {
    can_filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
  }
  else if (id_ == STM32_FDCAN2)
  {
    can_filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO1;
  }
#else
  can_filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
#endif
#endif

  if (HAL_FDCAN_ConfigFilter(hcan_, &can_filter) != HAL_OK)
  {
    return ErrorCode::FAILED;
  }

  can_filter.IdType = FDCAN_EXTENDED_ID;

  if (HAL_FDCAN_ConfigFilter(hcan_, &can_filter) != HAL_OK)
  {
    return ErrorCode::FAILED;
  }

  if (HAL_FDCAN_Start(hcan_) != HAL_OK)
  {
    return ErrorCode::FAILED;
  }

  if (can_filter.FilterConfig == FDCAN_FILTER_TO_RXFIFO0)
  {
    HAL_FDCAN_ActivateNotification(hcan_, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0);
  }
  else
  {
    HAL_FDCAN_ActivateNotification(hcan_, FDCAN_IT_RX_FIFO1_NEW_MESSAGE, 0);
  }

  HAL_FDCAN_ActivateNotification(hcan_, FDCAN_IT_TX_FIFO_EMPTY, 0);

  return ErrorCode::OK;
}

ErrorCode STM32CANFD::AddMessage(const ClassicPack& pack)
{
  FDCAN_TxHeaderTypeDef header;  // NOLINT

  header.Identifier = pack.id;

  switch (pack.type)
  {
    case Type::STANDARD:
      ASSERT(pack.id <= 0x7FF);
      header.IdType = FDCAN_STANDARD_ID;
      header.TxFrameType = FDCAN_DATA_FRAME;
      break;

    case Type::EXTENDED:
      ASSERT(pack.id <= 0x1FFFFFFF);
      header.IdType = FDCAN_EXTENDED_ID;
      header.TxFrameType = FDCAN_DATA_FRAME;
      break;

    case Type::REMOTE_STANDARD:
      ASSERT(pack.id <= 0x7FF);
      header.IdType = FDCAN_STANDARD_ID;
      header.TxFrameType = FDCAN_REMOTE_FRAME;
      break;

    case Type::REMOTE_EXTENDED:
      ASSERT(pack.id <= 0x1FFFFFFF);
      header.IdType = FDCAN_EXTENDED_ID;
      header.TxFrameType = FDCAN_REMOTE_FRAME;
      break;
    default:
      ASSERT(false);
      return ErrorCode::FAILED;
  }

  header.DataLength = FDCAN_DLC_BYTES_8;
  header.ErrorStateIndicator = FDCAN_ESI_PASSIVE;
  header.BitRateSwitch = FDCAN_BRS_OFF;
  header.FDFormat = FDCAN_CLASSIC_CAN;
  header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
  header.MessageMarker = 0x01;

  while (true)
  {
    uint32_t slot = 0;

    if (HAL_FDCAN_AddMessageToTxFifoQ(hcan_, &header, pack.data) != HAL_OK)
    {
      if (tx_pool_.Put(pack, slot) != ErrorCode::OK)
      {
        return ErrorCode::FAILED;
      }
    }
    else
    {
      return ErrorCode::OK;
    }

    if (bus_busy_.load(std::memory_order_acquire) == 0 &&
        tx_pool_.RecycleSlot(slot) == ErrorCode::OK)
    {
      continue;
    }
    else
    {
      return ErrorCode::OK;
    }
  }
}

ErrorCode STM32CANFD::AddMessage(const FDPack& pack)
{
  FDCAN_TxHeaderTypeDef header;
  ASSERT(pack.len <= 64);

  header.Identifier = pack.id;

  switch (pack.type)
  {
    case Type::STANDARD:
      ASSERT(pack.id <= 0x7FF);
      header.IdType = FDCAN_STANDARD_ID;
      header.TxFrameType = FDCAN_DATA_FRAME;
      break;

    case Type::EXTENDED:
      ASSERT(pack.id <= 0x1FFFFFFF);
      header.IdType = FDCAN_EXTENDED_ID;
      header.TxFrameType = FDCAN_DATA_FRAME;
      break;

    case Type::REMOTE_STANDARD:
    case Type::REMOTE_EXTENDED:
    default:
      ASSERT(false);
      return ErrorCode::FAILED;
  }

  if (pack.len <= 8)
  {
    header.DataLength = FDCAN_PACK_LEN_MAP[pack.len];
  }
  else if (pack.len <= 24)
  {
    header.DataLength = FDCAN_PACK_LEN_MAP[(pack.len - 9) / 4 + 1 + 8];
  }
  else if (pack.len < 32)
  {
    header.DataLength = FDCAN_DLC_BYTES_32;
  }
  else if (pack.len < 48)
  {
    header.DataLength = FDCAN_DLC_BYTES_48;
  }
  else
  {
    header.DataLength = FDCAN_DLC_BYTES_64;
  }

  header.ErrorStateIndicator = FDCAN_ESI_PASSIVE;
  header.BitRateSwitch = FDCAN_BRS_ON;
  header.FDFormat = FDCAN_FD_CAN;
  header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
  header.MessageMarker = 0x00;

  while (true)
  {
    uint32_t slot = 0;

    if (HAL_FDCAN_AddMessageToTxFifoQ(hcan_, &header, pack.data) != HAL_OK)
    {
      if (tx_pool_fd_.Put(pack, slot) != ErrorCode::OK)
      {
        return ErrorCode::FAILED;
      }
    }
    else
    {
      return ErrorCode::OK;
    }

    if (bus_busy_.load(std::memory_order_acquire) == 0 &&
        tx_pool_fd_.RecycleSlot(slot) == ErrorCode::OK)
    {
      continue;
    }
    else
    {
      return ErrorCode::OK;
    }
  }
}

void STM32CANFD::ProcessRxInterrupt(uint32_t fifo)
{
  if (HAL_FDCAN_GetRxMessage(hcan_, fifo, &rx_buff_.header, rx_buff_.pack_fd.data) ==
      HAL_OK)
  {
    if (rx_buff_.header.FDFormat == FDCAN_FD_CAN)
    {
      rx_buff_.pack_fd.id = rx_buff_.header.Identifier;
      rx_buff_.pack_fd.type =
          (rx_buff_.header.IdType == FDCAN_EXTENDED_ID) ? Type::EXTENDED : Type::STANDARD;

      if (rx_buff_.header.RxFrameType != FDCAN_DATA_FRAME)
      {
        if (rx_buff_.pack_fd.type == Type::STANDARD)
        {
          rx_buff_.pack_fd.type = Type::REMOTE_STANDARD;
        }
        else
        {
          rx_buff_.pack_fd.type = Type::REMOTE_EXTENDED;
        }
      }

      rx_buff_.pack_fd.len = rx_buff_.header.DataLength;

      for (uint32_t i = 0; i < 16; i++)
      {
        if (rx_buff_.pack_fd.len == FDCAN_PACK_LEN_MAP[i])
        {
          rx_buff_.pack_fd.len = FDCAN_PACK_LEN_TO_INT_MAP[i];
          break;
        }
      }

      OnMessage(rx_buff_.pack_fd, true);
    }
    else
    {
      rx_buff_.pack.id = rx_buff_.header.Identifier;
      rx_buff_.pack.type =
          (rx_buff_.header.IdType == FDCAN_EXTENDED_ID) ? Type::EXTENDED : Type::STANDARD;

      if (rx_buff_.header.RxFrameType != FDCAN_DATA_FRAME)
      {
        if (rx_buff_.pack.type == Type::STANDARD)
        {
          rx_buff_.pack.type = Type::REMOTE_STANDARD;
        }
        else
        {
          rx_buff_.pack.type = Type::REMOTE_EXTENDED;
        }
      }
      else
      {
        memcpy(rx_buff_.pack.data, rx_buff_.pack_fd.data, 8);
      }

      OnMessage(rx_buff_.pack, true);
    }
  }
}

void STM32CANFD::ProcessTxInterrupt()
{
  if (tx_pool_fd_.Get(tx_buff_.pack_fd) == ErrorCode::OK)
  {
    tx_buff_.header.Identifier = tx_buff_.pack_fd.id;
    switch (tx_buff_.pack_fd.type)
    {
      case Type::STANDARD:
        tx_buff_.header.IdType = FDCAN_STANDARD_ID;
        tx_buff_.header.TxFrameType = FDCAN_DATA_FRAME;
        break;

      case Type::EXTENDED:
        tx_buff_.header.IdType = FDCAN_EXTENDED_ID;
        tx_buff_.header.TxFrameType = FDCAN_DATA_FRAME;
        break;

      case Type::REMOTE_STANDARD:
        tx_buff_.header.IdType = FDCAN_STANDARD_ID;
        tx_buff_.header.TxFrameType = FDCAN_REMOTE_FRAME;
        break;

      case Type::REMOTE_EXTENDED:
        tx_buff_.header.IdType = FDCAN_EXTENDED_ID;
        tx_buff_.header.TxFrameType = FDCAN_REMOTE_FRAME;
        break;
      default:
        ASSERT(false);
        return;
    }
    tx_buff_.header.DataLength = tx_buff_.pack_fd.len;
    tx_buff_.header.ErrorStateIndicator = FDCAN_ESI_PASSIVE;
    tx_buff_.header.BitRateSwitch = FDCAN_BRS_ON;
    tx_buff_.header.FDFormat = FDCAN_FD_CAN;
    tx_buff_.header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    tx_buff_.header.MessageMarker = 0x00;

    HAL_FDCAN_AddMessageToTxFifoQ(hcan_, &tx_buff_.header, tx_buff_.pack_fd.data);
    bus_busy_.store(UINT32_MAX, std::memory_order_release);
  }
  else if (tx_pool_.Get(tx_buff_.pack) == ErrorCode::OK)
  {
    tx_buff_.header.Identifier = tx_buff_.pack.id;
    switch (tx_buff_.pack.type)
    {
      case Type::STANDARD:
        tx_buff_.header.IdType = FDCAN_STANDARD_ID;
        tx_buff_.header.TxFrameType = FDCAN_DATA_FRAME;
        break;

      case Type::EXTENDED:
        tx_buff_.header.IdType = FDCAN_EXTENDED_ID;
        tx_buff_.header.TxFrameType = FDCAN_DATA_FRAME;
        break;

      case Type::REMOTE_STANDARD:
        tx_buff_.header.IdType = FDCAN_STANDARD_ID;
        tx_buff_.header.TxFrameType = FDCAN_REMOTE_FRAME;
        break;

      case Type::REMOTE_EXTENDED:
        tx_buff_.header.IdType = FDCAN_EXTENDED_ID;
        tx_buff_.header.TxFrameType = FDCAN_REMOTE_FRAME;
        break;
      default:
        ASSERT(false);
        return;
    }
    tx_buff_.header.DataLength = 8;
    tx_buff_.header.FDFormat = FDCAN_CLASSIC_CAN;
    tx_buff_.header.TxFrameType = FDCAN_DATA_FRAME;
    tx_buff_.header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    tx_buff_.header.MessageMarker = 0x00;
    tx_buff_.header.ErrorStateIndicator = FDCAN_ESI_PASSIVE;
    tx_buff_.header.BitRateSwitch = FDCAN_BRS_OFF;

    HAL_FDCAN_AddMessageToTxFifoQ(hcan_, &tx_buff_.header, tx_buff_.pack.data);
    bus_busy_.store(UINT32_MAX, std::memory_order_release);
  }

  if (GetTxFifoTotalElements(hcan_) - HAL_FDCAN_GetTxFifoFreeLevel(hcan_) == 0)
  {
    bus_busy_.store(0, std::memory_order_release);
  }
}

extern "C" void HAL_FDCAN_ErrorCallback(FDCAN_HandleTypeDef* hcan)
{
  hcan->ErrorCode = HAL_FDCAN_ERROR_NONE;
  auto can = STM32CANFD::map[STM32_FDCAN_GetID(hcan->Instance)];
  if (can)
  {
    can->ProcessTxInterrupt();
  }
}

extern "C" void HAL_FDCAN_ErrorStatusCallback(FDCAN_HandleTypeDef* hfdcan,
                                              uint32_t ErrorStatusITs)
{
  if ((ErrorStatusITs & FDCAN_IT_BUS_OFF) != RESET)
  {
    FDCAN_ProtocolStatusTypeDef protocol_status = {};
    HAL_FDCAN_GetProtocolStatus(hfdcan, &protocol_status);
    if (protocol_status.BusOff)
    {
      CLEAR_BIT(hfdcan->Instance->CCCR, FDCAN_CCCR_INIT);
    }
  }

  auto can = STM32CANFD::map[STM32_FDCAN_GetID(hfdcan->Instance)];
  if (can)
  {
    can->ProcessTxInterrupt();
  }
}

extern "C" void HAL_FDCAN_TxBufferCompleteCallback(FDCAN_HandleTypeDef* hcan,
                                                   uint32_t BufferIndexes)
{
  UNUSED(BufferIndexes);
  auto can = STM32CANFD::map[STM32_FDCAN_GetID(hcan->Instance)];
  if (can)
  {
    can->ProcessTxInterrupt();
  }
}

extern "C" void HAL_FDCAN_TxFifoEmptyCallback(FDCAN_HandleTypeDef* hcan)
{
  auto can = STM32CANFD::map[STM32_FDCAN_GetID(hcan->Instance)];
  if (can)
  {
    can->ProcessTxInterrupt();
  }
}

extern "C" void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef* hcan, uint32_t RxFifo0ITs)
{
  UNUSED(RxFifo0ITs);
  auto can = STM32CANFD::map[STM32_FDCAN_GetID(hcan->Instance)];
  if (can)
  {
    can->ProcessRxInterrupt(FDCAN_RX_FIFO0);
  }
}

extern "C" void HAL_FDCAN_RxFifo1Callback(FDCAN_HandleTypeDef* hcan, uint32_t RxFifo1ITs)
{
  UNUSED(RxFifo1ITs);
  auto can = STM32CANFD::map[STM32_FDCAN_GetID(hcan->Instance)];
  if (can)
  {
    can->ProcessRxInterrupt(FDCAN_RX_FIFO1);
  }
}

#endif
