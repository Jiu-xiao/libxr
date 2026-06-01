#include "mspm0_can.hpp"

#if defined(__MSPM0_HAS_MCAN__)

#include <cstring>

using namespace LibXR;

extern "C"
{
  extern volatile uint32_t g_mspm0_can_irq_line1_count;
  extern volatile uint32_t g_mspm0_can_irq_unexpected_iidx_count;
  extern volatile uint32_t g_mspm0_can_irq_last_iidx;
  extern volatile uint32_t g_mspm0_can_irq_last_ir;
  extern volatile uint32_t g_mspm0_can_irq_last_ris;
  extern volatile uint32_t g_mspm0_can_irq_last_mis;
  extern volatile uint32_t g_mspm0_can_irq_rf0n_count;
  extern volatile uint32_t g_mspm0_can_irq_rf1n_count;
  extern volatile uint32_t g_mspm0_can_irq_tc_count;
  extern volatile uint32_t g_mspm0_can_irq_ara_count;
  extern volatile uint32_t g_mspm0_can_irq_mraf_count;
  extern volatile uint32_t g_mspm0_can_drv_init_stage;
  extern volatile uint32_t g_mspm0_can_drv_init_first_fault_stage;
  extern volatile uint32_t g_mspm0_can_drv_init_last_ir;
  extern volatile uint32_t g_mspm0_can_drv_init_last_ris;
  extern volatile uint32_t g_mspm0_can_drv_init_last_mis;
  extern volatile uint32_t g_mspm0_can_clk_dbg_sysosc_hz;
  extern volatile uint32_t g_mspm0_can_clk_dbg_syspll_ref_hz;
  extern volatile uint32_t g_mspm0_can_clk_dbg_syspll_clk1_hz;
  extern volatile uint32_t g_mspm0_can_clk_dbg_canclksrc_raw;
  extern volatile uint32_t g_mspm0_can_clk_dbg_clkdiv_raw;
  extern volatile uint32_t g_mspm0_can_clk_dbg_fclk_hz;
}

namespace
{

constexpr uint32_t MSPM0_CAN_MSP_LINE_MASK =
    DL_MCAN_MSP_INTERRUPT_LINE0 | DL_MCAN_MSP_INTERRUPT_LINE1;

constexpr uint32_t MSPM0_CAN_INTR_MASK =
    DL_MCAN_INTR_SRC_RX_FIFO0_NEW_MSG | DL_MCAN_INTR_SRC_RX_FIFO1_NEW_MSG |
    DL_MCAN_INTR_SRC_TRANS_COMPLETE | DL_MCAN_INTR_SRC_TRANS_CANCEL_FINISH |
    DL_MCAN_INTR_SRC_TX_FIFO_EMPTY | DL_MCAN_INTR_SRC_BUS_OFF_STATUS |
    DL_MCAN_INTR_SRC_PROTOCOL_ERR_ARB | DL_MCAN_INTR_SRC_PROTOCOL_ERR_DATA |
    DL_MCAN_INTR_SRC_MSG_RAM_ACCESS_FAILURE;

constexpr uint32_t MSPM0_CAN_ERROR_INTR_MASK =
    DL_MCAN_INTR_SRC_BUS_OFF_STATUS | DL_MCAN_INTR_SRC_PROTOCOL_ERR_ARB |
    DL_MCAN_INTR_SRC_PROTOCOL_ERR_DATA | DL_MCAN_INTR_SRC_RES_ADDR_ACCESS |
    DL_MCAN_INTR_SRC_MSG_RAM_ACCESS_FAILURE;

constexpr uint32_t MSPM0_SYSOSC_FREQ_4M_HZ = 4000000U;
constexpr uint32_t MSPM0_SYSOSC_FREQ_16M_HZ = 16000000U;
constexpr uint32_t MSPM0_SYSOSC_FREQ_24M_HZ = 24000000U;
constexpr uint32_t MSPM0_SYSOSC_FREQ_32M_HZ = 32000000U;
constexpr uint32_t MSPM0_CAN_CONFIG_TIMEOUT = 300000U;

void mspm0_can_capture_init_state(MCAN_Regs* instance, uint32_t stage)
{
  g_mspm0_can_drv_init_stage = stage;
  g_mspm0_can_drv_init_last_ir = DL_MCAN_getIntrStatus(instance);
  g_mspm0_can_drv_init_last_ris =
      DL_MCAN_getRawInterruptStatus(instance, MSPM0_CAN_MSP_LINE_MASK);
  g_mspm0_can_drv_init_last_mis =
      DL_MCAN_getEnabledInterruptStatus(instance, MSPM0_CAN_MSP_LINE_MASK);

  if ((g_mspm0_can_drv_init_first_fault_stage == 0U) &&
      ((g_mspm0_can_drv_init_last_ir & DL_MCAN_INTR_SRC_RES_ADDR_ACCESS) != 0U))
  {
    g_mspm0_can_drv_init_first_fault_stage = stage;
  }
}

constexpr uint32_t mspm0_can_dlc_to_len(uint32_t dlc)
{
  constexpr uint8_t LENGTH_TABLE[16] = {0, 1,  2,  3,  4,  5,  6,  7,
                                        8, 12, 16, 20, 24, 32, 48, 64};
  return LENGTH_TABLE[(dlc < 16U) ? dlc : 15U];
}

constexpr uint32_t mspm0_can_update_field(uint32_t reg, uint32_t mask, uint32_t ofs,
                                          uint32_t value)
{
  return (reg & ~mask) | ((value << ofs) & mask);
}

constexpr ErrorCode mspm0_can_status_to_error(int32_t status)
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

CAN::ErrorID mspm0_can_error_id_from_status(const DL_MCAN_ProtocolStatus& protocol_status)
{
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

  switch (protocol_status.lastErrCode)
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

    case DL_MCAN_ERR_CODE_NO_ERROR:
    case DL_MCAN_ERR_CODE_NO_CHANGE:
    default:
      return CAN::ErrorID::CAN_ERROR_ID_GENERIC;
  }
}

