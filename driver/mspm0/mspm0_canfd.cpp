#include "mspm0_canfd.hpp"

#include "mspm0_can.hpp"

#if defined(__MSPM0_HAS_MCAN__)

#include <cstring>

using namespace LibXR;

namespace
{

constexpr uint32_t MSPM0_CANFD_MSP_LINE_MASK = DL_MCAN_MSP_INTERRUPT_LINE1;

constexpr uint32_t MSPM0_CANFD_INTR_MASK =
    DL_MCAN_INTR_SRC_RX_FIFO0_NEW_MSG | DL_MCAN_INTR_SRC_RX_FIFO1_NEW_MSG |
    DL_MCAN_INTR_SRC_TRANS_COMPLETE | DL_MCAN_INTR_SRC_TRANS_CANCEL_FINISH |
    DL_MCAN_INTR_SRC_TX_FIFO_EMPTY | DL_MCAN_INTR_SRC_BUS_OFF_STATUS |
    DL_MCAN_INTR_SRC_PROTOCOL_ERR_ARB | DL_MCAN_INTR_SRC_PROTOCOL_ERR_DATA |
    DL_MCAN_INTR_SRC_MSG_RAM_ACCESS_FAILURE;

constexpr uint32_t MSPM0_CANFD_ERROR_INTR_MASK =
    DL_MCAN_INTR_SRC_BUS_OFF_STATUS | DL_MCAN_INTR_SRC_PROTOCOL_ERR_ARB |
    DL_MCAN_INTR_SRC_PROTOCOL_ERR_DATA | DL_MCAN_INTR_SRC_RES_ADDR_ACCESS |
    DL_MCAN_INTR_SRC_MSG_RAM_ACCESS_FAILURE;

constexpr uint8_t MSPM0_CANFD_DLC_TO_LEN[16] = {0, 1,  2,  3,  4,  5,  6,  7,
                                                8, 12, 16, 20, 24, 32, 48, 64};

constexpr uint32_t mspm0_canfd_dlc_to_len(uint32_t dlc)
{
  return MSPM0_CANFD_DLC_TO_LEN[(dlc < 16U) ? dlc : 15U];
}

constexpr uint32_t mspm0_canfd_update_field(uint32_t reg, uint32_t mask, uint32_t ofs,
                                            uint32_t value)
{
  return (reg & ~mask) | ((value << ofs) & mask);
}

constexpr ErrorCode mspm0_canfd_status_to_error(int32_t status)
{
  switch (status)
  {
    case 0:
      return ErrorCode::OK;
    case -2:
    case -3:
      return ErrorCode::ARG_ERR;
    case -4:
      return ErrorCode::TIMEOUT;
    case -5:
      return ErrorCode::OUT_OF_RANGE;
    case -6:
    case -7:
      return ErrorCode::NOT_SUPPORT;
    case -1:
    default:
      return ErrorCode::FAILED;
  }
}

CAN::ErrorID mspm0_canfd_error_id_from_status(const DL_MCAN_ProtocolStatus& protocol_status,
                                              uint32_t intr_status)
{
  if ((intr_status & (DL_MCAN_INTR_SRC_RES_ADDR_ACCESS |
                      DL_MCAN_INTR_SRC_MSG_RAM_ACCESS_FAILURE)) != 0U)
  {
    return CAN::ErrorID::CAN_ERROR_ID_GENERIC;
  }

  if (protocol_status.busOffStatus != 0U)
  {
    return CAN::ErrorID::CAN_ERROR_ID_BUS_OFF;
  }

  if (protocol_status.errPassive != 0U)
  {
    return CAN::ErrorID::CAN_ERROR_ID_ERROR_PASSIVE;
  }

  if (protocol_status.warningStatus != 0U)
  {
    return CAN::ErrorID::CAN_ERROR_ID_ERROR_WARNING;
  }

  uint32_t lec = protocol_status.lastErrCode & 0x7U;
  if (lec == static_cast<uint32_t>(DL_MCAN_ERR_CODE_NO_ERROR))
  {
    lec = protocol_status.dlec & 0x7U;
  }

  switch (lec)
  {
    case DL_MCAN_ERR_CODE_STUFF_ERROR:
      return CAN::ErrorID::CAN_ERROR_ID_STUFF;

    case DL_MCAN_ERR_CODE_FORM_ERROR:
      return CAN::ErrorID::CAN_ERROR_ID_FORM;

    case DL_MCAN_ERR_CODE_ACK_ERROR:
      return CAN::ErrorID::CAN_ERROR_ID_ACK;

    case DL_MCAN_ERR_CODE_BIT1_ERROR:
      return CAN::ErrorID::CAN_ERROR_ID_BIT1;

    case DL_MCAN_ERR_CODE_BIT0_ERROR:
      return CAN::ErrorID::CAN_ERROR_ID_BIT0;

    case DL_MCAN_ERR_CODE_CRC_ERROR:
      return CAN::ErrorID::CAN_ERROR_ID_CRC;

    case DL_MCAN_ERR_CODE_NO_CHANGE:
    case DL_MCAN_ERR_CODE_NO_ERROR:
    default:
      return CAN::ErrorID::CAN_ERROR_ID_OTHER;
  }
}

uint32_t mspm0_canfd_len_to_dlc(uint32_t len)
{
  if (len <= 8U)
  {
    return len;
  }
  if (len <= 12U)
  {
    return 9U;
  }
  if (len <= 16U)
  {
    return 10U;
  }
  if (len <= 20U)
  {
    return 11U;
  }
  if (len <= 24U)
  {
    return 12U;
  }
  if (len <= 32U)
  {
    return 13U;
  }
  if (len <= 48U)
  {
    return 14U;
  }
  return 15U;
}

void mspm0_canfd_pack_type_to_id(uint32_t& id_field, uint32_t& xtd_field,
                                 uint32_t& rtr_field, uint32_t pack_id,
                                 CAN::Type pack_type)
{
  switch (pack_type)
  {
    case CAN::Type::STANDARD:
      id_field = (pack_id & 0x7FFU) << 18U;
      xtd_field = 0U;
      rtr_field = 0U;
      break;

    case CAN::Type::EXTENDED:
      id_field = pack_id & 0x1FFFFFFFU;
      xtd_field = 1U;
      rtr_field = 0U;
      break;

    case CAN::Type::REMOTE_STANDARD:
      id_field = (pack_id & 0x7FFU) << 18U;
      xtd_field = 0U;
      rtr_field = 1U;
      break;

    case CAN::Type::REMOTE_EXTENDED:
      id_field = pack_id & 0x1FFFFFFFU;
      xtd_field = 1U;
      rtr_field = 1U;
      break;

    default:
      ASSERT(false);
      break;
  }
}

void mspm0_canfd_fill_tx_elem(const FDCAN::ClassicPack& pack, DL_MCAN_TxBufElement& elem)
{
  memset(&elem, 0, sizeof(elem));

  mspm0_canfd_pack_type_to_id(elem.id, elem.xtd, elem.rtr, pack.id, pack.type);

  elem.dlc = (pack.dlc <= sizeof(pack.data)) ? pack.dlc : sizeof(pack.data);
  elem.brs = 0U;
  elem.fdf = 0U;
  elem.efc = 0U;
  elem.mm = 0U;
  memcpy(elem.data, pack.data, sizeof(pack.data));
}

void mspm0_canfd_fill_tx_elem(const FDCAN::FDPack& pack, DL_MCAN_TxBufElement& elem)
{
  memset(&elem, 0, sizeof(elem));

  mspm0_canfd_pack_type_to_id(elem.id, elem.xtd, elem.rtr, pack.id, pack.type);

  elem.dlc = mspm0_canfd_len_to_dlc(pack.len);
  elem.brs = 1U;
  elem.fdf = 1U;
  elem.efc = 0U;
  elem.mm = 0U;

  const uint32_t copy_len =
      (pack.len <= sizeof(pack.data)) ? pack.len : sizeof(pack.data);
  if (copy_len > 0U)
  {
    memcpy(elem.data, pack.data, copy_len);
  }
}

}  // namespace

