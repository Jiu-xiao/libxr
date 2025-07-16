#include "stm32_can.hpp"

#ifdef HAL_CAN_MODULE_ENABLED

using namespace LibXR;

STM32CAN* STM32CAN::map[STM32_CAN_NUMBER] = {nullptr};

/**
 * @brief 获取 CAN ID Get CAN ID
 *
 * @param addr CAN外设地址 CAN device address
 * @return stm32_can_id_t
 */
stm32_can_id_t STM32_CAN_GetID(CAN_TypeDef* addr)
{
  // NOLINTBEGIN
  if (addr == nullptr)
  {  // NOLINT
    return stm32_can_id_t::STM32_CAN_ID_ERROR;
  }
#ifdef CAN1
  else if (addr == CAN1)
  {  // NOLINT
    return stm32_can_id_t::STM32_CAN1;
  }
#endif
#ifdef CAN2
  else if (addr == CAN2)
  {  // NOLINT
    return stm32_can_id_t::STM32_CAN2;
  }
#endif
#ifdef CAN3
  else if (addr == CAN3)
  {  // NOLINT
    return stm32_can_id_t::STM32_CAN3;
  }
#endif
  else
  {
    return stm32_can_id_t::STM32_CAN_ID_ERROR;
  }
  // NOLINTEND
}

STM32CAN::STM32CAN(CAN_HandleTypeDef* hcan, uint32_t pool_size)
    : CAN(), hcan_(hcan), id_(STM32_CAN_GetID(hcan->Instance)), tx_pool_(pool_size)
{
  map[id_] = this;
  Init();
}

ErrorCode STM32CAN::Init(void)
{
  CAN_FilterTypeDef can_filter = {};

  can_filter.FilterIdHigh = 0;
  can_filter.FilterIdLow = 0;
  can_filter.FilterMode = CAN_FILTERMODE_IDMASK;
  can_filter.FilterScale = CAN_FILTERSCALE_32BIT;
  can_filter.FilterMaskIdHigh = 0;
  can_filter.FilterMaskIdLow = 0;
  can_filter.FilterFIFOAssignment = fifo_;
  can_filter.FilterActivation = ENABLE;
#ifdef CAN3
  if (id_ == STM32_CAN1)
  {
    can_filter.FilterBank = 0;
    can_filter.SlaveStartFilterBank = 14;
    fifo_ = CAN_RX_FIFO0;
  }
  else if (id_ == STM32_CAN2)
  {
    can_filter.FilterBank = 14;
    can_filter.SlaveStartFilterBank = 14;
    fifo_ = CAN_RX_FIFO0;
  }
  else if (id_ == STM32_CAN3)
  {
    can_filter.FilterBank = 3;
    fifo_ = CAN_RX_FIFO1;
  }
#else
#ifdef CAN2
  if (id_ == STM32_CAN1)
  {
    can_filter.FilterBank = 0;
    can_filter.SlaveStartFilterBank = 14;
    fifo_ = CAN_RX_FIFO0;
  }
  else if (id_ == STM32_CAN2)
  {
    can_filter.FilterBank = 14;
    can_filter.SlaveStartFilterBank = 14;
    fifo_ = CAN_RX_FIFO1;
  }
#else
  if (id_ == STM32_CAN1)
  {
    can_filter.FilterBank = 0;
    can_filter.SlaveStartFilterBank = 14;
    fifo_ = CAN_RX_FIFO0;
  }
#endif
#endif
  else
  {
    ASSERT(false);
    return ErrorCode::FAILED;
  }
  can_filter.FilterFIFOAssignment = fifo_;

  if (HAL_CAN_ConfigFilter(hcan_, &can_filter) != HAL_OK)
  {
    return ErrorCode::FAILED;
  }

  if (HAL_CAN_Start(hcan_) != HAL_OK)
  {
    return ErrorCode::FAILED;
  }

  if (fifo_ == CAN_RX_FIFO0)
  {
    HAL_CAN_ActivateNotification(hcan_, CAN_IT_RX_FIFO0_MSG_PENDING);
  }
  else
  {
    HAL_CAN_ActivateNotification(hcan_, CAN_IT_RX_FIFO1_MSG_PENDING);
  }

  HAL_CAN_ActivateNotification(hcan_, CAN_IT_ERROR);
  HAL_CAN_ActivateNotification(hcan_, CAN_IT_TX_MAILBOX_EMPTY);

  return ErrorCode::OK;
}