uint32_t mspm0_can_sysosc_freq_hz()
{
  uint32_t sysosc_hz = CPUCLK_FREQ;

  switch (DL_SYSCTL_getTargetSYSOSCFreq())
  {
    case DL_SYSCTL_SYSOSC_FREQ_4M:
      sysosc_hz = MSPM0_SYSOSC_FREQ_4M_HZ;
      break;

    case DL_SYSCTL_SYSOSC_FREQ_BASE:
      sysosc_hz = MSPM0_SYSOSC_FREQ_32M_HZ;
      break;

    case DL_SYSCTL_SYSOSC_FREQ_USERTRIM:
      switch (SYSCTL->SOCLOCK.SYSOSCTRIMUSER & SYSCTL_SYSOSCTRIMUSER_FREQ_MASK)
      {
        case SYSCTL_SYSOSCTRIMUSER_FREQ_SYSOSC16M:
          sysosc_hz = MSPM0_SYSOSC_FREQ_16M_HZ;
          break;

        case SYSCTL_SYSOSCTRIMUSER_FREQ_SYSOSC24M:
          sysosc_hz = MSPM0_SYSOSC_FREQ_24M_HZ;
          break;

        default:
          sysosc_hz = CPUCLK_FREQ;
          break;
      }
      break;

    default:
      sysosc_hz = CPUCLK_FREQ;
      break;
  }

  g_mspm0_can_clk_dbg_sysosc_hz = sysosc_hz;
  return sysosc_hz;
}

uint32_t mspm0_can_hfclk_freq_hz()
{
#if defined(DL_SYSCTL_HSCLK_SOURCE_HFCLK)
  if ((DL_SYSCTL_getHSCLKSource() == DL_SYSCTL_HSCLK_SOURCE_HFCLK) &&
      (DL_SYSCTL_getMCLKSource() == DL_SYSCTL_MCLK_SOURCE_HSCLK) &&
      (DL_SYSCTL_getMCLKDivider() == DL_SYSCTL_MCLK_DIVIDER_DISABLE))
  {
    return CPUCLK_FREQ;
  }
#endif

  return 0U;
}