extern "C"
{
  volatile uint32_t g_mspm0_can_irq_entry_count = 0;
  volatile uint32_t g_mspm0_can_irq_line1_count = 0;
  volatile uint32_t g_mspm0_can_irq_unexpected_iidx_count = 0;
  volatile uint32_t g_mspm0_can_irq_last_iidx = 0;
  volatile uint32_t g_mspm0_can_irq_last_ir = 0;
  volatile uint32_t g_mspm0_can_irq_last_ris = 0;
  volatile uint32_t g_mspm0_can_irq_last_mis = 0;
  volatile uint32_t g_mspm0_can_irq_rf0n_count = 0;
  volatile uint32_t g_mspm0_can_irq_rf1n_count = 0;
  volatile uint32_t g_mspm0_can_irq_tc_count = 0;
  volatile uint32_t g_mspm0_can_irq_ara_count = 0;
  volatile uint32_t g_mspm0_can_irq_mraf_count = 0;
  volatile uint32_t g_mspm0_can_drv_init_stage = 0;
  volatile uint32_t g_mspm0_can_drv_init_first_fault_stage = 0;
  volatile uint32_t g_mspm0_can_drv_init_last_ir = 0;
  volatile uint32_t g_mspm0_can_drv_init_last_ris = 0;
  volatile uint32_t g_mspm0_can_drv_init_last_mis = 0;
}

MSPM0CANFD* MSPM0CANFD::instance_map_[MAX_CAN_INSTANCES] = {nullptr};