ErrorCode STM32CAN::AddMessage(const ClassicPack& pack)
{
  CAN_TxHeaderTypeDef txHeader;  // NOLINT

  txHeader.DLC = sizeof(pack.data);

  switch (pack.type)
  {
    case Type::STANDARD:
      txHeader.IDE = CAN_ID_STD;
      txHeader.RTR = CAN_RTR_DATA;
      break;
    case Type::EXTENDED:
      txHeader.IDE = CAN_ID_EXT;
      txHeader.RTR = CAN_RTR_DATA;
      break;
    case Type::REMOTE_STANDARD:
      txHeader.IDE = CAN_ID_STD;
      txHeader.RTR = CAN_RTR_REMOTE;
      break;
    case Type::REMOTE_EXTENDED:
      txHeader.IDE = CAN_ID_EXT;
      txHeader.RTR = CAN_RTR_REMOTE;
      break;
    default:
      ASSERT(false);
      return ErrorCode::FAILED;
  }
  txHeader.StdId = (pack.type == Type::EXTENDED) ? 0 : pack.id;
  txHeader.ExtId = (pack.type == Type::EXTENDED) ? pack.id : 0;
  txHeader.TransmitGlobalTime = DISABLE;

  while (true)
  {
    uint32_t slot = 0;

    if (HAL_CAN_AddTxMessage(hcan_, &txHeader, pack.data, &txMailbox) != HAL_OK)
    {
      if (tx_pool_.Put(pack, slot) != ErrorCode::OK)
      {
        return ErrorCode::FULL;
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

void STM32CAN::ProcessRxInterrupt()
{
  while (HAL_CAN_GetRxMessage(hcan_, fifo_, &rx_buff_.header, rx_buff_.pack.data) ==
         HAL_OK)
  {
    if (rx_buff_.header.IDE == CAN_ID_STD)
    {
      if (rx_buff_.header.StdId == 2046)
      {
        __NOP();
      }
      rx_buff_.pack.id = rx_buff_.header.StdId;
      rx_buff_.pack.type = Type::STANDARD;
    }
    else
    {
      rx_buff_.pack.id = rx_buff_.header.ExtId;
      rx_buff_.pack.type = Type::EXTENDED;
    }

    if (rx_buff_.header.RTR == CAN_RTR_REMOTE)
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
    OnMessage(rx_buff_.pack, true);
  }
}

void STM32CAN::ProcessTxInterrupt()
{
  if (tx_pool_.Get(tx_buff_.pack) == ErrorCode::OK)
  {
    tx_buff_.header.DLC = sizeof(tx_buff_.pack.data);
    switch (tx_buff_.pack.type)
    {
      case Type::STANDARD:
        tx_buff_.header.IDE = CAN_ID_STD;
        tx_buff_.header.RTR = CAN_RTR_DATA;
        break;
      case Type::EXTENDED:
        tx_buff_.header.IDE = CAN_ID_EXT;
        tx_buff_.header.RTR = CAN_RTR_DATA;
        break;
      case Type::REMOTE_STANDARD:
        tx_buff_.header.IDE = CAN_ID_STD;
        tx_buff_.header.RTR = CAN_RTR_REMOTE;
        break;
      case Type::REMOTE_EXTENDED:
        tx_buff_.header.IDE = CAN_ID_EXT;
        tx_buff_.header.RTR = CAN_RTR_REMOTE;
        break;
      default:
        ASSERT(false);
        return;
    }
    tx_buff_.header.StdId = (tx_buff_.pack.type == Type::EXTENDED) ? 0 : tx_buff_.pack.id;
    tx_buff_.header.ExtId = (tx_buff_.pack.type == Type::EXTENDED) ? tx_buff_.pack.id : 0;
    tx_buff_.header.TransmitGlobalTime = DISABLE;

    HAL_CAN_AddTxMessage(hcan_, &tx_buff_.header, tx_buff_.pack.data, &txMailbox);

    bus_busy_.store(UINT32_MAX, std::memory_order_release);
  }
  else
  {
    uint32_t tsr = READ_REG(hcan_->Instance->TSR);

    if (((tsr & CAN_TSR_TME0) != 0U) && ((tsr & CAN_TSR_TME1) != 0U) &&
        ((tsr & CAN_TSR_TME2) != 0U))
    {
      bus_busy_.store(0, std::memory_order_release);
    }
    else
    {
      bus_busy_.store(UINT32_MAX, std::memory_order_release);
    }
  }
}

extern "C" void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef* hcan)
{
  STM32CAN* can = STM32CAN::map[STM32_CAN_GetID(hcan->Instance)];
  if (can)
  {
    can->ProcessRxInterrupt();
  }
}

extern "C" void HAL_CAN_RxFifo1MsgPendingCallback(CAN_HandleTypeDef* hcan)
{
  STM32CAN* can = STM32CAN::map[STM32_CAN_GetID(hcan->Instance)];
  if (can)
  {
    can->ProcessRxInterrupt();
  }
}

extern "C" void HAL_CAN_ErrorCallback(CAN_HandleTypeDef* hcan)
{
  HAL_CAN_ResetError(hcan);
}

extern "C" void HAL_CAN_TxMailbox0CompleteCallback(CAN_HandleTypeDef* hcan)
{
  STM32CAN* can = STM32CAN::map[STM32_CAN_GetID(hcan->Instance)];
  if (can)
  {
    can->ProcessTxInterrupt();
  }
}

extern "C" void HAL_CAN_TxMailbox1CompleteCallback(CAN_HandleTypeDef* hcan)
{
  STM32CAN* can = STM32CAN::map[STM32_CAN_GetID(hcan->Instance)];
  if (can)
  {
    can->ProcessTxInterrupt();
  }
}

extern "C" void HAL_CAN_TxMailbox2CompleteCallback(CAN_HandleTypeDef* hcan)
{
  STM32CAN* can = STM32CAN::map[STM32_CAN_GetID(hcan->Instance)];
  if (can)
  {
    can->ProcessTxInterrupt();
  }
}

#endif