uint32_t mspm0_can_syspll_ref_freq_hz()
{
#if defined(DL_SYSCTL_SYSPLL_REF_SYSOSC)
  const uint32_t pll_ref = SYSCTL->SOCLOCK.SYSPLLCFG0 & SYSCTL_SYSPLLCFG0_SYSPLLREF_MASK;
  if (pll_ref == static_cast<uint32_t>(DL_SYSCTL_SYSPLL_REF_SYSOSC))
  {
    const uint32_t sysosc_hz = mspm0_can_sysosc_freq_hz();
    const uint32_t ref_hz = (sysosc_hz != 0U) ? sysosc_hz : CPUCLK_FREQ;
    g_mspm0_can_clk_dbg_syspll_ref_hz = ref_hz;
    return ref_hz;
  }

  if (pll_ref == static_cast<uint32_t>(DL_SYSCTL_SYSPLL_REF_HFCLK))
  {
    const uint32_t ref_hz = mspm0_can_hfclk_freq_hz();
    g_mspm0_can_clk_dbg_syspll_ref_hz = ref_hz;
    return ref_hz;
  }
#endif

  g_mspm0_can_clk_dbg_syspll_ref_hz = 0U;
  return 0U;
}

uint32_t mspm0_can_syspll_pdiv_value()
{
  switch (SYSCTL->SOCLOCK.SYSPLLCFG1 & SYSCTL_SYSPLLCFG1_PDIV_MASK)
  {
    case SYSCTL_SYSPLLCFG1_PDIV_REFDIV1:
      return 1U;

    case SYSCTL_SYSPLLCFG1_PDIV_REFDIV2:
      return 2U;

    case SYSCTL_SYSPLLCFG1_PDIV_REFDIV4:
      return 4U;

    case SYSCTL_SYSPLLCFG1_PDIV_REFDIV8:
      return 8U;

    default:
      return 0U;
  }
}

uint32_t mspm0_can_syspll_clk1_freq_hz()
{
  const uint32_t cfg0 = SYSCTL->SOCLOCK.SYSPLLCFG0;
  if ((cfg0 & SYSCTL_SYSPLLCFG0_ENABLECLK1_MASK) == 0U)
  {
    return 0U;
  }

  const uint32_t ref_hz = mspm0_can_syspll_ref_freq_hz();
  const uint32_t pdiv = mspm0_can_syspll_pdiv_value();
  if ((ref_hz == 0U) || (pdiv == 0U))
  {
    return 0U;
  }

  const uint32_t qdiv = ((SYSCTL->SOCLOCK.SYSPLLCFG1 & SYSCTL_SYSPLLCFG1_QDIV_MASK) >>
                         SYSCTL_SYSPLLCFG1_QDIV_OFS) +
                        1U;
  const uint32_t rdiv_clk1 =
      ((cfg0 & SYSCTL_SYSPLLCFG0_RDIVCLK1_MASK) >> SYSCTL_SYSPLLCFG0_RDIVCLK1_OFS) + 1U;
  const uint32_t clk1_div = 2U * rdiv_clk1;
  const uint64_t vco_hz = (static_cast<uint64_t>(ref_hz) * static_cast<uint64_t>(qdiv)) /
                          static_cast<uint64_t>(pdiv);

  const uint32_t clk1_hz =
      static_cast<uint32_t>(vco_hz / static_cast<uint64_t>(clk1_div));
  g_mspm0_can_clk_dbg_syspll_clk1_hz = clk1_hz;
  return clk1_hz;
}

uint32_t mspm0_can_fclk_divider_hz(DL_MCAN_FCLK_DIV divider)
{
  switch (divider)
  {
    case DL_MCAN_FCLK_DIV_1:
      return 1U;

    case DL_MCAN_FCLK_DIV_2:
      return 2U;

    case DL_MCAN_FCLK_DIV_4:
      return 4U;

    case static_cast<DL_MCAN_FCLK_DIV>(MCAN_CLKDIV_RATIO_DIV_BY_1_1):
      return 1U;

    default:
      return 0U;
  }
}

