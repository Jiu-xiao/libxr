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

constexpr uint32_t MSPM0_SYSOSC_FREQ_4M_HZ = 4000000U;
constexpr uint32_t MSPM0_SYSOSC_FREQ_16M_HZ = 16000000U;
constexpr uint32_t MSPM0_SYSOSC_FREQ_24M_HZ = 24000000U;
constexpr uint32_t MSPM0_SYSOSC_FREQ_32M_HZ = 32000000U;
constexpr uint32_t MSPM0_SAMPLE_POINT_SCALE = 1000U;
constexpr uint32_t MSPM0_SAMPLE_POINT_TOLERANCE = 2U;

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
  constexpr uint8_t LENGTH_TABLE[16] = {0, 1, 2, 3, 4, 5, 6, 7,
                                        8, 12, 16, 20, 24, 32, 48, 64};
  return LENGTH_TABLE[(dlc < 16U) ? dlc : 15U];
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

  const uint32_t qdiv =
      ((SYSCTL->SOCLOCK.SYSPLLCFG1 & SYSCTL_SYSPLLCFG1_QDIV_MASK) >>
       SYSCTL_SYSPLLCFG1_QDIV_OFS) +
      1U;
  const uint32_t rdiv_clk1 =
      ((cfg0 & SYSCTL_SYSPLLCFG0_RDIVCLK1_MASK) >> SYSCTL_SYSPLLCFG0_RDIVCLK1_OFS) + 1U;
  const uint32_t clk1_div = 2U * rdiv_clk1;
  const uint64_t vco_hz = (static_cast<uint64_t>(ref_hz) * static_cast<uint64_t>(qdiv)) /
                          static_cast<uint64_t>(pdiv);

  const uint32_t clk1_hz = static_cast<uint32_t>(vco_hz / static_cast<uint64_t>(clk1_div));
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

const MCAN_MCAN_Regs* mspm0_can_core(const MCAN_Regs* instance)
{
  return &instance->MCANSS.MCAN;
}

