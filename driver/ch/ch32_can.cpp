#include "ch32_can.hpp"

using namespace LibXR;

CH32CAN* CH32CAN::map[CH32_CAN_NUMBER] = {nullptr};

static inline uint8_t ch32_can_mode_macro(const CAN::Mode& m)
{
  if (m.loopback && m.listen_only)
  {
    return CAN_Mode_Silent_LoopBack;
  }
  if (m.loopback)
  {
    return CAN_Mode_LoopBack;
  }
  if (m.listen_only)
  {
    return CAN_Mode_Silent;
  }
  return CAN_Mode_Normal;
}

/**
 * @brief Enable NVIC vectors for a given CAN instance.
 *
 * Important:
 * - CAN1 TX/RX0 may be named as USB_HP_CAN1_TX_IRQn / USB_LP_CAN1_RX0_IRQn in WCH
 * headers.
 * - RX vector selection should follow fifo_ (FIFO0 -> RX0, FIFO1 -> RX1).
 */
static inline void CH32_CAN_EnableNVIC(ch32_can_id_t id, uint8_t fifo)
{
  // ===== TX =====
  switch (id)
  {
#if defined(CAN1)
    case CH32_CAN1:
      NVIC_EnableIRQ(USB_HP_CAN1_TX_IRQn);
      break;
#endif

#if defined(CAN2)
    case CH32_CAN2:
      NVIC_EnableIRQ(CAN2_TX_IRQn);
      break;
#endif
    default:
      break;
  }

  // ===== RX (FIFO0 -> RX0 vector, FIFO1 -> RX1 vector) =====
  if (fifo == 0u)
  {
    switch (id)
    {
#if defined(CAN1)
      case CH32_CAN1:
        NVIC_EnableIRQ(USB_LP_CAN1_RX0_IRQn);
        break;
#endif

#if defined(CAN2)
      case CH32_CAN2:
        NVIC_EnableIRQ(CAN2_RX0_IRQn);
        break;
#endif
      default:
        break;
    }
  }
  else
  {
    switch (id)
    {
#if defined(CAN1)
      case CH32_CAN1:
        NVIC_EnableIRQ(CAN1_RX1_IRQn);
        break;
#endif

#if defined(CAN2)
      case CH32_CAN2:
        NVIC_EnableIRQ(CAN2_RX1_IRQn);
        break;
#endif

      default:
        break;
    }
  }

  // ===== SCE =====
  switch (id)
  {
#if defined(CAN1)
    case CH32_CAN1:
      NVIC_EnableIRQ(CAN1_SCE_IRQn);
      break;
#endif

#if defined(CAN2)
    case CH32_CAN2:
      NVIC_EnableIRQ(CAN2_SCE_IRQn);
      break;
#endif

    default:
      break;
  }
}

CH32CAN::CH32CAN(ch32_can_id_t id, uint32_t pool_size)
    : CAN(), instance_(CH32_CAN_GetInstanceID(id)), id_(id), tx_pool_(pool_size)
{
#if defined(CAN2)
  if (id == CH32_CAN1)
  {
    filter_bank_ = 0;
    fifo_ = CAN_FilterFIFO0;  // 0
  }
  else
  {
    filter_bank_ = 14;
    fifo_ = CAN_FilterFIFO1;  // 1
  }
#else
  filter_bank_ = 0;
  fifo_ = CAN_FilterFIFO0;  // 0
#endif

  ASSERT(id_ < CH32_CAN_NUMBER);
  ASSERT(instance_ != nullptr);

  map[id_] = this;

  // Enable peripheral clock
  RCC_APB1PeriphClockCmd(CH32_CAN_RCC_PERIPH_MAP[id_], ENABLE);

  // Keep CAN in initialization mode until user calls SetConfig().
  (void)CAN_OperatingModeRequest(instance_, CAN_OperatingMode_Initialization);

#if defined(CAN2)
  // If dual CAN exists and filters are shared, set a typical split point.
  CAN_SlaveStartBank(CH32_CAN_DEFAULT_SLAVE_START_BANK);
#endif

  (void)Init();
}