uint32_t mspm0_can_mcan_fclk_hz(MCAN_Regs* instance)
{
  const uint32_t canclksrc_raw =
      SYSCTL->SOCLOCK.GENCLKCFG & SYSCTL_GENCLKCFG_CANCLKSRC_MASK;
  const uint32_t clkdiv_raw =
      instance->MCANSS.TI_WRAPPER.MSP.MCANSS_CLKDIV & MCAN_CLKDIV_RATIO_MASK;
  g_mspm0_can_clk_dbg_canclksrc_raw = canclksrc_raw;
  g_mspm0_can_clk_dbg_clkdiv_raw = clkdiv_raw;

  uint32_t src_hz = 0U;
  switch (canclksrc_raw)
  {
    case SYSCTL_GENCLKCFG_CANCLKSRC_SYSPLLOUT1:
    {
      const uint32_t cfg0 = SYSCTL->SOCLOCK.SYSPLLCFG0;
      const uint32_t pdiv = mspm0_can_syspll_pdiv_value();
      const uint32_t qdiv = ((SYSCTL->SOCLOCK.SYSPLLCFG1 & SYSCTL_SYSPLLCFG1_QDIV_MASK) >>
                             SYSCTL_SYSPLLCFG1_QDIV_OFS) +
                            1U;
      const uint32_t rdiv_clk1 =
          ((cfg0 & SYSCTL_SYSPLLCFG0_RDIVCLK1_MASK) >> SYSCTL_SYSPLLCFG0_RDIVCLK1_OFS) +
          1U;
      const uint32_t clk1_div = 2U * rdiv_clk1;

      g_mspm0_can_clk_dbg_sysosc_hz = CPUCLK_FREQ;
      g_mspm0_can_clk_dbg_syspll_ref_hz = CPUCLK_FREQ;

      if ((pdiv != 0U) && (clk1_div != 0U))
      {
        src_hz = static_cast<uint32_t>(
            (static_cast<uint64_t>(CPUCLK_FREQ) * static_cast<uint64_t>(qdiv)) /
            (static_cast<uint64_t>(pdiv) * static_cast<uint64_t>(clk1_div)));
      }

      g_mspm0_can_clk_dbg_syspll_clk1_hz = src_hz;
      break;
    }

    case SYSCTL_GENCLKCFG_CANCLKSRC_HFCLK:
      src_hz = mspm0_can_hfclk_freq_hz();
      break;

    default:
      return 0U;
  }

  const uint32_t divider =
      mspm0_can_fclk_divider_hz(static_cast<DL_MCAN_FCLK_DIV>(clkdiv_raw));
  if ((src_hz == 0U) || (divider == 0U))
  {
    g_mspm0_can_clk_dbg_fclk_hz = 0U;
    return 0U;
  }

  const uint32_t fclk_hz = src_hz / divider;
  g_mspm0_can_clk_dbg_fclk_hz = fclk_hz;
  return fclk_hz;
}

uint32_t mspm0_can_tx_dedicated_buf_count(const MCAN_Regs* instance)
{
  return (instance->MCANSS.MCAN.MCAN_TXBC & MCAN_TXBC_NDTB_MASK) >> MCAN_TXBC_NDTB_OFS;
}

uint32_t mspm0_can_tx_fifo_size(const MCAN_Regs* instance)
{
  return (instance->MCANSS.MCAN.MCAN_TXBC & MCAN_TXBC_TFQS_MASK) >> MCAN_TXBC_TFQS_OFS;
}

void mspm0_can_pack_to_tx_elem(const CAN::ClassicPack& pack, DL_MCAN_TxBufElement& elem)
{
  memset(&elem, 0, sizeof(elem));

  switch (pack.type)
  {
    case CAN::Type::STANDARD:
      elem.id = (pack.id & 0x7FFU) << 18U;
      elem.xtd = 0U;
      elem.rtr = 0U;
      break;

    case CAN::Type::EXTENDED:
      elem.id = pack.id & 0x1FFFFFFFU;
      elem.xtd = 1U;
      elem.rtr = 0U;
      break;

    case CAN::Type::REMOTE_STANDARD:
      elem.id = (pack.id & 0x7FFU) << 18U;
      elem.xtd = 0U;
      elem.rtr = 1U;
      break;

    case CAN::Type::REMOTE_EXTENDED:
      elem.id = pack.id & 0x1FFFFFFFU;
      elem.xtd = 1U;
      elem.rtr = 1U;
      break;

    default:
      ASSERT(false);
      break;
  }

  elem.dlc = (pack.dlc <= sizeof(pack.data)) ? pack.dlc : sizeof(pack.data);
  elem.brs = 0U;
  elem.fdf = 0U;
  elem.efc = 0U;
  elem.mm = 0U;
  memcpy(elem.data, pack.data, sizeof(pack.data));
}

}  // namespace