uint32_t mspm0_can_mcan_fclk_hz(MCAN_Regs* instance)
{
  const uint32_t canclksrc_raw = SYSCTL->SOCLOCK.GENCLKCFG & SYSCTL_GENCLKCFG_CANCLKSRC_MASK;
  const uint32_t clkdiv_raw = instance->MCANSS.TI_WRAPPER.MSP.MCANSS_CLKDIV & MCAN_CLKDIV_RATIO_MASK;
  g_mspm0_can_clk_dbg_canclksrc_raw = canclksrc_raw;
  g_mspm0_can_clk_dbg_clkdiv_raw = clkdiv_raw;

  uint32_t src_hz = 0U;
  switch (canclksrc_raw)
  {
    case SYSCTL_GENCLKCFG_CANCLKSRC_SYSPLLOUT1:
    {
      const uint32_t cfg0 = SYSCTL->SOCLOCK.SYSPLLCFG0;
      const uint32_t pdiv = mspm0_can_syspll_pdiv_value();
      const uint32_t qdiv =
          ((SYSCTL->SOCLOCK.SYSPLLCFG1 & SYSCTL_SYSPLLCFG1_QDIV_MASK) >>
           SYSCTL_SYSPLLCFG1_QDIV_OFS) +
          1U;
      const uint32_t rdiv_clk1 =
          ((cfg0 & SYSCTL_SYSPLLCFG0_RDIVCLK1_MASK) >> SYSCTL_SYSPLLCFG0_RDIVCLK1_OFS) + 1U;
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

  const uint32_t divider = mspm0_can_fclk_divider_hz(static_cast<DL_MCAN_FCLK_DIV>(clkdiv_raw));
  if ((src_hz == 0U) || (divider == 0U))
  {
    g_mspm0_can_clk_dbg_fclk_hz = 0U;
    return 0U;
  }

  const uint32_t fclk_hz = src_hz / divider;
  g_mspm0_can_clk_dbg_fclk_hz = fclk_hz;
  return fclk_hz;
}

bool mspm0_can_is_loopback_enabled(const MCAN_Regs* instance)
{
  const MCAN_MCAN_Regs* core = mspm0_can_core(instance);
  return ((core->MCAN_CCCR & MCAN_CCCR_TEST_MASK) != 0U) &&
         ((core->MCAN_TEST & MCAN_TEST_LBCK_MASK) != 0U);
}

uint32_t mspm0_can_sample_point_permille(uint32_t tseg1, uint32_t tseg2)
{
  const uint32_t tq_num = 1U + tseg1 + tseg2;
  if (tq_num == 0U)
  {
    return 0U;
  }

  return (((1U + tseg1) * MSPM0_SAMPLE_POINT_SCALE) + (tq_num / 2U)) / tq_num;
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

  if (cfg.mode.triple_sampling)
  {
    return ErrorCode::NOT_SUPPORT;
  }

  const bool loopback_enabled = mspm0_can_is_loopback_enabled(res_.instance);
  const MCAN_MCAN_Regs* core = mspm0_can_core(res_.instance);
  const bool listen_only_enabled = ((core->MCAN_CCCR & MCAN_CCCR_MON_MASK) != 0U);
  const bool one_shot_enabled = ((core->MCAN_CCCR & MCAN_CCCR_DAR_MASK) != 0U);

  if ((cfg.mode.loopback != loopback_enabled) ||
      (cfg.mode.listen_only != listen_only_enabled) ||
      (cfg.mode.one_shot != one_shot_enabled))
  {
    return ErrorCode::CHECK_ERR;
  }

  const uint32_t fclk_hz = GetClockFreq();
  if (fclk_hz == 0U)
  {
    return ErrorCode::NOT_SUPPORT;
  }

  DL_MCAN_BitTimingParams hw_timing = {};
  DL_MCAN_getBitTime(res_.instance, &hw_timing);

  const uint32_t actual_brp = hw_timing.nomRatePrescalar + 1U;
  const uint32_t actual_tseg1 = hw_timing.nomTimeSeg1 + 1U;
  const uint32_t actual_tseg2 = hw_timing.nomTimeSeg2 + 1U;
  const uint32_t actual_sjw = hw_timing.nomSynchJumpWidth + 1U;
  const uint32_t actual_tq_num = 1U + actual_tseg1 + actual_tseg2;
  const uint32_t actual_bitrate = fclk_hz / (actual_brp * actual_tq_num);
  const uint32_t actual_sample_point =
      mspm0_can_sample_point_permille(actual_tseg1, actual_tseg2);

  if ((cfg.bitrate != 0U) && (cfg.bitrate != actual_bitrate))
  {
    return ErrorCode::CHECK_ERR;
  }

  if (cfg.sample_point > 0.0f)
  {
    const uint32_t requested_sample_point =
        static_cast<uint32_t>(cfg.sample_point * static_cast<float>(MSPM0_SAMPLE_POINT_SCALE) +
                              0.5f);
    const uint32_t delta = (requested_sample_point > actual_sample_point)
                               ? (requested_sample_point - actual_sample_point)
                               : (actual_sample_point - requested_sample_point);
    if (delta > MSPM0_SAMPLE_POINT_TOLERANCE)
    {
      return ErrorCode::CHECK_ERR;
    }
  }

  if ((cfg.bit_timing.brp != 0U) && (cfg.bit_timing.brp != actual_brp))
  {
    return ErrorCode::CHECK_ERR;
  }

  const uint32_t requested_tseg1 = cfg.bit_timing.prop_seg + cfg.bit_timing.phase_seg1;
  if ((requested_tseg1 != 0U) && (requested_tseg1 != actual_tseg1))
  {
    return ErrorCode::CHECK_ERR;
  }

  if ((cfg.bit_timing.phase_seg2 != 0U) && (cfg.bit_timing.phase_seg2 != actual_tseg2))
  {
    return ErrorCode::CHECK_ERR;
  }

  if ((cfg.bit_timing.sjw != 0U) && (cfg.bit_timing.sjw != actual_sjw))
  {
    return ErrorCode::CHECK_ERR;
  }

  return ErrorCode::OK;
}

uint32_t MSPM0CAN::GetClockFreq() const
{
  if (res_.instance == nullptr)
  {
    return 0U;
  }

  return mspm0_can_mcan_fclk_hz(res_.instance);
}

ErrorCode MSPM0CAN::TrySendImmediate(const ClassicPack& pack)
{
  DL_MCAN_TxFIFOStatus tx_status = {};
  DL_MCAN_getTxFIFOQueStatus(res_.instance, &tx_status);

  if (tx_status.freeLvl == 0U)
  {
    return ErrorCode::BUSY;
  }

  DL_MCAN_TxBufElement tx_elem = {};
  mspm0_can_pack_to_tx_elem(pack, tx_elem);

  const uint32_t TX_INDEX = tx_status.putIdx;
  DL_MCAN_writeMsgRam(res_.instance, DL_MCAN_MEM_TYPE_FIFO, TX_INDEX, &tx_elem);

  return mspm0_can_status_to_error(DL_MCAN_TXBufAddReq(res_.instance, TX_INDEX));
}

ErrorCode MSPM0CAN::AddMessage(const ClassicPack& pack)
{
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();

  const ErrorCode SEND_ANS = TrySendImmediate(pack);
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
  DL_MCAN_TxFIFOStatus tx_status = {};

  while (true)
  {
    DL_MCAN_getTxFIFOQueStatus(res_.instance, &tx_status);
    if (tx_status.freeLvl == 0U)
    {
      return;
    }

    ClassicPack next_pack = {};
    if (tx_pool_.Get(next_pack) != ErrorCode::OK)
    {
      return;
    }

    const ErrorCode SEND_ANS = TrySendImmediate(next_pack);
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
    DL_MCAN_readMsgRam(res_.instance, DL_MCAN_MEM_TYPE_FIFO, 0U, fifo_num,
                       &rx_elem);

    const int32_t ACK_ANS = DL_MCAN_writeRxFIFOAck(res_.instance, fifo_num, fifo_status.getIdx);
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
