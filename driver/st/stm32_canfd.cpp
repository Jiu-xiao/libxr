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

static inline uint32_t BytesToDlc(uint32_t n)
{
  if (n <= 8U)
  {
    return n;
  }  // FDCAN_DLC_BYTES_0..8 == 0..8
  if (n <= 12U)
  {
    return FDCAN_DLC_BYTES_12;
  }
  if (n <= 16U)
  {
    return FDCAN_DLC_BYTES_16;
  }
  if (n <= 20U)
  {
    return FDCAN_DLC_BYTES_20;
  }
  if (n <= 24U)
  {
    return FDCAN_DLC_BYTES_24;
  }
  if (n <= 32U)
  {
    return FDCAN_DLC_BYTES_32;
  }
  if (n <= 48U)
  {
    return FDCAN_DLC_BYTES_48;
  }
  return FDCAN_DLC_BYTES_64;  // n >= 49 → 64
}

static inline uint32_t DlcToBytes(uint32_t dlc)
{
  if (dlc <= FDCAN_DLC_BYTES_8)
  {  // 0..8 → 0..8
    return dlc;
  }
  else if (dlc == FDCAN_DLC_BYTES_12)
  {
    return 12U;
  }
  else if (dlc == FDCAN_DLC_BYTES_16)
  {
    return 16U;
  }
  else if (dlc == FDCAN_DLC_BYTES_20)
  {
    return 20U;
  }
  else if (dlc == FDCAN_DLC_BYTES_24)
  {
    return 24U;
  }
  else if (dlc == FDCAN_DLC_BYTES_32)
  {
    return 32U;
  }
  else if (dlc == FDCAN_DLC_BYTES_48)
  {
    return 48U;
  }
  else
  {  // FDCAN_DLC_BYTES_64 或其它非法值
    return 64U;
  }
}

STM32CANFD::STM32CANFD(FDCAN_HandleTypeDef* hcan, uint32_t queue_size)
    : FDCAN(),
      hcan_(hcan),
      id_(STM32_FDCAN_GetID(hcan->Instance)),
      tx_pool_(queue_size),
      tx_pool_fd_(queue_size)
{
  CheckMessageRAMOffset(hcan);
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

    if (HardwareTxQueueEmptySize() != 0 && tx_pool_.RecycleSlot(slot) == ErrorCode::OK)
    {
      continue;
    }
    else
    {
      return ErrorCode::OK;
    }
  }
}

ErrorCode STM32CANFD::SetConfig(const CAN::Configuration& cfg)
{
  // 兼容接口：只用仲裁相位参数，FD 数据相位保持不动
  FDCAN::Configuration fd_cfg{};
  fd_cfg.bitrate = cfg.bitrate;
  fd_cfg.sample_point = cfg.sample_point;
  fd_cfg.bit_timing = cfg.bit_timing;
  fd_cfg.mode = cfg.mode;
  // data_timing / fd_mode 全部置 0 → “保持原值”
  return SetConfig(fd_cfg);
}