MSPM0CANFD::MSPM0CANFD(Resources res, uint32_t tx_pool_size)
    : FDCAN(), res_(res), tx_pool_(tx_pool_size), tx_pool_fd_(tx_pool_size)
{
  ASSERT(res_.instance != nullptr);
  ASSERT(res_.index < MAX_CAN_INSTANCES);
  ASSERT(instance_map_[res_.index] == nullptr);

  instance_map_[res_.index] = this;

  NVIC_ClearPendingIRQ(res_.irqn);
  NVIC_EnableIRQ(res_.irqn);

  const ErrorCode INIT_ANS = Init();
  ASSERT(INIT_ANS == ErrorCode::OK);
}

ErrorCode MSPM0CANFD::Init()
{
  uint32_t timeout = INIT_TIMEOUT;
  while (!DL_MCAN_isMemInitDone(res_.instance))
  {
    if (timeout-- == 0U)
    {
      return ErrorCode::BUSY;
    }
  }

  timeout = INIT_TIMEOUT;
  while (DL_MCAN_getOpMode(res_.instance) != DL_MCAN_OPERATION_MODE_NORMAL)
  {
    if (timeout-- == 0U)
    {
      return ErrorCode::BUSY;
    }
  }

  DL_MCAN_enableIntr(res_.instance, MSPM0_CANFD_INTR_MASK, true);
  DL_MCAN_selectIntrLine(res_.instance, DL_MCAN_INTR_MASK_ALL, DL_MCAN_INTR_LINE_NUM_1);
  DL_MCAN_enableIntrLine(res_.instance, DL_MCAN_INTR_LINE_NUM_1, true);

  DL_MCAN_clearIntrStatus(res_.instance, DL_MCAN_INTR_MASK_ALL,
                          DL_MCAN_INTR_SRC_MCAN_LINE_1);

  DL_MCAN_clearInterruptStatus(res_.instance, MSPM0_CANFD_MSP_LINE_MASK);
  DL_MCAN_enableInterrupt(res_.instance, MSPM0_CANFD_MSP_LINE_MASK);

  return ErrorCode::OK;
}

ErrorCode MSPM0CANFD::SetConfig(const CAN::Configuration& cfg)
{
  FDCAN::Configuration fd_cfg = {};
  fd_cfg.bitrate = cfg.bitrate;
  fd_cfg.sample_point = cfg.sample_point;
  fd_cfg.bit_timing = cfg.bit_timing;
  fd_cfg.mode = cfg.mode;
  return SetConfig(fd_cfg);
}