ErrorCode CH32CAN::Init()
{
  if (instance_ == nullptr)
  {
    return ErrorCode::ARG_ERR;
  }

  // ===== Accept-all filter (ID Mask, all zeros) =====
  CAN_FilterInitTypeDef f = {};
  f.CAN_FilterIdHigh = 0u;
  f.CAN_FilterIdLow = 0u;
  f.CAN_FilterMaskIdHigh = 0u;
  f.CAN_FilterMaskIdLow = 0u;
  f.CAN_FilterFIFOAssignment = fifo_;
  f.CAN_FilterNumber = filter_bank_;
  f.CAN_FilterMode = CAN_FilterMode_IdMask;
  f.CAN_FilterScale = CAN_FilterScale_32bit;
  f.CAN_FilterActivation = ENABLE;

  CAN_FilterInit(&f);

  EnableIRQs();

  // Enable NVIC for this CAN instance (handles WCH alias IRQ names correctly)
  CH32_CAN_EnableNVIC(id_, fifo_);

  return ErrorCode::OK;
}

void CH32CAN::DisableIRQs()
{
  if (instance_ == nullptr) return;

  uint32_t it = 0u;

#ifdef CAN_IT_FMP0
  if (fifo_ == 0u) it |= CAN_IT_FMP0;
#endif
#ifdef CAN_IT_FMP1
  if (fifo_ == 1u) it |= CAN_IT_FMP1;
#endif

#ifdef CAN_IT_TME
  it |= CAN_IT_TME;
#endif

#ifdef CAN_IT_ERR
  it |= CAN_IT_ERR;
#endif
#ifdef CAN_IT_BOF
  it |= CAN_IT_BOF;
#endif
#ifdef CAN_IT_EPV
  it |= CAN_IT_EPV;
#endif
#ifdef CAN_IT_EWG
  it |= CAN_IT_EWG;
#endif
#ifdef CAN_IT_LEC
  it |= CAN_IT_LEC;
#endif

  if (it != 0u)
  {
    CAN_ITConfig(instance_, it, DISABLE);
  }
}

void CH32CAN::EnableIRQs()
{
  if (instance_ == nullptr) return;

  uint32_t it = 0u;

#ifdef CAN_IT_FMP0
  if (fifo_ == 0u) it |= CAN_IT_FMP0;
#endif
#ifdef CAN_IT_FMP1
  if (fifo_ == 1u) it |= CAN_IT_FMP1;
#endif

#ifdef CAN_IT_TME
  it |= CAN_IT_TME;
#endif

#ifdef CAN_IT_ERR
  it |= CAN_IT_ERR;
#endif
#ifdef CAN_IT_BOF
  it |= CAN_IT_BOF;
#endif
#ifdef CAN_IT_EPV
  it |= CAN_IT_EPV;
#endif
#ifdef CAN_IT_EWG
  it |= CAN_IT_EWG;
#endif
#ifdef CAN_IT_LEC
  it |= CAN_IT_LEC;
#endif

  if (it != 0u)
  {
    // WCH StdPeriph examples: clear pending bits before enabling interrupts
    // to avoid spurious entry and ensure proper ACK in some variants.
#ifdef CAN_IT_TME
    if ((it & CAN_IT_TME) != 0u)
    {
      CAN_ClearITPendingBit(instance_, CAN_IT_TME);
    }
#endif

#ifdef CAN_IT_FMP0
    if (fifo_ == 0u && ((it & CAN_IT_FMP0) != 0u))
    {
      CAN_ClearITPendingBit(instance_, CAN_IT_FMP0);
    }
#endif
#ifdef CAN_IT_FMP1
    if (fifo_ == 1u && ((it & CAN_IT_FMP1) != 0u))
    {
      CAN_ClearITPendingBit(instance_, CAN_IT_FMP1);
    }
#endif

    CAN_ITConfig(instance_, it, ENABLE);
  }
}

static inline bool is_nonzero_bit_timing(const CAN::BitTiming& bt)
{
  return (bt.brp != 0u) || (bt.prop_seg != 0u) || (bt.phase_seg1 != 0u) ||
         (bt.phase_seg2 != 0u) || (bt.sjw != 0u);
}