ErrorCode STM32CANFD::SetConfig(const FDCAN::Configuration& cfg)
{
  if (hcan_ == nullptr || hcan_->Instance == nullptr)
  {
    ASSERT(false);
    return ErrorCode::ARG_ERR;
  }

  FDCAN_GlobalTypeDef* can = hcan_->Instance;

  // 先关通知：只关本驱动用到的这些
  uint32_t it_mask = 0u;

#ifdef FDCAN_IT_RX_FIFO0_NEW_MESSAGE
  it_mask |= FDCAN_IT_RX_FIFO0_NEW_MESSAGE;
#endif
#ifdef FDCAN_IT_RX_FIFO1_NEW_MESSAGE
  it_mask |= FDCAN_IT_RX_FIFO1_NEW_MESSAGE;
#endif
#ifdef FDCAN_IT_TX_FIFO_EMPTY
  it_mask |= FDCAN_IT_TX_FIFO_EMPTY;
#endif

  if (it_mask != 0u)
  {
    HAL_FDCAN_DeactivateNotification(hcan_, it_mask);
  }

  // 停止 FDCAN，进入 INIT / CCE 配置状态
  if (HAL_FDCAN_Stop(hcan_) != HAL_OK)
  {
    return ErrorCode::FAILED;
  }

#ifdef FDCAN_CCCR_INIT
  SET_BIT(can->CCCR, FDCAN_CCCR_INIT);
#endif
#ifdef FDCAN_CCCR_CCE
  SET_BIT(can->CCCR, FDCAN_CCCR_CCE);
#endif

  // ===== 模式相关：one-shot / loopback / listen-only =====
#ifdef FDCAN_CCCR_DAR
  if (cfg.mode.one_shot)
  {
    // 禁用自动重发
    SET_BIT(can->CCCR, FDCAN_CCCR_DAR);
  }
  else
  {
    CLEAR_BIT(can->CCCR, FDCAN_CCCR_DAR);
  }
#endif

  // triple_sampling 对 FDCAN 没意义，这里直接忽略
  (void)cfg.mode.triple_sampling;

#ifdef FDCAN_CCCR_TEST
#ifdef FDCAN_TEST_LBCK
  if (cfg.mode.loopback)
  {
    // 内部回环
    SET_BIT(can->CCCR, FDCAN_CCCR_TEST);
    SET_BIT(can->TEST, FDCAN_TEST_LBCK);
  }
  else
  {
    CLEAR_BIT(can->TEST, FDCAN_TEST_LBCK);
    CLEAR_BIT(can->CCCR, FDCAN_CCCR_TEST);
  }
#endif
#endif

#ifdef FDCAN_CCCR_MON
  if (cfg.mode.listen_only)
  {
    // 总线监控（只听）
    SET_BIT(can->CCCR, FDCAN_CCCR_MON);
  }
  else
  {
    CLEAR_BIT(can->CCCR, FDCAN_CCCR_MON);
  }
#endif

  // ===== 仲裁相位 bit timing 范围校验 + 写 NBTP =====
  const auto& bt = cfg.bit_timing;

  // 用掩码算出字段最大值，避免硬编码
#ifdef FDCAN_NBTP_NBRP_Msk
  constexpr uint32_t NBRP_FIELD_MAX = (FDCAN_NBTP_NBRP_Msk >> FDCAN_NBTP_NBRP_Pos);
  constexpr uint32_t NTSEG1_FIELD_MAX = (FDCAN_NBTP_NTSEG1_Msk >> FDCAN_NBTP_NTSEG1_Pos);
  constexpr uint32_t NTSEG2_FIELD_MAX = (FDCAN_NBTP_NTSEG2_Msk >> FDCAN_NBTP_NTSEG2_Pos);
  constexpr uint32_t NSJW_FIELD_MAX = (FDCAN_NBTP_NSJW_Msk >> FDCAN_NBTP_NSJW_Pos);

  constexpr uint32_t NBRP_MAX = NBRP_FIELD_MAX + 1u;
  constexpr uint32_t NTSEG1_MAX = NTSEG1_FIELD_MAX + 1u;
  constexpr uint32_t NTSEG2_MAX = NTSEG2_FIELD_MAX + 1u;
  constexpr uint32_t NSJW_MAX = NSJW_FIELD_MAX + 1u;

  // 0 = 保持原值，非 0 做范围检查
  if (bt.brp != 0u)
  {
    if (bt.brp < 1u || bt.brp > NBRP_MAX)
    {
      ASSERT(false);
      return ErrorCode::ARG_ERR;
    }
  }

  uint32_t tseg1 = bt.prop_seg + bt.phase_seg1;
  if (bt.prop_seg != 0u || bt.phase_seg1 != 0u)
  {
    if (tseg1 < 1u || tseg1 > NTSEG1_MAX)
    {
      ASSERT(false);
      return ErrorCode::ARG_ERR;
    }
  }

  if (bt.phase_seg2 != 0u)
  {
    if (bt.phase_seg2 < 1u || bt.phase_seg2 > NTSEG2_MAX)
    {
      ASSERT(false);
      return ErrorCode::ARG_ERR;
    }
  }

  if (bt.sjw != 0u)
  {
    if (bt.sjw < 1u || bt.sjw > NSJW_MAX)
    {
      ASSERT(false);
      return ErrorCode::ARG_ERR;
    }
    if (bt.phase_seg2 != 0u && bt.sjw > bt.phase_seg2)
    {
      ASSERT(false);
      return ErrorCode::ARG_ERR;
    }
  }

  uint32_t nbtp_old = can->NBTP;
  uint32_t nbtp_new = nbtp_old;
  uint32_t nbtp_mask = 0u;

  // NBRP
  if (bt.brp != 0u)
  {
    uint32_t nbrp = (bt.brp - 1u) & NBRP_FIELD_MAX;
    uint32_t mask = FDCAN_NBTP_NBRP_Msk;
    nbtp_mask |= mask;
    nbtp_new &= ~mask;
    nbtp_new |= (nbrp << FDCAN_NBTP_NBRP_Pos);
  }

  // NTSEG1 = PROP_SEG + PHASE_SEG1
  if (bt.prop_seg != 0u || bt.phase_seg1 != 0u)
  {
    uint32_t ntseg1 = (tseg1 - 1u) & NTSEG1_FIELD_MAX;
    uint32_t mask = FDCAN_NBTP_NTSEG1_Msk;
    nbtp_mask |= mask;
    nbtp_new &= ~mask;
    nbtp_new |= (ntseg1 << FDCAN_NBTP_NTSEG1_Pos);
  }

  // NTSEG2 = PHASE_SEG2
  if (bt.phase_seg2 != 0u)
  {
    uint32_t ntseg2 = (bt.phase_seg2 - 1u) & NTSEG2_FIELD_MAX;
    uint32_t mask = FDCAN_NBTP_NTSEG2_Msk;
    nbtp_mask |= mask;
    nbtp_new &= ~mask;
    nbtp_new |= (ntseg2 << FDCAN_NBTP_NTSEG2_Pos);
  }

  // NSJW
  if (bt.sjw != 0u)
  {
    uint32_t nsjw = (bt.sjw - 1u) & NSJW_FIELD_MAX;
    uint32_t mask = FDCAN_NBTP_NSJW_Msk;
    nbtp_mask |= mask;
    nbtp_new &= ~mask;
    nbtp_new |= (nsjw << FDCAN_NBTP_NSJW_Pos);
  }

  if (nbtp_mask != 0u)
  {
    nbtp_old &= ~nbtp_mask;
    nbtp_old |= (nbtp_new & nbtp_mask);
    can->NBTP = nbtp_old;
  }
#endif  // FDCAN_NBTP_NBRP_Msk

  // ===== 数据相位 bit timing（CAN FD 才用），写 DBTP =====
  const auto& dbt = cfg.data_timing;

#ifdef FDCAN_DBTP_DBRP_Msk
  constexpr uint32_t DBRP_FIELD_MAX = (FDCAN_DBTP_DBRP_Msk >> FDCAN_DBTP_DBRP_Pos);
  constexpr uint32_t DTSEG1_FIELD_MAX = (FDCAN_DBTP_DTSEG1_Msk >> FDCAN_DBTP_DTSEG1_Pos);
  constexpr uint32_t DTSEG2_FIELD_MAX = (FDCAN_DBTP_DTSEG2_Msk >> FDCAN_DBTP_DTSEG2_Pos);
  constexpr uint32_t DSJW_FIELD_MAX = (FDCAN_DBTP_DSJW_Msk >> FDCAN_DBTP_DSJW_Pos);

  constexpr uint32_t DBRP_MAX = DBRP_FIELD_MAX + 1u;
  constexpr uint32_t DTSEG1_MAX = DTSEG1_FIELD_MAX + 1u;
  constexpr uint32_t DTSEG2_MAX = DTSEG2_FIELD_MAX + 1u;
  constexpr uint32_t DSJW_MAX = DSJW_FIELD_MAX + 1u;

  if (dbt.brp != 0u)
  {
    if (dbt.brp < 1u || dbt.brp > DBRP_MAX)
    {
      ASSERT(false);
      return ErrorCode::ARG_ERR;
    }
  }

  uint32_t dtseg1 = dbt.prop_seg + dbt.phase_seg1;
  if (dbt.prop_seg != 0u || dbt.phase_seg1 != 0u)
  {
    if (dtseg1 < 1u || dtseg1 > DTSEG1_MAX)
    {
      ASSERT(false);
      return ErrorCode::ARG_ERR;
    }
  }

  if (dbt.phase_seg2 != 0u)
  {
    if (dbt.phase_seg2 < 1u || dbt.phase_seg2 > DTSEG2_MAX)
    {
      ASSERT(false);
      return ErrorCode::ARG_ERR;
    }
  }

  if (dbt.sjw != 0u)
  {
    if (dbt.sjw < 1u || dbt.sjw > DSJW_MAX)
    {
      ASSERT(false);
      return ErrorCode::ARG_ERR;
    }
    if (dbt.phase_seg2 != 0u && dbt.sjw > dbt.phase_seg2)
    {
      ASSERT(false);
      return ErrorCode::ARG_ERR;
    }
  }

  uint32_t dbtp_old = can->DBTP;
  uint32_t dbtp_new = dbtp_old;
  uint32_t dbtp_mask = 0u;

  if (dbt.brp != 0u)
  {
    uint32_t dbrp = (dbt.brp - 1u) & DBRP_FIELD_MAX;
    uint32_t mask = FDCAN_DBTP_DBRP_Msk;
    dbtp_mask |= mask;
    dbtp_new &= ~mask;
    dbtp_new |= (dbrp << FDCAN_DBTP_DBRP_Pos);
  }

  if (dbt.prop_seg != 0u || dbt.phase_seg1 != 0u)
  {
    uint32_t dt1 = (dtseg1 - 1u) & DTSEG1_FIELD_MAX;
    uint32_t mask = FDCAN_DBTP_DTSEG1_Msk;
    dbtp_mask |= mask;
    dbtp_new &= ~mask;
    dbtp_new |= (dt1 << FDCAN_DBTP_DTSEG1_Pos);
  }

  if (dbt.phase_seg2 != 0u)
  {
    uint32_t dt2 = (dbt.phase_seg2 - 1u) & DTSEG2_FIELD_MAX;
    uint32_t mask = FDCAN_DBTP_DTSEG2_Msk;
    dbtp_mask |= mask;
    dbtp_new &= ~mask;
    dbtp_new |= (dt2 << FDCAN_DBTP_DTSEG2_Pos);
  }

  if (dbt.sjw != 0u)
  {
    uint32_t dsjw = (dbt.sjw - 1u) & DSJW_FIELD_MAX;
    uint32_t mask = FDCAN_DBTP_DSJW_Msk;
    dbtp_mask |= mask;
    dbtp_new &= ~mask;
    dbtp_new |= (dsjw << FDCAN_DBTP_DSJW_Pos);
  }

  if (dbtp_mask != 0u)
  {
    dbtp_old &= ~dbtp_mask;
    dbtp_old |= (dbtp_new & dbtp_mask);
    can->DBTP = dbtp_old;
  }
#else
  (void)dbt;
#endif  // FDCAN_DBTP_DBRP_Msk

  // 数据相位 FDMode：这里不动寄存器，只保留在上层语义中使用
  (void)cfg.fd_mode;

  // 重新启动 FDCAN
  if (HAL_FDCAN_Start(hcan_) != HAL_OK)
  {
    return ErrorCode::FAILED;
  }

  // 恢复通知：简单起见，两个 FIFO 都打开 + TX FIFO empty
#ifdef FDCAN_IT_RX_FIFO0_NEW_MESSAGE
  HAL_FDCAN_ActivateNotification(hcan_, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0);
#endif
#ifdef FDCAN_IT_RX_FIFO1_NEW_MESSAGE
  HAL_FDCAN_ActivateNotification(hcan_, FDCAN_IT_RX_FIFO1_NEW_MESSAGE, 0);
#endif
#ifdef FDCAN_IT_TX_FIFO_EMPTY
  HAL_FDCAN_ActivateNotification(hcan_, FDCAN_IT_TX_FIFO_EMPTY, 0);
#endif

  return ErrorCode::OK;
}