extern "C"
{
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
  volatile uint32_t g_mspm0_can_clk_dbg_sysosc_hz = 0;
  volatile uint32_t g_mspm0_can_clk_dbg_syspll_ref_hz = 0;
  volatile uint32_t g_mspm0_can_clk_dbg_syspll_clk1_hz = 0;
  volatile uint32_t g_mspm0_can_clk_dbg_canclksrc_raw = 0;
  volatile uint32_t g_mspm0_can_clk_dbg_clkdiv_raw = 0;
  volatile uint32_t g_mspm0_can_clk_dbg_fclk_hz = 0;
}

MSPM0CAN* MSPM0CAN::instance_map_[MAX_CAN_INSTANCES] = {nullptr};

MSPM0CAN::MSPM0CAN(Resources res, uint32_t tx_pool_size)
    : CAN(), res_(res), tx_pool_(tx_pool_size)
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

ErrorCode MSPM0CAN::Init()
{
  g_mspm0_can_drv_init_stage = 0U;
  g_mspm0_can_drv_init_first_fault_stage = 0U;
  g_mspm0_can_drv_init_last_ir = 0U;
  g_mspm0_can_drv_init_last_ris = 0U;
  g_mspm0_can_drv_init_last_mis = 0U;
  mspm0_can_capture_init_state(res_.instance, 10U);

  uint32_t timeout = INIT_TIMEOUT;
  while (!DL_MCAN_isMemInitDone(res_.instance))
  {
    if (timeout-- == 0U)
    {
      mspm0_can_capture_init_state(res_.instance, 11U);
      return ErrorCode::BUSY;
    }
  }
  mspm0_can_capture_init_state(res_.instance, 20U);

  timeout = INIT_TIMEOUT;
  while (DL_MCAN_getOpMode(res_.instance) != DL_MCAN_OPERATION_MODE_NORMAL)
  {
    if (timeout-- == 0U)
    {
      mspm0_can_capture_init_state(res_.instance, 21U);
      return ErrorCode::BUSY;
    }
  }
  mspm0_can_capture_init_state(res_.instance, 30U);

  DL_MCAN_enableIntr(res_.instance, MSPM0_CAN_INTR_MASK, true);
  mspm0_can_capture_init_state(res_.instance, 40U);
  DL_MCAN_enableIntrLine(res_.instance, DL_MCAN_INTR_LINE_NUM_0, true);
  mspm0_can_capture_init_state(res_.instance, 50U);
  DL_MCAN_enableIntrLine(res_.instance, DL_MCAN_INTR_LINE_NUM_1, true);
  mspm0_can_capture_init_state(res_.instance, 60U);

  DL_MCAN_clearIntrStatus(res_.instance, DL_MCAN_INTR_MASK_ALL,
                          DL_MCAN_INTR_SRC_MCAN_LINE_0);
  mspm0_can_capture_init_state(res_.instance, 70U);
  DL_MCAN_clearIntrStatus(res_.instance, DL_MCAN_INTR_MASK_ALL,
                          DL_MCAN_INTR_SRC_MCAN_LINE_1);
  mspm0_can_capture_init_state(res_.instance, 80U);

  DL_MCAN_clearInterruptStatus(res_.instance, MSPM0_CAN_MSP_LINE_MASK);
  mspm0_can_capture_init_state(res_.instance, 90U);
  DL_MCAN_enableInterrupt(res_.instance, MSPM0_CAN_MSP_LINE_MASK);
  mspm0_can_capture_init_state(res_.instance, 100U);

  return ErrorCode::OK;
}