static inline bool fill_keep_zero_from_cache(CAN::BitTiming& dst,
                                             const CAN::BitTiming& cache)
{
  auto keep = [&](uint32_t& field, uint32_t cached) -> bool
  {
    if (field == 0u)
    {
      if (cached == 0u) return false;
      field = cached;
    }
    return true;
  };

  bool ok = true;
  ok &= keep(dst.brp, cache.brp);
  ok &= keep(dst.prop_seg, cache.prop_seg);
  ok &= keep(dst.phase_seg1, cache.phase_seg1);
  ok &= keep(dst.phase_seg2, cache.phase_seg2);
  ok &= keep(dst.sjw, cache.sjw);
  return ok;
}

ErrorCode CH32CAN::SetConfig(const CAN::Configuration& cfg_in)
{
  if (instance_ == nullptr)
  {
    return ErrorCode::ARG_ERR;
  }

  // RAII: ensure IRQs are always restored even if we early-return
  struct IrqGuard
  {
    CH32CAN* self;
    explicit IrqGuard(CH32CAN* s) : self(s) { self->DisableIRQs(); }
    ~IrqGuard() { self->EnableIRQs(); }
  } guard(this);

  ErrorCode ec = ErrorCode::OK;
  bool entered_init = false;

  // Build an "effective" timing: support "0 means keep last applied".
  CAN::Configuration cfg = cfg_in;

  // Normalize bit timing (fill zeros from cache)
  if (!is_nonzero_bit_timing(cfg.bit_timing))
  {
    if (!fill_keep_zero_from_cache(cfg.bit_timing, cfg_cache_.bit_timing))
    {
      return ErrorCode::ARG_ERR;
    }
  }
  else
  {
    if (!fill_keep_zero_from_cache(cfg.bit_timing, cfg_cache_.bit_timing))
    {
      return ErrorCode::ARG_ERR;
    }
  }

  // ===== Validate timing (bxCAN constraints) =====
  const CAN::BitTiming& bt = cfg.bit_timing;

  // brp: 1..1024
  if (bt.brp < 1u || bt.brp > 1024u)
  {
    ASSERT(false);
    return ErrorCode::ARG_ERR;
  }

  // BS1 = prop + phase1: 1..16
  const uint32_t bs1 = bt.prop_seg + bt.phase_seg1;
  if (bs1 < 1u || bs1 > 16u)
  {
    ASSERT(false);
    return ErrorCode::ARG_ERR;
  }

  // BS2: 1..8
  if (bt.phase_seg2 < 1u || bt.phase_seg2 > 8u)
  {
    ASSERT(false);
    return ErrorCode::ARG_ERR;
  }

  // SJW: 1..4 and SJW <= BS2
  if (bt.sjw < 1u || bt.sjw > 4u || bt.sjw > bt.phase_seg2)
  {
    ASSERT(false);
    return ErrorCode::ARG_ERR;
  }

  // ===== Enter initialization mode =====
  if (CAN_OperatingModeRequest(instance_, CAN_OperatingMode_Initialization) ==
      CAN_ModeStatus_Failed)
  {
    return ErrorCode::FAILED;
  }
  entered_init = true;

  // ===== Apply init struct =====
  CAN_InitTypeDef init;
  std::memset(&init, 0, sizeof(init));
  CAN_StructInit(&init);

  init.CAN_Prescaler = static_cast<uint16_t>(bt.brp);
  init.CAN_Mode = ch32_can_mode_macro(cfg.mode);

  init.CAN_SJW = static_cast<uint8_t>(bt.sjw - 1u);
  init.CAN_BS1 = static_cast<uint8_t>(bs1 - 1u);
  init.CAN_BS2 = static_cast<uint8_t>(bt.phase_seg2 - 1u);

  // Mode knobs (best-effort mapping)
  init.CAN_NART = cfg.mode.one_shot ? ENABLE : DISABLE;

  // Reasonable defaults
  init.CAN_TTCM = DISABLE;
  init.CAN_ABOM = ENABLE;   // auto bus-off management
  init.CAN_AWUM = DISABLE;  // auto wake-up
  init.CAN_RFLM = DISABLE;  // FIFO not locked
  init.CAN_TXFP = ENABLE;   // prioritize by TX request order

  if (CAN_Init(instance_, &init) != CAN_InitStatus_Success)
  {
    ec = ErrorCode::FAILED;
  }
  else
  {
    if (CAN_OperatingModeRequest(instance_, CAN_OperatingMode_Normal) ==
        CAN_ModeStatus_Failed)
    {
      ec = ErrorCode::FAILED;
    }
    else
    {
      // Update cache only when success
      cfg_cache_ = cfg;
      ec = ErrorCode::OK;
    }
  }

  // Best-effort: if we entered init but failed later, try to return to normal
  if (ec != ErrorCode::OK && entered_init)
  {
    (void)CAN_OperatingModeRequest(instance_, CAN_OperatingMode_Normal);
  }

  return ec;
}