ErrorCode MSPM0CANFD::SetConfig(const FDCAN::Configuration& cfg)
{
  if (res_.instance == nullptr)
  {
    return ErrorCode::ARG_ERR;
  }

  (void)cfg.bitrate;
  (void)cfg.sample_point;
  (void)cfg.data_bitrate;
  (void)cfg.data_sample_point;
  (void)cfg.mode.triple_sampling;
  (void)cfg.fd_mode;

  const auto& bt = cfg.bit_timing;
  const auto& dbt = cfg.data_timing;

  constexpr uint32_t NBRP_FIELD_MAX = (MCAN_NBTP_NBRP_MASK >> MCAN_NBTP_NBRP_OFS);
  constexpr uint32_t NTSEG1_FIELD_MAX = (MCAN_NBTP_NTSEG1_MASK >> MCAN_NBTP_NTSEG1_OFS);
  constexpr uint32_t NTSEG2_FIELD_MAX = (MCAN_NBTP_NTSEG2_MASK >> MCAN_NBTP_NTSEG2_OFS);
  constexpr uint32_t NSJW_FIELD_MAX = (MCAN_NBTP_NSJW_MASK >> MCAN_NBTP_NSJW_OFS);

  constexpr uint32_t NBRP_MAX = NBRP_FIELD_MAX + 1U;
  constexpr uint32_t NTSEG1_MAX = NTSEG1_FIELD_MAX + 1U;
  constexpr uint32_t NTSEG2_MAX = NTSEG2_FIELD_MAX + 1U;
  constexpr uint32_t NSJW_MAX = NSJW_FIELD_MAX + 1U;

  if ((bt.brp != 0U) && ((bt.brp < 1U) || (bt.brp > NBRP_MAX)))
  {
    ASSERT(false);
    return ErrorCode::ARG_ERR;
  }

  const uint32_t requested_tseg1 = bt.prop_seg + bt.phase_seg1;
  if ((bt.prop_seg != 0U) || (bt.phase_seg1 != 0U))
  {
    if ((requested_tseg1 < 1U) || (requested_tseg1 > NTSEG1_MAX))
    {
      ASSERT(false);
      return ErrorCode::ARG_ERR;
    }
  }

  if ((bt.phase_seg2 != 0U) && ((bt.phase_seg2 < 1U) || (bt.phase_seg2 > NTSEG2_MAX)))
  {
    ASSERT(false);
    return ErrorCode::ARG_ERR;
  }

  if (bt.sjw != 0U)
  {
    if ((bt.sjw < 1U) || (bt.sjw > NSJW_MAX))
    {
      ASSERT(false);
      return ErrorCode::ARG_ERR;
    }

    if ((bt.phase_seg2 != 0U) && (bt.sjw > bt.phase_seg2))
    {
      ASSERT(false);
      return ErrorCode::ARG_ERR;
    }
  }

  constexpr uint32_t DBRP_FIELD_MAX = (MCAN_DBTP_DBRP_MASK >> MCAN_DBTP_DBRP_OFS);
  constexpr uint32_t DTSEG1_FIELD_MAX = (MCAN_DBTP_DTSEG1_MASK >> MCAN_DBTP_DTSEG1_OFS);
  constexpr uint32_t DTSEG2_FIELD_MAX = (MCAN_DBTP_DTSEG2_MASK >> MCAN_DBTP_DTSEG2_OFS);
  constexpr uint32_t DSJW_FIELD_MAX = (MCAN_DBTP_DSJW_MASK >> MCAN_DBTP_DSJW_OFS);

  constexpr uint32_t DBRP_MAX = DBRP_FIELD_MAX + 1U;
  constexpr uint32_t DTSEG1_MAX = DTSEG1_FIELD_MAX + 1U;
  constexpr uint32_t DTSEG2_MAX = DTSEG2_FIELD_MAX + 1U;
  constexpr uint32_t DSJW_MAX = DSJW_FIELD_MAX + 1U;

  if ((dbt.brp != 0U) && ((dbt.brp < 1U) || (dbt.brp > DBRP_MAX)))
  {
    ASSERT(false);
    return ErrorCode::ARG_ERR;
  }

  const uint32_t requested_dtseg1 = dbt.prop_seg + dbt.phase_seg1;
  if ((dbt.prop_seg != 0U) || (dbt.phase_seg1 != 0U))
  {
    if ((requested_dtseg1 < 1U) || (requested_dtseg1 > DTSEG1_MAX))
    {
      ASSERT(false);
      return ErrorCode::ARG_ERR;
    }
  }

  if ((dbt.phase_seg2 != 0U) &&
      ((dbt.phase_seg2 < 1U) || (dbt.phase_seg2 > DTSEG2_MAX)))
  {
    ASSERT(false);
    return ErrorCode::ARG_ERR;
  }

  if (dbt.sjw != 0U)
  {
    if ((dbt.sjw < 1U) || (dbt.sjw > DSJW_MAX))
    {
      ASSERT(false);
      return ErrorCode::ARG_ERR;
    }

    if ((dbt.phase_seg2 != 0U) && (dbt.sjw > dbt.phase_seg2))
    {
      ASSERT(false);
      return ErrorCode::ARG_ERR;
    }
  }

  DL_MCAN_BitTimingParams hw_timing = {};
  DL_MCAN_getBitTime(res_.instance, &hw_timing);

  const uint32_t effective_brp =
      (bt.brp != 0U) ? bt.brp : (hw_timing.nomRatePrescalar + 1U);
  const uint32_t effective_tseg1 =
      (requested_tseg1 != 0U) ? requested_tseg1 : (hw_timing.nomTimeSeg1 + 1U);
  const uint32_t effective_tseg2 =
      (bt.phase_seg2 != 0U) ? bt.phase_seg2 : (hw_timing.nomTimeSeg2 + 1U);
  const uint32_t effective_sjw =
      (bt.sjw != 0U) ? bt.sjw : (hw_timing.nomSynchJumpWidth + 1U);

  const uint32_t effective_dbrp =
      (dbt.brp != 0U) ? dbt.brp : (hw_timing.dataRatePrescalar + 1U);
  const uint32_t effective_dtseg1 =
      (requested_dtseg1 != 0U) ? requested_dtseg1 : (hw_timing.dataTimeSeg1 + 1U);
  const uint32_t effective_dtseg2 =
      (dbt.phase_seg2 != 0U) ? dbt.phase_seg2 : (hw_timing.dataTimeSeg2 + 1U);
  const uint32_t effective_dsjw =
      (dbt.sjw != 0U) ? dbt.sjw : (hw_timing.dataSynchJumpWidth + 1U);

  if (effective_sjw > effective_tseg2)
  {
    ASSERT(false);
    return ErrorCode::ARG_ERR;
  }

  if (effective_dsjw > effective_dtseg2)
  {
    ASSERT(false);
    return ErrorCode::ARG_ERR;
  }

  const uint32_t irqn_enabled = NVIC_GetEnableIRQ(res_.irqn);
  NVIC_DisableIRQ(res_.irqn);
  NVIC_ClearPendingIRQ(res_.irqn);

  DL_MCAN_enableIntr(res_.instance, MSPM0_CANFD_INTR_MASK, false);
  DL_MCAN_enableIntrLine(res_.instance, DL_MCAN_INTR_LINE_NUM_1, false);
  DL_MCAN_disableInterrupt(res_.instance, MSPM0_CANFD_MSP_LINE_MASK);

  ErrorCode ec = ErrorCode::FAILED;
  bool controller_normal = false;

  do
  {
    DL_MCAN_setOpMode(res_.instance, DL_MCAN_OPERATION_MODE_SW_INIT);

    uint32_t timeout = INIT_TIMEOUT;
    while (DL_MCAN_getOpMode(res_.instance) != DL_MCAN_OPERATION_MODE_SW_INIT)
    {
      if (timeout-- == 0U)
      {
        ec = ErrorCode::BUSY;
        break;
      }
    }
    if (ec != ErrorCode::FAILED)
    {
      break;
    }

    MCAN_MCAN_Regs* core = &res_.instance->MCANSS.MCAN;

    uint32_t cccr = core->MCAN_CCCR;
    cccr = mspm0_canfd_update_field(cccr, MCAN_CCCR_CCE_MASK, MCAN_CCCR_CCE_OFS, 1U);
    core->MCAN_CCCR = cccr;

    timeout = INIT_TIMEOUT;
    while ((core->MCAN_CCCR & MCAN_CCCR_CCE_MASK) == 0U)
    {
      if (timeout-- == 0U)
      {
        ec = ErrorCode::BUSY;
        break;
      }
    }
    if (ec != ErrorCode::FAILED)
    {
      break;
    }

    cccr = core->MCAN_CCCR;
    cccr = cfg.mode.one_shot ? (cccr | MCAN_CCCR_DAR_MASK) : (cccr & ~MCAN_CCCR_DAR_MASK);
    cccr = cfg.mode.listen_only ? (cccr | MCAN_CCCR_MON_MASK) : (cccr & ~MCAN_CCCR_MON_MASK);

    if (cfg.mode.loopback)
    {
      cccr |= MCAN_CCCR_TEST_MASK;
      core->MCAN_CCCR = cccr;
      core->MCAN_TEST |= MCAN_TEST_LBCK_MASK;
    }
    else
    {
      core->MCAN_TEST &= ~MCAN_TEST_LBCK_MASK;
      cccr &= ~MCAN_CCCR_TEST_MASK;
      core->MCAN_CCCR = cccr;
    }

    DL_MCAN_BitTimingParams new_timing = hw_timing;
    new_timing.nomRatePrescalar = effective_brp - 1U;
    new_timing.nomTimeSeg1 = effective_tseg1 - 1U;
    new_timing.nomTimeSeg2 = effective_tseg2 - 1U;
    new_timing.nomSynchJumpWidth = effective_sjw - 1U;
    new_timing.dataRatePrescalar = effective_dbrp - 1U;
    new_timing.dataTimeSeg1 = effective_dtseg1 - 1U;
    new_timing.dataTimeSeg2 = effective_dtseg2 - 1U;
    new_timing.dataSynchJumpWidth = effective_dsjw - 1U;

    ec = mspm0_canfd_status_to_error(DL_MCAN_setBitTime(res_.instance, &new_timing));
    if (ec != ErrorCode::OK)
    {
      break;
    }

    DL_MCAN_setOpMode(res_.instance, DL_MCAN_OPERATION_MODE_NORMAL);

    timeout = INIT_TIMEOUT;
    while (DL_MCAN_getOpMode(res_.instance) != DL_MCAN_OPERATION_MODE_NORMAL)
    {
      if (timeout-- == 0U)
      {
        ec = ErrorCode::BUSY;
        break;
      }
    }
    if (ec != ErrorCode::OK)
    {
      break;
    }

    controller_normal = true;
  } while (false);

  if (!controller_normal)
  {
    DL_MCAN_setOpMode(res_.instance, DL_MCAN_OPERATION_MODE_NORMAL);

    uint32_t timeout = INIT_TIMEOUT;
    while (DL_MCAN_getOpMode(res_.instance) != DL_MCAN_OPERATION_MODE_NORMAL)
    {
      if (timeout-- == 0U)
      {
        controller_normal = false;
        break;
      }
    }
    controller_normal =
        (DL_MCAN_getOpMode(res_.instance) == DL_MCAN_OPERATION_MODE_NORMAL);
  }

  if (controller_normal)
  {
    DL_MCAN_clearIntrStatus(res_.instance, DL_MCAN_INTR_MASK_ALL,
                            DL_MCAN_INTR_SRC_MCAN_LINE_1);
    DL_MCAN_clearInterruptStatus(res_.instance, MSPM0_CANFD_MSP_LINE_MASK);

    DL_MCAN_enableIntr(res_.instance, MSPM0_CANFD_INTR_MASK, true);
    DL_MCAN_selectIntrLine(res_.instance, DL_MCAN_INTR_MASK_ALL, DL_MCAN_INTR_LINE_NUM_1);
    DL_MCAN_enableIntrLine(res_.instance, DL_MCAN_INTR_LINE_NUM_1, true);
    DL_MCAN_enableInterrupt(res_.instance, MSPM0_CANFD_MSP_LINE_MASK);
  }

  NVIC_ClearPendingIRQ(res_.irqn);
  if (irqn_enabled != 0U)
  {
    NVIC_EnableIRQ(res_.irqn);
  }

  return ec;
}