ErrorCode MSPM0CAN::SetConfig(const CAN::Configuration& cfg)
{
  if (res_.instance == nullptr)
  {
    return ErrorCode::ARG_ERR;
  }

  (void)cfg.bitrate;
  (void)cfg.sample_point;
  (void)cfg.mode.triple_sampling;

  const auto& bt = cfg.bit_timing;

  constexpr uint32_t BRP_FIELD_MAX =
      (MCAN_NBTP_NBRP_MASK >> MCAN_NBTP_NBRP_OFS);  // stores brp-1
  constexpr uint32_t TSEG1_FIELD_MAX =
      (MCAN_NBTP_NTSEG1_MASK >> MCAN_NBTP_NTSEG1_OFS);  // stores tseg1-1
  constexpr uint32_t TSEG2_FIELD_MAX =
      (MCAN_NBTP_NTSEG2_MASK >> MCAN_NBTP_NTSEG2_OFS);  // stores tseg2-1
  constexpr uint32_t SJW_FIELD_MAX =
      (MCAN_NBTP_NSJW_MASK >> MCAN_NBTP_NSJW_OFS);  // stores sjw-1

  constexpr uint32_t BRP_MAX = BRP_FIELD_MAX + 1U;
  constexpr uint32_t TSEG1_MAX = TSEG1_FIELD_MAX + 1U;
  constexpr uint32_t TSEG2_MAX = TSEG2_FIELD_MAX + 1U;
  constexpr uint32_t SJW_MAX = SJW_FIELD_MAX + 1U;

  if ((bt.brp != 0U) && ((bt.brp < 1U) || (bt.brp > BRP_MAX)))
  {
    ASSERT(false);
    return ErrorCode::ARG_ERR;
  }

  const uint32_t requested_tseg1 = bt.prop_seg + bt.phase_seg1;
  if ((bt.prop_seg != 0U) || (bt.phase_seg1 != 0U))
  {
    if ((requested_tseg1 < 1U) || (requested_tseg1 > TSEG1_MAX))
    {
      ASSERT(false);
      return ErrorCode::ARG_ERR;
    }
  }

  if ((bt.phase_seg2 != 0U) && ((bt.phase_seg2 < 1U) || (bt.phase_seg2 > TSEG2_MAX)))
  {
    ASSERT(false);
    return ErrorCode::ARG_ERR;
  }

  if (bt.sjw != 0U)
  {
    if ((bt.sjw < 1U) || (bt.sjw > SJW_MAX))
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

  if (effective_sjw > effective_tseg2)
  {
    ASSERT(false);
    return ErrorCode::ARG_ERR;
  }

  const uint32_t irqn_enabled = NVIC_GetEnableIRQ(res_.irqn);
  NVIC_DisableIRQ(res_.irqn);
  NVIC_ClearPendingIRQ(res_.irqn);

  DL_MCAN_enableIntr(res_.instance, MSPM0_CAN_INTR_MASK, false);
  DL_MCAN_enableIntrLine(res_.instance, DL_MCAN_INTR_LINE_NUM_0, false);
  DL_MCAN_enableIntrLine(res_.instance, DL_MCAN_INTR_LINE_NUM_1, false);
  DL_MCAN_disableInterrupt(res_.instance, MSPM0_CAN_MSP_LINE_MASK);

  ErrorCode ec = ErrorCode::FAILED;
  bool controller_normal = false;

  do
  {
    DL_MCAN_setOpMode(res_.instance, DL_MCAN_OPERATION_MODE_SW_INIT);

    uint32_t timeout = MSPM0_CAN_CONFIG_TIMEOUT;
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
    cccr = mspm0_can_update_field(cccr, MCAN_CCCR_CCE_MASK, MCAN_CCCR_CCE_OFS, 1U);
    core->MCAN_CCCR = cccr;

    timeout = MSPM0_CAN_CONFIG_TIMEOUT;
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

    const bool internal_loopback = cfg.mode.loopback;
    const bool monitor_mode = cfg.mode.listen_only;
    cccr = monitor_mode ? (cccr | MCAN_CCCR_MON_MASK) : (cccr & ~MCAN_CCCR_MON_MASK);

    if (internal_loopback)
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
    new_timing.dataRatePrescalar = 0U;
    new_timing.dataTimeSeg1 = 0U;
    new_timing.dataTimeSeg2 = 0U;
    new_timing.dataSynchJumpWidth = 0U;

    ec = mspm0_can_status_to_error(DL_MCAN_setBitTime(res_.instance, &new_timing));
    if (ec != ErrorCode::OK)
    {
      break;
    }

    DL_MCAN_setOpMode(res_.instance, DL_MCAN_OPERATION_MODE_NORMAL);

    timeout = MSPM0_CAN_CONFIG_TIMEOUT;
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

    uint32_t timeout = MSPM0_CAN_CONFIG_TIMEOUT;
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
                            DL_MCAN_INTR_SRC_MCAN_LINE_0);
    DL_MCAN_clearIntrStatus(res_.instance, DL_MCAN_INTR_MASK_ALL,
                            DL_MCAN_INTR_SRC_MCAN_LINE_1);
    DL_MCAN_clearInterruptStatus(res_.instance, MSPM0_CAN_MSP_LINE_MASK);

    DL_MCAN_enableIntr(res_.instance, MSPM0_CAN_INTR_MASK, true);
    DL_MCAN_enableIntrLine(res_.instance, DL_MCAN_INTR_LINE_NUM_0, true);
    DL_MCAN_enableIntrLine(res_.instance, DL_MCAN_INTR_LINE_NUM_1, true);
    DL_MCAN_enableInterrupt(res_.instance, MSPM0_CAN_MSP_LINE_MASK);
  }

  NVIC_ClearPendingIRQ(res_.irqn);
  if (irqn_enabled != 0U)
  {
    NVIC_EnableIRQ(res_.irqn);
  }

  return ec;
}