uint32_t CH32CAN::GetClockFreq() const
{
  RCC_ClocksTypeDef clocks{};
  RCC_GetClocksFreq(&clocks);
  return clocks.PCLK1_Frequency;
}

inline void CH32CAN::BuildTxMsg(const ClassicPack& p, CanTxMsg& m)
{
  const bool is_ext = (p.type == Type::EXTENDED) || (p.type == Type::REMOTE_EXTENDED);
  const bool is_rtr =
      (p.type == Type::REMOTE_STANDARD) || (p.type == Type::REMOTE_EXTENDED);

  m.DLC = (p.dlc <= 8u) ? static_cast<uint8_t>(p.dlc) : 8u;
  m.IDE = is_ext ? CAN_ID_EXT : CAN_ID_STD;
  m.RTR = is_rtr ? CAN_RTR_REMOTE : CAN_RTR_DATA;

  m.StdId = is_ext ? 0u : (p.id & 0x7FFu);
  m.ExtId = is_ext ? (p.id & 0x1FFFFFFFu) : 0u;

  std::memcpy(m.Data, p.data, 8);
}

void CH32CAN::TxService()
{
  if (instance_ == nullptr) return;

  tx_pend_.store(1u, std::memory_order_release);

  uint32_t expected = 0u;
  if (!tx_lock_.compare_exchange_strong(expected, 1u, std::memory_order_acquire,
                                        std::memory_order_relaxed))
  {
    return;
  }

  for (;;)
  {
    tx_pend_.store(0u, std::memory_order_release);

    for (;;)
    {
      ClassicPack p{};
      if (tx_pool_.Get(p) != ErrorCode::OK)
      {
        break;
      }

      BuildTxMsg(p, tx_msg_);

      uint8_t mb = CAN_Transmit(instance_, &tx_msg_);
      if (mb == CAN_TxStatus_NoMailBox)
      {
        (void)tx_pool_.Put(p);
        break;
      }
    }

    tx_lock_.store(0u, std::memory_order_release);

    if (tx_pend_.load(std::memory_order_acquire) == 0u)
    {
      return;
    }

    expected = 0u;
    if (!tx_lock_.compare_exchange_strong(expected, 1u, std::memory_order_acquire,
                                          std::memory_order_relaxed))
    {
      return;
    }
  }
}

ErrorCode CH32CAN::AddMessage(const ClassicPack& pack)
{
  if (pack.type == Type::ERROR)
  {
    return ErrorCode::ARG_ERR;
  }

  if (tx_pool_.Put(pack) != ErrorCode::OK)
  {
    return ErrorCode::FULL;
  }

  TxService();
  return ErrorCode::OK;
}

void CH32CAN::ProcessTxInterrupt()
{
#ifdef CAN_IT_TME
  if (instance_ == nullptr) return;

  if (CAN_GetITStatus(instance_, CAN_IT_TME) != RESET)
  {
    CAN_ClearITPendingBit(instance_, CAN_IT_TME);
    TxService();
  }
#else
  TxService();
#endif
}

