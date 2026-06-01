#include "mspm0_canfd.hpp"
#include "mspm0_can.hpp"

#if defined(__MSPM0_HAS_MCAN__)

#include <cstring>

using namespace LibXR;

namespace
{

constexpr uint32_t MSPM0_CANFD_MSP_LINE_MASK =
    DL_MCAN_MSP_INTERRUPT_LINE1;

constexpr uint32_t MSPM0_CANFD_INTR_MASK =
    DL_MCAN_INTR_SRC_RX_FIFO0_NEW_MSG | DL_MCAN_INTR_SRC_RX_FIFO1_NEW_MSG |
    DL_MCAN_INTR_SRC_TRANS_COMPLETE | DL_MCAN_INTR_SRC_TRANS_CANCEL_FINISH |
    DL_MCAN_INTR_SRC_TX_FIFO_EMPTY | DL_MCAN_INTR_SRC_BUS_OFF_STATUS |
    DL_MCAN_INTR_SRC_PROTOCOL_ERR_ARB | DL_MCAN_INTR_SRC_PROTOCOL_ERR_DATA |
    DL_MCAN_INTR_SRC_MSG_RAM_ACCESS_FAILURE;

constexpr uint8_t MSPM0_CANFD_DLC_TO_LEN[16] = {0, 1, 2, 3, 4, 5, 6, 7,
                                                 8, 12, 16, 20, 24, 32, 48, 64};

constexpr uint32_t mspm0_canfd_dlc_to_len(uint32_t dlc)
{
  return MSPM0_CANFD_DLC_TO_LEN[(dlc < 16U) ? dlc : 15U];
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

  elem.dlc = 8U;
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

  const uint32_t copy_len = (pack.len <= sizeof(pack.data)) ? pack.len : sizeof(pack.data);
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
  DL_MCAN_selectIntrLine(res_.instance, DL_MCAN_INTR_MASK_ALL,
                         DL_MCAN_INTR_LINE_NUM_1);
  DL_MCAN_enableIntrLine(res_.instance, DL_MCAN_INTR_LINE_NUM_1, true);

  DL_MCAN_clearIntrStatus(res_.instance, DL_MCAN_INTR_MASK_ALL,
                          DL_MCAN_INTR_SRC_MCAN_LINE_1);

  DL_MCAN_clearInterruptStatus(res_.instance, MSPM0_CANFD_MSP_LINE_MASK);
  DL_MCAN_enableInterrupt(res_.instance, MSPM0_CANFD_MSP_LINE_MASK);

  return ErrorCode::OK;
}

ErrorCode MSPM0CANFD::TrySendImmediateClassic(const ClassicPack& pack)
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

ErrorCode MSPM0CANFD::TrySendImmediateFD(const FDPack& pack)
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
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();

  const ErrorCode SEND_ANS = TrySendImmediateClassic(pack);
  if (SEND_ANS == ErrorCode::OK)
  {
    if (primask == 0U)
    {
      __enable_irq();
    }
    return ErrorCode::OK;
  }

  if (SEND_ANS != ErrorCode::BUSY)
  {
    if (primask == 0U)
    {
      __enable_irq();
    }
    return SEND_ANS;
  }

  uint32_t slot = 0U;
  const ErrorCode PUT_ANS = tx_pool_.Put(pack, slot);

  ProcessTxInterrupt();

  if (primask == 0U)
  {
    __enable_irq();
  }

  if (PUT_ANS != ErrorCode::OK)
  {
    return ErrorCode::FULL;
  }

  return ErrorCode::OK;
}

ErrorCode MSPM0CANFD::AddMessage(const FDPack& pack)
{
  if (pack.len > 64U)
  {
    return ErrorCode::ARG_ERR;
  }

  const uint32_t primask = __get_PRIMASK();
  __disable_irq();

  const ErrorCode SEND_ANS = TrySendImmediateFD(pack);
  if (SEND_ANS == ErrorCode::OK)
  {
    if (primask == 0U)
    {
      __enable_irq();
    }
    return ErrorCode::OK;
  }

  if (SEND_ANS != ErrorCode::BUSY)
  {
    if (primask == 0U)
    {
      __enable_irq();
    }
    return SEND_ANS;
  }

  uint32_t slot = 0U;
  const ErrorCode PUT_ANS = tx_pool_fd_.Put(pack, slot);

  ProcessTxInterrupt();

  if (primask == 0U)
  {
    __enable_irq();
  }

  if (PUT_ANS != ErrorCode::OK)
  {
    return ErrorCode::FULL;
  }

  return ErrorCode::OK;
}

void MSPM0CANFD::ProcessTxInterrupt()
{
  DL_MCAN_TxFIFOStatus tx_status = {};

  while (true)
  {
    DL_MCAN_getTxFIFOQueStatus(res_.instance, &tx_status);
    if (tx_status.freeLvl == 0U)
    {
      return;
    }

    FDPack next_fd_pack = {};
    if (tx_pool_fd_.Get(next_fd_pack) == ErrorCode::OK)
    {
      const ErrorCode SEND_ANS = TrySendImmediateFD(next_fd_pack);
      if (SEND_ANS != ErrorCode::OK)
      {
        ASSERT(SEND_ANS == ErrorCode::BUSY);
        uint32_t fd_slot = 0U;
        const ErrorCode REQUEUE_FD_ANS = tx_pool_fd_.Put(next_fd_pack, fd_slot);
        ASSERT(REQUEUE_FD_ANS == ErrorCode::OK);
        UNUSED(REQUEUE_FD_ANS);
        return;
      }
      continue;
    }

    ClassicPack next_pack = {};
    if (tx_pool_.Get(next_pack) != ErrorCode::OK)
    {
      return;
    }

    const ErrorCode SEND_ANS = TrySendImmediateClassic(next_pack);
    if (SEND_ANS != ErrorCode::OK)
    {
      ASSERT(SEND_ANS == ErrorCode::BUSY);
      uint32_t slot = 0U;
      const ErrorCode REQUEUE_ANS = tx_pool_.Put(next_pack, slot);
      ASSERT(REQUEUE_ANS == ErrorCode::OK);
      UNUSED(REQUEUE_ANS);
      return;
    }
  }
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
    DL_MCAN_readMsgRam(res_.instance, DL_MCAN_MEM_TYPE_FIFO, 0U, fifo_num,
                       &rx_elem);

    const int32_t ACK_ANS = DL_MCAN_writeRxFIFOAck(res_.instance, fifo_num, fifo_status.getIdx);
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
      const size_t fd_copy_len = (pack_fd.len < sizeof(pack_fd.data)) ? pack_fd.len : sizeof(pack_fd.data);
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
      pack.dlc = static_cast<uint8_t>((payload_len <= sizeof(pack.data)) ? payload_len
                                                                          : sizeof(pack.data));
      const size_t copy_len = (payload_len < sizeof(pack.data)) ? payload_len : sizeof(pack.data);
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

  if ((intr_status & DL_MCAN_INTR_SRC_RX_FIFO0_NEW_MSG) != 0U)
  {
    ProcessRxFIFO(DL_MCAN_RX_FIFO_NUM_0);
  }

  if ((intr_status & DL_MCAN_INTR_SRC_RX_FIFO1_NEW_MSG) != 0U)
  {
    ProcessRxFIFO(DL_MCAN_RX_FIFO_NUM_1);
  }

  if ((intr_status & (DL_MCAN_INTR_SRC_TRANS_COMPLETE |
                      DL_MCAN_INTR_SRC_TRANS_CANCEL_FINISH |
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