uint32_t MSPM0CAN::GetClockFreq() const { return ResolveClockFreq(res_.instance); }

uint32_t MSPM0CAN::ResolveClockFreq(MCAN_Regs* instance)
{
  if (instance == nullptr)
  {
    return 0U;
  }

  return mspm0_can_mcan_fclk_hz(instance);
}

ErrorCode MSPM0CAN::SendImmediate(const ClassicPack& pack)
{
  DL_MCAN_TxBufElement tx_elem = {};
  mspm0_can_pack_to_tx_elem(pack, tx_elem);

  if (mspm0_can_tx_fifo_size(res_.instance) != 0U)
  {
    DL_MCAN_TxFIFOStatus tx_status = {};
    DL_MCAN_getTxFIFOQueStatus(res_.instance, &tx_status);

    if (tx_status.freeLvl == 0U)
    {
      return ErrorCode::BUSY;
    }

    DL_MCAN_writeMsgRam(res_.instance, DL_MCAN_MEM_TYPE_FIFO, 0U, &tx_elem);

    const uint32_t tx_index = tx_status.putIdx;
    const int32_t tx_intr_ans =
        DL_MCAN_TXBufTransIntrEnable(res_.instance, tx_index, true);
    ASSERT(tx_intr_ans == 0);
    UNUSED(tx_intr_ans);
    return mspm0_can_status_to_error(DL_MCAN_TXBufAddReq(res_.instance, tx_index));
  }

  const uint32_t dedicated_buf_count = mspm0_can_tx_dedicated_buf_count(res_.instance);
  if (dedicated_buf_count == 0U)
  {
    return ErrorCode::NOT_SUPPORT;
  }

  const uint32_t req_pending = DL_MCAN_getTxBufReqPend(res_.instance);
  uint32_t tx_index = 0U;
  bool found_free_buf = false;
  for (; tx_index < dedicated_buf_count; ++tx_index)
  {
    if ((req_pending & (1UL << tx_index)) == 0U)
    {
      found_free_buf = true;
      break;
    }
  }

  if (!found_free_buf)
  {
    return ErrorCode::BUSY;
  }

  DL_MCAN_writeMsgRam(res_.instance, DL_MCAN_MEM_TYPE_BUF, tx_index, &tx_elem);
  const int32_t tx_intr_ans = DL_MCAN_TXBufTransIntrEnable(res_.instance, tx_index, true);
  ASSERT(tx_intr_ans == 0);
  UNUSED(tx_intr_ans);

  return mspm0_can_status_to_error(DL_MCAN_TXBufAddReq(res_.instance, tx_index));
}