void CH32CAN::ProcessRxInterrupt()
{
  if (instance_ == nullptr) return;

  // NOTE:
  // WCH examples clear FMP pending in RX ISR.
  // We keep the "drain FIFO" behavior and ACK pending at the end.
  while (CAN_MessagePending(instance_, fifo_) != 0u)
  {
    CAN_Receive(instance_, fifo_, &rx_msg_);

    ClassicPack p{};
    if (rx_msg_.IDE == CAN_ID_STD)
    {
      p.id = rx_msg_.StdId;
      p.type = Type::STANDARD;
    }
    else
    {
      p.id = rx_msg_.ExtId;
      p.type = Type::EXTENDED;
    }

    if (rx_msg_.RTR == CAN_RTR_REMOTE)
    {
      p.type = (p.type == Type::STANDARD) ? Type::REMOTE_STANDARD : Type::REMOTE_EXTENDED;
    }

    p.dlc = (rx_msg_.DLC <= 8u) ? rx_msg_.DLC : 8u;
    std::memcpy(p.data, rx_msg_.Data, 8);

    OnMessage(p, true);
  }

  // ACK RX pending (WCH StdPeriph style)
#ifdef CAN_IT_FMP0
  if (fifo_ == 0u)
  {
    if (CAN_GetITStatus(instance_, CAN_IT_FMP0) != RESET)
    {
      CAN_ClearITPendingBit(instance_, CAN_IT_FMP0);
    }
  }
#endif
#ifdef CAN_IT_FMP1
  if (fifo_ == 1u)
  {
    if (CAN_GetITStatus(instance_, CAN_IT_FMP1) != RESET)
    {
      CAN_ClearITPendingBit(instance_, CAN_IT_FMP1);
    }
  }
#endif
}

void CH32CAN::ProcessErrorInterrupt()
{
  if (instance_ == nullptr) return;

  // NOTE (Fix #2):
  // WCH CAN_ClearITPendingBit(CAN_IT_ERR/CAN_IT_LEC) clears ERRSR.
  // Therefore snapshot flags + LEC BEFORE clearing any pending bits.
  const bool bof = (CAN_GetFlagStatus(instance_, CAN_FLAG_BOF) != RESET);
  const bool epv = (CAN_GetFlagStatus(instance_, CAN_FLAG_EPV) != RESET);
  const bool ewg = (CAN_GetFlagStatus(instance_, CAN_FLAG_EWG) != RESET);
  const uint8_t lec = CAN_GetLastErrorCode(instance_);

#ifdef CAN_IT_LEC
  if (CAN_GetITStatus(instance_, CAN_IT_LEC) != RESET)
  {
    CAN_ClearITPendingBit(instance_, CAN_IT_LEC);
  }
#endif
#ifdef CAN_IT_ERR
  if (CAN_GetITStatus(instance_, CAN_IT_ERR) != RESET)
  {
    CAN_ClearITPendingBit(instance_, CAN_IT_ERR);
  }
#endif
#ifdef CAN_IT_BOF
  if (CAN_GetITStatus(instance_, CAN_IT_BOF) != RESET)
  {
    CAN_ClearITPendingBit(instance_, CAN_IT_BOF);
  }
#endif
#ifdef CAN_IT_EPV
  if (CAN_GetITStatus(instance_, CAN_IT_EPV) != RESET)
  {
    CAN_ClearITPendingBit(instance_, CAN_IT_EPV);
  }
#endif
#ifdef CAN_IT_EWG
  if (CAN_GetITStatus(instance_, CAN_IT_EWG) != RESET)
  {
    CAN_ClearITPendingBit(instance_, CAN_IT_EWG);
  }
#endif

  ClassicPack p{};
  p.type = Type::ERROR;
  p.dlc = 0u;

  CAN::ErrorID eid = CAN::ErrorID::CAN_ERROR_ID_GENERIC;

  if (bof)
  {
    eid = CAN::ErrorID::CAN_ERROR_ID_BUS_OFF;
  }
  else if (epv)
  {
    eid = CAN::ErrorID::CAN_ERROR_ID_ERROR_PASSIVE;
  }
  else if (ewg)
  {
    eid = CAN::ErrorID::CAN_ERROR_ID_ERROR_WARNING;
  }
  else
  {
    switch (lec)
    {
      case CAN_ErrorCode_StuffErr:
        eid = CAN::ErrorID::CAN_ERROR_ID_STUFF;
        break;
      case CAN_ErrorCode_FormErr:
        eid = CAN::ErrorID::CAN_ERROR_ID_FORM;
        break;
      case CAN_ErrorCode_ACKErr:
        eid = CAN::ErrorID::CAN_ERROR_ID_ACK;
        break;
      case CAN_ErrorCode_BitRecessiveErr:
        eid = CAN::ErrorID::CAN_ERROR_ID_BIT1;
        break;
      case CAN_ErrorCode_BitDominantErr:
        eid = CAN::ErrorID::CAN_ERROR_ID_BIT0;
        break;
      case CAN_ErrorCode_CRCErr:
        eid = CAN::ErrorID::CAN_ERROR_ID_CRC;
        break;
      default:
        eid = CAN::ErrorID::CAN_ERROR_ID_OTHER;
        break;
    }
  }

  p.id = static_cast<uint32_t>(eid);
  OnMessage(p, true);
}