uint32_t MSPM0CANFD::GetClockFreq() const
{
  return MSPM0CAN::ResolveClockFreq(res_.instance);
}

ErrorCode MSPM0CANFD::SendImmediateClassic(const ClassicPack& pack)
{
  DL_MCAN_TxFIFOStatus tx_status = {};
  DL_MCAN_getTxFIFOQueStatus(res_.instance, &tx_status);

  if (tx_status.freeLvl == 0U)
  {
    return ErrorCode::BUSY;
  }

  DL_MCAN_TxBufElement tx_elem = {};
  mspm0_canfd_fill_tx_elem(pack, tx_elem);

  const uint32_t TX_INDEX = tx_status.putIdx;
  DL_MCAN_writeMsgRam(res_.instance, DL_MCAN_MEM_TYPE_FIFO, TX_INDEX, &tx_elem);

  return mspm0_canfd_status_to_error(DL_MCAN_TXBufAddReq(res_.instance, TX_INDEX));
}

ErrorCode MSPM0CANFD::SendImmediateFD(const FDPack& pack)
{
  DL_MCAN_TxFIFOStatus tx_status = {};
  DL_MCAN_getTxFIFOQueStatus(res_.instance, &tx_status);

  if (tx_status.freeLvl == 0U)
  {
    return ErrorCode::BUSY;
  }

  DL_MCAN_TxBufElement tx_elem = {};
  mspm0_canfd_fill_tx_elem(pack, tx_elem);

  const uint32_t TX_INDEX = tx_status.putIdx;
  DL_MCAN_writeMsgRam(res_.instance, DL_MCAN_MEM_TYPE_FIFO, TX_INDEX, &tx_elem);

  return mspm0_canfd_status_to_error(DL_MCAN_TXBufAddReq(res_.instance, TX_INDEX));
}