uint32_t STM32CANFD::GetClockFreq() const
{
  // 所有带 FDCAN 的 STM32 都通过 RCCEx 提供核时钟查询
#if defined(RCC_PERIPHCLK_FDCAN)
  return HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_FDCAN);
#elif defined(RCC_PERIPHCLK_FDCAN1)
  return HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_FDCAN1);
#else
  // 理论上不会走到这里，有就说明 HAL/RCC 宏不一致
  ASSERT(false);
  return 0u;
#endif
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

  header.DataLength = BytesToDlc(pack.len);

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

    if (HardwareTxQueueEmptySize() != 0 && tx_pool_fd_.RecycleSlot(slot) == ErrorCode::OK)
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

      rx_buff_.pack_fd.len = DlcToBytes(rx_buff_.header.DataLength);

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
    tx_buff_.header.DataLength = BytesToDlc(tx_buff_.pack_fd.len);
    tx_buff_.header.ErrorStateIndicator = FDCAN_ESI_PASSIVE;
    tx_buff_.header.BitRateSwitch = FDCAN_BRS_ON;
    tx_buff_.header.FDFormat = FDCAN_FD_CAN;
    tx_buff_.header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    tx_buff_.header.MessageMarker = 0x00;

    HAL_FDCAN_AddMessageToTxFifoQ(hcan_, &tx_buff_.header, tx_buff_.pack_fd.data);
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
    tx_buff_.header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    tx_buff_.header.MessageMarker = 0x00;
    tx_buff_.header.ErrorStateIndicator = FDCAN_ESI_PASSIVE;
    tx_buff_.header.BitRateSwitch = FDCAN_BRS_OFF;

    HAL_FDCAN_AddMessageToTxFifoQ(hcan_, &tx_buff_.header, tx_buff_.pack.data);
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