ErrorCode CH32CAN::GetErrorState(CAN::ErrorState& state) const
{
  if (instance_ == nullptr)
  {
    return ErrorCode::ARG_ERR;
  }

  state.rx_error_counter = CAN_GetReceiveErrorCounter(instance_);
  state.tx_error_counter = CAN_GetLSBTransmitErrorCounter(instance_);

  state.bus_off = (CAN_GetFlagStatus(instance_, CAN_FLAG_BOF) != RESET);
  state.error_passive = (CAN_GetFlagStatus(instance_, CAN_FLAG_EPV) != RESET);
  state.error_warning = (CAN_GetFlagStatus(instance_, CAN_FLAG_EWG) != RESET);

  return ErrorCode::OK;
}

#if defined(CAN1)
extern "C" void USB_HP_CAN1_TX_IRQHandler(void) __attribute__((interrupt));
extern "C" void USB_HP_CAN1_TX_IRQHandler(void)
{
  if (auto* can = LibXR::CH32CAN::map[CH32_CAN1])
  {
    can->ProcessTxInterrupt();
  }
}

extern "C" void USB_LP_CAN1_RX0_IRQHandler(void) __attribute__((interrupt));
extern "C" void USB_LP_CAN1_RX0_IRQHandler(void)
{
  if (auto* can = LibXR::CH32CAN::map[CH32_CAN1])
  {
    can->ProcessRxInterrupt();
  }
}

extern "C" void CAN1_TX_IRQHandler(void) __attribute__((interrupt));
extern "C" void CAN1_TX_IRQHandler(void)
{
  if (auto* can = LibXR::CH32CAN::map[CH32_CAN1])
  {
    can->ProcessTxInterrupt();
  }
}

extern "C" void CAN1_RX1_IRQHandler(void) __attribute__((interrupt));
extern "C" void CAN1_RX1_IRQHandler(void)
{
  if (auto* can = LibXR::CH32CAN::map[CH32_CAN1])
  {
    can->ProcessRxInterrupt();
  }
}

extern "C" void CAN1_SCE_IRQHandler(void) __attribute__((interrupt));
extern "C" void CAN1_SCE_IRQHandler(void)
{
  if (auto* can = LibXR::CH32CAN::map[CH32_CAN1])
  {
    can->ProcessErrorInterrupt();
  }
}
#endif

#if defined(CAN2)
extern "C" void CAN2_TX_IRQHandler(void) __attribute__((interrupt));
extern "C" void CAN2_TX_IRQHandler(void)
{
  if (auto* can = LibXR::CH32CAN::map[CH32_CAN2])
  {
    can->ProcessTxInterrupt();
  }
}

extern "C" void CAN2_RX0_IRQHandler(void) __attribute__((interrupt));
extern "C" void CAN2_RX0_IRQHandler(void)
{
  if (auto* can = LibXR::CH32CAN::map[CH32_CAN2])
  {
    can->ProcessRxInterrupt();
  }
}

extern "C" void CAN2_RX1_IRQHandler(void) __attribute__((interrupt));
extern "C" void CAN2_RX1_IRQHandler(void)
{
  if (auto* can = LibXR::CH32CAN::map[CH32_CAN2])
  {
    can->ProcessRxInterrupt();
  }
}

extern "C" void CAN2_SCE_IRQHandler(void) __attribute__((interrupt));
extern "C" void CAN2_SCE_IRQHandler(void)
{
  if (auto* can = LibXR::CH32CAN::map[CH32_CAN2])
  {
    can->ProcessErrorInterrupt();
  }
}
#endif