ErrorCode MSPM0CANFD::AddMessage(const ClassicPack& pack)
{
  if (pack.type == Type::ERROR)
  {
    return ErrorCode::ARG_ERR;
  }

  if (tx_pool_.Put(pack) != ErrorCode::OK)
  {
    ProcessTxInterrupt();
    if (tx_pool_.Put(pack) != ErrorCode::OK)
    {
      return ErrorCode::FULL;
    }
  }

  ProcessTxInterrupt();
  return ErrorCode::OK;
}

ErrorCode MSPM0CANFD::AddMessage(const FDPack& pack)
{
  if (pack.type == Type::ERROR)
  {
    return ErrorCode::ARG_ERR;
  }

  if (pack.len > 64U)
  {
    return ErrorCode::ARG_ERR;
  }

  if ((pack.type != Type::STANDARD) && (pack.type != Type::EXTENDED))
  {
    ASSERT(false);
    return ErrorCode::FAILED;
  }

  if (tx_pool_fd_.Put(pack) != ErrorCode::OK)
  {
    ProcessTxInterrupt();
    if (tx_pool_fd_.Put(pack) != ErrorCode::OK)
    {
      return ErrorCode::FULL;
    }
  }

  ProcessTxInterrupt();
  return ErrorCode::OK;
}

ErrorCode MSPM0CANFD::GetErrorState(CAN::ErrorState& state) const
{
  if (res_.instance == nullptr)
  {
    return ErrorCode::ARG_ERR;
  }

  DL_MCAN_ErrCntStatus err_counter = {};
  DL_MCAN_ProtocolStatus protocol_status = {};
  DL_MCAN_getErrCounters(res_.instance, &err_counter);
  DL_MCAN_getProtocolStatus(res_.instance, &protocol_status);

  state.tx_error_counter = static_cast<uint8_t>(err_counter.transErrLogCnt & 0xFFU);
  state.rx_error_counter = static_cast<uint8_t>(err_counter.recErrCnt & 0xFFU);
  state.bus_off = (protocol_status.busOffStatus != 0U);
  state.error_passive =
      ((protocol_status.errPassive != 0U) || (err_counter.rpStatus != 0U));
  state.error_warning = (protocol_status.warningStatus != 0U);

  return ErrorCode::OK;
}