ErrorCode MSPM0CAN::AddMessage(const ClassicPack& pack)
{
  if (pack.type == Type::ERROR)
  {
    return ErrorCode::ARG_ERR;
  }

  if (tx_pool_.Put(pack) != ErrorCode::OK)
  {
    return ErrorCode::FULL;
  }

  ProcessTxInterrupt();
  return ErrorCode::OK;
}

ErrorCode MSPM0CAN::GetErrorState(CAN::ErrorState& state) const
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

void MSPM0CAN::ProcessTxInterrupt()
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
      ClassicPack next_pack = {};
      if (tx_pool_.Get(next_pack) != ErrorCode::OK)
      {
        break;
      }

      const ErrorCode send_ans = SendImmediate(next_pack);
      if (send_ans != ErrorCode::OK)
      {
        (void)tx_pool_.Put(next_pack);
        break;
      }
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

void MSPM0CAN::ProcessErrorInterrupt(uint32_t intr_status)
{
  ClassicPack pack = {};
  pack.type = Type::ERROR;
  pack.dlc = 0U;

  DL_MCAN_ProtocolStatus protocol_status = {};
  DL_MCAN_getProtocolStatus(res_.instance, &protocol_status);

  if ((intr_status & MSPM0_CAN_ERROR_INTR_MASK) != 0U)
  {
    pack.id = CAN::FromErrorID(mspm0_can_error_id_from_status(protocol_status));
  }
  else
  {
    pack.id = CAN::FromErrorID(CAN::ErrorID::CAN_ERROR_ID_GENERIC);
  }

  OnMessage(pack, true);
}

void MSPM0CAN::ProcessRxFIFO(uint32_t fifo_num)
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

    const uint32_t payload_len = mspm0_can_dlc_to_len(rx_elem.dlc);
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

void MSPM0CAN::HandleMcanLineInterrupt(DL_MCAN_INTR_SRC_MCAN line)
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

  if ((intr_status & MSPM0_CAN_ERROR_INTR_MASK) != 0U)
  {
    ProcessErrorInterrupt(intr_status);
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

void MSPM0CAN::HandleInterrupt()
{
  for (uint32_t round = 0; round < 32U; ++round)
  {
    const uint32_t msp_status =
        DL_MCAN_getRawInterruptStatus(res_.instance, MSPM0_CAN_MSP_LINE_MASK);
    g_mspm0_can_irq_last_ris = msp_status;
    g_mspm0_can_irq_last_mis =
        DL_MCAN_getEnabledInterruptStatus(res_.instance, MSPM0_CAN_MSP_LINE_MASK);
    g_mspm0_can_irq_last_iidx = 0U;

    if (msp_status == 0U)
    {
      return;
    }

    if ((msp_status & DL_MCAN_MSP_INTERRUPT_LINE0) != 0U)
    {
      DL_MCAN_clearInterruptStatus(res_.instance, DL_MCAN_MSP_INTERRUPT_LINE0);
      HandleMcanLineInterrupt(DL_MCAN_INTR_SRC_MCAN_LINE_0);
    }

    if ((msp_status & DL_MCAN_MSP_INTERRUPT_LINE1) != 0U)
    {
      g_mspm0_can_irq_line1_count++;
      DL_MCAN_clearInterruptStatus(res_.instance, DL_MCAN_MSP_INTERRUPT_LINE1);
      HandleMcanLineInterrupt(DL_MCAN_INTR_SRC_MCAN_LINE_1);
    }

    if ((msp_status & ~MSPM0_CAN_MSP_LINE_MASK) != 0U)
    {
      g_mspm0_can_irq_unexpected_iidx_count++;
    }
  }
}

void MSPM0CAN::OnInterrupt(uint8_t index)
{
  if (index >= MAX_CAN_INSTANCES)
  {
    return;
  }

  MSPM0CAN* can = instance_map_[index];
  if (can == nullptr)
  {
    return;
  }

  can->HandleInterrupt();
}

#endif
