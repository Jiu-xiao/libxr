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

ErrorCode STM32CAN::SetConfig(const CAN::Configuration& cfg)
{
  if (hcan_ == nullptr || hcan_->Instance == nullptr)
  {
    ASSERT(false);
    return ErrorCode::ARG_ERR;
  }

  CAN_TypeDef* can = hcan_->Instance;

  // 先关掉与 Init 对应的中断
  uint32_t it_mask = 0u;

#ifdef CAN_IT_RX_FIFO0_MSG_PENDING
  if (fifo_ == CAN_RX_FIFO0)
  {
    it_mask |= CAN_IT_RX_FIFO0_MSG_PENDING;
  }
#endif

#ifdef CAN_IT_RX_FIFO1_MSG_PENDING
  if (fifo_ == CAN_RX_FIFO1)
  {
    it_mask |= CAN_IT_RX_FIFO1_MSG_PENDING;
  }
#endif

#ifdef CAN_IT_ERROR
  it_mask |= CAN_IT_ERROR;
#endif

#ifdef CAN_IT_TX_MAILBOX_EMPTY
  it_mask |= CAN_IT_TX_MAILBOX_EMPTY;
#endif

  if (it_mask != 0u)
  {
    HAL_CAN_DeactivateNotification(hcan_, it_mask);
  }

  // 停止 CAN，进入配置状态
  if (HAL_CAN_Stop(hcan_) != HAL_OK)
  {
    return ErrorCode::FAILED;
  }

  // 一次发送模式（不自动重发）→ NART
#ifdef CAN_MCR_NART
  if (cfg.mode.one_shot)
  {
    SET_BIT(can->MCR, CAN_MCR_NART);
  }
  else
  {
    CLEAR_BIT(can->MCR, CAN_MCR_NART);
  }
#endif

  const auto& bt = cfg.bit_timing;

  // ====== 范围校验（0 表示“保持原值”，跳过检查） ======
  // 最大值从掩码推出来，避免硬编码
  constexpr uint32_t BRP_FIELD_MAX =
      (CAN_BTR_BRP_Msk >> CAN_BTR_BRP_Pos);  // 存的是 brp-1
  constexpr uint32_t TS1_FIELD_MAX =
      (CAN_BTR_TS1_Msk >> CAN_BTR_TS1_Pos);  // 存的是 ts1-1
  constexpr uint32_t TS2_FIELD_MAX =
      (CAN_BTR_TS2_Msk >> CAN_BTR_TS2_Pos);  // 存的是 ts2-1
  constexpr uint32_t SJW_FIELD_MAX =
      (CAN_BTR_SJW_Msk >> CAN_BTR_SJW_Pos);  // 存的是 sjw-1

  // 实际可配置的量（寄存器值 + 1）
  constexpr uint32_t BRP_MAX = BRP_FIELD_MAX + 1u;  // 1..1024
  constexpr uint32_t TS1_MAX = TS1_FIELD_MAX + 1u;  // 1..16
  constexpr uint32_t TS2_MAX = TS2_FIELD_MAX + 1u;  // 1..8
  constexpr uint32_t SJW_MAX = SJW_FIELD_MAX + 1u;  // 1..4

  // brp 范围检查
  if (bt.brp != 0u)
  {
    if (bt.brp < 1u || bt.brp > BRP_MAX)
    {
      ASSERT(false);
      return ErrorCode::ARG_ERR;
    }
  }

  // tseg1 = prop_seg + phase_seg1
  uint32_t tseg1 = bt.prop_seg + bt.phase_seg1;
  if (bt.prop_seg != 0u || bt.phase_seg1 != 0u)
  {
    if (tseg1 < 1u || tseg1 > TS1_MAX)
    {
      ASSERT(false);
      return ErrorCode::ARG_ERR;
    }
  }

  if (bt.phase_seg2 != 0u)
  {
    if (bt.phase_seg2 < 1u || bt.phase_seg2 > TS2_MAX)
    {
      ASSERT(false);
      return ErrorCode::ARG_ERR;
    }
  }

  if (bt.sjw != 0u)
  {
    if (bt.sjw < 1u || bt.sjw > SJW_MAX)
    {
      ASSERT(false);
      return ErrorCode::ARG_ERR;
    }

    // 规范上 SJW ≤ TSEG2（只在二者都要更新时检查）
    if (bt.phase_seg2 != 0u && bt.sjw > bt.phase_seg2)
    {
      ASSERT(false);
      return ErrorCode::ARG_ERR;
    }
  }

  uint32_t btr_old = can->BTR;
  uint32_t btr_new = btr_old;
  uint32_t btr_mask = 0u;

  // BRP：bt.brp == 0 → 保持原值
  if (bt.brp != 0u)
  {
    uint32_t brp = (bt.brp - 1u) & BRP_FIELD_MAX;
    uint32_t mask = CAN_BTR_BRP_Msk;
    btr_mask |= mask;
    btr_new &= ~mask;
    btr_new |= (brp << CAN_BTR_BRP_Pos);
  }

  // TSEG1 = PROP_SEG + PHASE_SEG1，两者都为 0 → 保持原值
  if (bt.prop_seg != 0u || bt.phase_seg1 != 0u)
  {
    uint32_t ts1 = (tseg1 - 1u) & TS1_FIELD_MAX;
    uint32_t mask = CAN_BTR_TS1_Msk;
    btr_mask |= mask;
    btr_new &= ~mask;
    btr_new |= (ts1 << CAN_BTR_TS1_Pos);
  }

  // TSEG2 = PHASE_SEG2，phase_seg2 == 0 → 保持原值
  if (bt.phase_seg2 != 0u)
  {
    uint32_t ts2 = (bt.phase_seg2 - 1u) & TS2_FIELD_MAX;
    uint32_t mask = CAN_BTR_TS2_Msk;
    btr_mask |= mask;
    btr_new &= ~mask;
    btr_new |= (ts2 << CAN_BTR_TS2_Pos);
  }

  // SJW，sjw == 0 → 保持原值
  if (bt.sjw != 0u)
  {
    uint32_t sjw = (bt.sjw - 1u) & SJW_FIELD_MAX;
    uint32_t mask = CAN_BTR_SJW_Msk;
    btr_mask |= mask;
    btr_new &= ~mask;
    btr_new |= (sjw << CAN_BTR_SJW_Pos);
  }

  // 三采样：只有 HAL 定义了 CAN_BTR_SAM 才动；否则忽略 triple_sampling
#ifdef CAN_BTR_SAM
  {
    uint32_t mask = CAN_BTR_SAM;
    btr_mask |= mask;
    btr_new &= ~mask;
    if (cfg.mode.triple_sampling)
    {
      btr_new |= mask;
    }
  }
#else
  (void)cfg.mode.triple_sampling;
#endif

  // Loopback：bool 两态，强制覆盖
#ifdef CAN_BTR_LBKM
  {
    uint32_t mask = CAN_BTR_LBKM;
    btr_mask |= mask;
    btr_new &= ~mask;
    if (cfg.mode.loopback)
    {
      btr_new |= mask;
    }
  }
#endif

  // Listen-only：bool 两态，强制覆盖
#ifdef CAN_BTR_SILM
  {
    uint32_t mask = CAN_BTR_SILM;
    btr_mask |= mask;
    btr_new &= ~mask;
    if (cfg.mode.listen_only)
    {
      btr_new |= mask;
    }
  }
#endif

  // 只改被 btr_mask 覆盖到的位，其余保持原值
  if (btr_mask != 0u)
  {
    btr_old &= ~btr_mask;
    btr_old |= (btr_new & btr_mask);
    can->BTR = btr_old;
  }

  // 重新启动 CAN
  if (HAL_CAN_Start(hcan_) != HAL_OK)
  {
    return ErrorCode::FAILED;
  }

  // 按 Init 的方式恢复中断
  uint32_t it_rx = 0u;

#ifdef CAN_IT_RX_FIFO0_MSG_PENDING
  if (fifo_ == CAN_RX_FIFO0)
  {
    it_rx = CAN_IT_RX_FIFO0_MSG_PENDING;
  }
#endif

#ifdef CAN_IT_RX_FIFO1_MSG_PENDING
  if (fifo_ == CAN_RX_FIFO1)
  {
    it_rx = CAN_IT_RX_FIFO1_MSG_PENDING;
  }
#endif

  if (it_rx != 0u)
  {
    HAL_CAN_ActivateNotification(hcan_, it_rx);
  }

#ifdef CAN_IT_ERROR
  HAL_CAN_ActivateNotification(hcan_, CAN_IT_ERROR);
#endif

#ifdef CAN_IT_TX_MAILBOX_EMPTY
  HAL_CAN_ActivateNotification(hcan_, CAN_IT_TX_MAILBOX_EMPTY);
#endif

  return ErrorCode::OK;
}

uint32_t STM32CAN::GetClockFreq() const
{
  // 经典 bxCAN 始终挂在 APB1 上
  return HAL_RCC_GetPCLK1Freq();
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