void MSPM0CANFD::ProcessTxInterrupt()
{
  tx_pend_.store(1U, std::memory_order_release);

  uint32_t expected = 0U;
  if (!tx_lock_.compare_exchange_strong(expected, 1U, std::memory_order_acquire,
                                        std::memory_order_relaxed))
  {
    return;
  }

  for (;;)
  {
    tx_pend_.store(0U, std::memory_order_release);

    while (true)
    {
      DL_MCAN_TxFIFOStatus tx_status = {};
      DL_MCAN_getTxFIFOQueStatus(res_.instance, &tx_status);
      if (tx_status.freeLvl == 0U)
      {
        break;
      }

      FDPack next_fd_pack = {};
      if (tx_pool_fd_.Get(next_fd_pack) == ErrorCode::OK)
      {
        if (SendImmediateFD(next_fd_pack) != ErrorCode::OK)
        {
          if (tx_pool_fd_.Put(next_fd_pack) != ErrorCode::OK)
          {
            ASSERT(false);
          }
          break;
        }
        continue;
      }

      ClassicPack next_pack = {};
      if (tx_pool_.Get(next_pack) == ErrorCode::OK)
      {
        if (SendImmediateClassic(next_pack) != ErrorCode::OK)
        {
          if (tx_pool_.Put(next_pack) != ErrorCode::OK)
          {
            ASSERT(false);
          }
          break;
        }
        continue;
      }

      break;
    }

    tx_lock_.store(0U, std::memory_order_release);

    if (tx_pend_.load(std::memory_order_acquire) == 0U)
    {
      return;
    }

    expected = 0U;
    if (!tx_lock_.compare_exchange_strong(expected, 1U, std::memory_order_acquire,
                                          std::memory_order_relaxed))
    {
      return;
    }
  }
}

void MSPM0CANFD::ProcessErrorStatusInterrupt(uint32_t intr_status)
{
  ClassicPack pack = {};
  pack.type = Type::ERROR;
  pack.dlc = 0U;

  DL_MCAN_ProtocolStatus protocol_status = {};
  DL_MCAN_getProtocolStatus(res_.instance, &protocol_status);

  pack.id = CAN::FromErrorID(mspm0_canfd_error_id_from_status(protocol_status, intr_status));
  OnMessage(pack, true);
}

void MSPM0CANFD::ProcessRxFIFO(uint32_t fifo_num)
{
  DL_MCAN_RxFIFOStatus fifo_status = {};
  fifo_status.num = fifo_num;

  while (true)
  {
    DL_MCAN_getRxFIFOStatus(res_.instance, &fifo_status);
    if (fifo_status.fillLvl == 0U)
    {
      return;
    }

    DL_MCAN_RxBufElement rx_elem = {};
    DL_MCAN_readMsgRam(res_.instance, DL_MCAN_MEM_TYPE_FIFO, 0U, fifo_num, &rx_elem);

    const int32_t ACK_ANS =
        DL_MCAN_writeRxFIFOAck(res_.instance, fifo_num, fifo_status.getIdx);
    ASSERT(ACK_ANS == 0);
    UNUSED(ACK_ANS);

    if (rx_elem.fdf != 0U)
    {
      FDPack pack_fd = {};
      if (rx_elem.xtd != 0U)
      {
        pack_fd.id = rx_elem.id & 0x1FFFFFFFU;
        pack_fd.type = (rx_elem.rtr != 0U) ? Type::REMOTE_EXTENDED : Type::EXTENDED;
      }
      else
      {
        pack_fd.id = (rx_elem.id >> 18U) & 0x7FFU;
        pack_fd.type = (rx_elem.rtr != 0U) ? Type::REMOTE_STANDARD : Type::STANDARD;
      }

      pack_fd.len = static_cast<uint8_t>(mspm0_canfd_dlc_to_len(rx_elem.dlc));
      const size_t fd_copy_len =
          (pack_fd.len < sizeof(pack_fd.data)) ? pack_fd.len : sizeof(pack_fd.data);
      if (fd_copy_len > 0U)
      {
        memcpy(pack_fd.data, rx_elem.data, fd_copy_len);
      }

      OnMessage(pack_fd, true);
    }
    else
    {
      ClassicPack pack = {};
      if (rx_elem.xtd != 0U)
      {
        pack.id = rx_elem.id & 0x1FFFFFFFU;
        pack.type = (rx_elem.rtr != 0U) ? Type::REMOTE_EXTENDED : Type::EXTENDED;
      }
      else
      {
        pack.id = (rx_elem.id >> 18U) & 0x7FFU;
        pack.type = (rx_elem.rtr != 0U) ? Type::REMOTE_STANDARD : Type::STANDARD;
      }

      const uint32_t payload_len = mspm0_canfd_dlc_to_len(rx_elem.dlc);
      pack.dlc = static_cast<uint8_t>(
          (payload_len <= sizeof(pack.data)) ? payload_len : sizeof(pack.data));
      const size_t copy_len =
          (payload_len < sizeof(pack.data)) ? payload_len : sizeof(pack.data);
      if (copy_len > 0U)
      {
        memcpy(pack.data, rx_elem.data, copy_len);
      }

      OnMessage(pack, true);
    }
  }
}

void MSPM0CANFD::HandleMcanLineInterrupt(DL_MCAN_INTR_SRC_MCAN line)
{
  const uint32_t intr_status = DL_MCAN_getIntrStatus(res_.instance);
  g_mspm0_can_irq_last_ir = intr_status;
  if (intr_status == 0U)
  {
    return;
  }

  if ((intr_status & DL_MCAN_INTR_SRC_RX_FIFO0_NEW_MSG) != 0U)
  {
    g_mspm0_can_irq_rf0n_count++;
  }

  if ((intr_status & DL_MCAN_INTR_SRC_RX_FIFO1_NEW_MSG) != 0U)
  {
    g_mspm0_can_irq_rf1n_count++;
  }

  if ((intr_status & DL_MCAN_INTR_SRC_TRANS_COMPLETE) != 0U)
  {
    g_mspm0_can_irq_tc_count++;
  }

  if ((intr_status & DL_MCAN_INTR_SRC_RES_ADDR_ACCESS) != 0U)
  {
    g_mspm0_can_irq_ara_count++;
  }

  if ((intr_status & DL_MCAN_INTR_SRC_MSG_RAM_ACCESS_FAILURE) != 0U)
  {
    g_mspm0_can_irq_mraf_count++;
  }

  DL_MCAN_clearIntrStatus(res_.instance, intr_status, line);

  if ((intr_status & MSPM0_CANFD_ERROR_INTR_MASK) != 0U)
  {
    ProcessErrorStatusInterrupt(intr_status);
    ProcessTxInterrupt();
  }

  if ((intr_status & DL_MCAN_INTR_SRC_RX_FIFO0_NEW_MSG) != 0U)
  {
    ProcessRxFIFO(DL_MCAN_RX_FIFO_NUM_0);
  }

  if ((intr_status & DL_MCAN_INTR_SRC_RX_FIFO1_NEW_MSG) != 0U)
  {
    ProcessRxFIFO(DL_MCAN_RX_FIFO_NUM_1);
  }

  if ((intr_status &
       (DL_MCAN_INTR_SRC_TRANS_COMPLETE | DL_MCAN_INTR_SRC_TRANS_CANCEL_FINISH |
        DL_MCAN_INTR_SRC_TX_FIFO_EMPTY)) != 0U)
  {
    ProcessTxInterrupt();
  }
}

void MSPM0CANFD::HandleInterrupt()
{
  for (uint32_t round = 0; round < 32U; ++round)
  {
    g_mspm0_can_irq_last_ris =
        DL_MCAN_getRawInterruptStatus(res_.instance, MSPM0_CANFD_MSP_LINE_MASK);
    g_mspm0_can_irq_last_mis =
        DL_MCAN_getEnabledInterruptStatus(res_.instance, MSPM0_CANFD_MSP_LINE_MASK);

    const DL_MCAN_IIDX pending = DL_MCAN_getPendingInterrupt(res_.instance);
    g_mspm0_can_irq_last_iidx = static_cast<uint32_t>(pending);

    if (pending == DL_MCAN_IIDX_LINE1)
    {
      g_mspm0_can_irq_line1_count++;
      HandleMcanLineInterrupt(DL_MCAN_INTR_SRC_MCAN_LINE_1);
      continue;
    }

    if (static_cast<uint32_t>(pending) != 0U)
    {
      g_mspm0_can_irq_unexpected_iidx_count++;
    }
    return;
  }
}

void MSPM0CANFD::OnInterrupt(uint8_t index)
{
  if (index >= MAX_CAN_INSTANCES)
  {
    return;
  }

  MSPM0CANFD* can = instance_map_[index];
  if (can == nullptr)
  {
    return;
  }

  can->HandleInterrupt();
}

#if defined(CANFD0_BASE)
extern "C" void CANFD0_IRQHandler(void)  // NOLINT
{
  g_mspm0_can_irq_entry_count++;
  g_mspm0_can_irq_last_ir = DL_MCAN_getIntrStatus(CANFD0);
  g_mspm0_can_irq_last_ris =
      DL_MCAN_getRawInterruptStatus(CANFD0, DL_MCAN_MSP_INTERRUPT_LINE1);
  g_mspm0_can_irq_last_mis =
      DL_MCAN_getEnabledInterruptStatus(CANFD0, DL_MCAN_MSP_INTERRUPT_LINE1);
  LibXR::MSPM0CAN::OnInterrupt(0);
  LibXR::MSPM0CANFD::OnInterrupt(0);
}
#endif

#if defined(CANFD1_BASE)
extern "C" void CANFD1_IRQHandler(void)  // NOLINT
{
  LibXR::MSPM0CAN::OnInterrupt(1);
  LibXR::MSPM0CANFD::OnInterrupt(1);
}
#endif

#endif
