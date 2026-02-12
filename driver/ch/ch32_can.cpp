#include "ch32_can.hpp"

#include "ch32_usbcan_shared.hpp"

using namespace LibXR;

CH32CAN* CH32CAN::map[CH32_CAN_NUMBER] = {nullptr};

#if defined(CAN1) && !defined(CAN2)
static void can1_rx0_thunk()
{
  if (auto* can = LibXR::CH32CAN::map[CH32_CAN1])
  {
    can->ProcessRxInterrupt();
  }
}

static void can1_tx_thunk()
{
  if (auto* can = LibXR::CH32CAN::map[CH32_CAN1])
  {
    can->ProcessTxInterrupt();
  }
}
#endif

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

static inline void ch32_can_enable_nvic(ch32_can_id_t id, uint8_t fifo)
{
  // TX interrupt line.
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

  // RX interrupt line: FIFO0 -> RX0 vector, FIFO1 -> RX1 vector.
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

  // SCE interrupt line.
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
  if constexpr (LibXR::CH32UsbCanShared::usb_can_share_enabled())
  {
#if defined(CAN1) && !defined(CAN2)
    const bool USB_ALREADY_INITED =
        LibXR::CH32UsbCanShared::usb_inited.load(std::memory_order_acquire);
    // On shared USB/CAN interrupt configurations, CAN1 must initialize before USB.
    ASSERT(USB_ALREADY_INITED == false);
#endif
  }

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

  // Enable peripheral clock.
  RCC_APB1PeriphClockCmd(CH32_CAN_RCC_PERIPH_MAP[id_], ENABLE);

  // Keep CAN in initialization mode until SetConfig() is called.
  (void)CAN_OperatingModeRequest(instance_, CAN_OperatingMode_Initialization);

#if defined(CAN2)
  // On dual-CAN variants, configure the default shared filter split point.
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

  // Default accept-all filter (ID-mask mode, all zeros).
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

  // Enable NVIC for this CAN instance.
  ch32_can_enable_nvic(id_, fifo_);

  if constexpr (LibXR::CH32UsbCanShared::usb_can_share_enabled())
  {
#if defined(CAN1) && !defined(CAN2)
    LibXR::CH32UsbCanShared::register_can1_rx0(&can1_rx0_thunk);
    LibXR::CH32UsbCanShared::register_can1_tx(&can1_tx_thunk);
    LibXR::CH32UsbCanShared::can1_inited.store(true, std::memory_order_release);
#endif
  }

  return ErrorCode::OK;
}

void CH32CAN::DisableIRQs()
{
  uint32_t it = 0u;

#ifdef CAN_IT_FMP0
  if (fifo_ == 0u)
  {
    it |= CAN_IT_FMP0;
  }
#endif
#ifdef CAN_IT_FMP1
  if (fifo_ == 1u)
  {
    it |= CAN_IT_FMP1;
  }
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
  uint32_t it = 0u;

#ifdef CAN_IT_FMP0
  if (fifo_ == 0u)
  {
    it |= CAN_IT_FMP0;
  }
#endif
#ifdef CAN_IT_FMP1
  if (fifo_ == 1u)
  {
    it |= CAN_IT_FMP1;
  }
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
    // Clear pending interrupt flags before enabling interrupt sources.
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

static inline bool fill_keep_zero_from_cache(CAN::BitTiming& dst,
                                             const CAN::BitTiming& cache)
{
  auto keep = [&](uint32_t& field, uint32_t cached) -> bool
  {
    if (field == 0u)
    {
      if (cached == 0u)
      {
        return false;
      }
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

  // Ensure IRQ state restoration on all return paths.
  struct IrqGuard
  {
    CH32CAN* self;
    explicit IrqGuard(CH32CAN* s) : self(s) { self->DisableIRQs(); }
    ~IrqGuard() { self->EnableIRQs(); }
  } guard(this);

  ErrorCode ec = ErrorCode::OK;
  bool entered_init = false;

  // Build effective timing values; zero fields reuse cached configuration.
  CAN::Configuration cfg = cfg_in;

  if (!fill_keep_zero_from_cache(cfg.bit_timing, cfg_cache_.bit_timing))
  {
    return ErrorCode::ARG_ERR;
  }

  // Validate bxCAN timing constraints.
  const CAN::BitTiming& bt = cfg.bit_timing;

  // BRP: 1..1024.
  if (bt.brp < 1u || bt.brp > 1024u)
  {
    ASSERT(false);
    return ErrorCode::ARG_ERR;
  }

  // BS1 = PROP_SEG + PHASE_SEG1: 1..16.
  const uint32_t BS1 = bt.prop_seg + bt.phase_seg1;
  if (BS1 < 1u || BS1 > 16u)
  {
    ASSERT(false);
    return ErrorCode::ARG_ERR;
  }

  // BS2: 1..8.
  if (bt.phase_seg2 < 1u || bt.phase_seg2 > 8u)
  {
    ASSERT(false);
    return ErrorCode::ARG_ERR;
  }

  // SJW: 1..4 and SJW <= BS2.
  if (bt.sjw < 1u || bt.sjw > 4u || bt.sjw > bt.phase_seg2)
  {
    ASSERT(false);
    return ErrorCode::ARG_ERR;
  }

  // Enter initialization mode.
  if (CAN_OperatingModeRequest(instance_, CAN_OperatingMode_Initialization) ==
      CAN_ModeStatus_Failed)
  {
    return ErrorCode::FAILED;
  }
  entered_init = true;

  // Apply initialization structure.
  CAN_InitTypeDef init;
  std::memset(&init, 0, sizeof(init));
  CAN_StructInit(&init);

  init.CAN_Prescaler = static_cast<uint16_t>(bt.brp);
  init.CAN_Mode = ch32_can_mode_macro(cfg.mode);

  init.CAN_SJW = static_cast<uint8_t>(bt.sjw - 1u);
  init.CAN_BS1 = static_cast<uint8_t>(BS1 - 1u);
  init.CAN_BS2 = static_cast<uint8_t>(bt.phase_seg2 - 1u);

  // Mode mapping.
  init.CAN_NART = cfg.mode.one_shot ? ENABLE : DISABLE;

  // Default controller options.
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
      // Update cache only on successful configuration.
      cfg_cache_ = cfg;
      ec = ErrorCode::OK;
    }
  }

  // If configuration fails after entering init mode, request normal mode.
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
  const bool IS_EXT = (p.type == Type::EXTENDED) || (p.type == Type::REMOTE_EXTENDED);
  const bool IS_RTR =
      (p.type == Type::REMOTE_STANDARD) || (p.type == Type::REMOTE_EXTENDED);

  m.DLC = (p.dlc <= 8u) ? static_cast<uint8_t>(p.dlc) : 8u;
  m.IDE = IS_EXT ? CAN_ID_EXT : CAN_ID_STD;
  m.RTR = IS_RTR ? CAN_RTR_REMOTE : CAN_RTR_DATA;

  m.StdId = IS_EXT ? 0u : (p.id & 0x7FFu);
  m.ExtId = IS_EXT ? (p.id & 0x1FFFFFFFu) : 0u;

  std::memcpy(m.Data, p.data, 8);
}

void CH32CAN::TxService()
{
  if (instance_ == nullptr)
  {
    return;
  }

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
  if (instance_ == nullptr)
  {
    return;
  }

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
  if (instance_ == nullptr)
  {
    return;
  }

  // Drain RX FIFO first, then acknowledge RX pending flags.
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

  // Acknowledge RX pending flags.
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
  if (instance_ == nullptr)
  {
    return;
  }

  // Snapshot error flags and LEC before clearing pending bits.
  const bool BOF = (CAN_GetFlagStatus(instance_, CAN_FLAG_BOF) != RESET);
  const bool EPV = (CAN_GetFlagStatus(instance_, CAN_FLAG_EPV) != RESET);
  const bool EWG = (CAN_GetFlagStatus(instance_, CAN_FLAG_EWG) != RESET);
  const uint8_t LEC = CAN_GetLastErrorCode(instance_);

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

  if (BOF)
  {
    eid = CAN::ErrorID::CAN_ERROR_ID_BUS_OFF;
  }
  else if (EPV)
  {
    eid = CAN::ErrorID::CAN_ERROR_ID_ERROR_PASSIVE;
  }
  else if (EWG)
  {
    eid = CAN::ErrorID::CAN_ERROR_ID_ERROR_WARNING;
  }
  else
  {
    switch (LEC)
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
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void CAN1_TX_IRQHandler(void) __attribute__((interrupt));
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void CAN1_TX_IRQHandler(void)
{
  if (auto* can = LibXR::CH32CAN::map[CH32_CAN1])
  {
    can->ProcessTxInterrupt();
  }
}

// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void CAN1_RX1_IRQHandler(void) __attribute__((interrupt));
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void CAN1_RX1_IRQHandler(void)
{
  if (auto* can = LibXR::CH32CAN::map[CH32_CAN1])
  {
    can->ProcessRxInterrupt();
  }
}

// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void CAN1_SCE_IRQHandler(void) __attribute__((interrupt));
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void CAN1_SCE_IRQHandler(void)
{
  if (auto* can = LibXR::CH32CAN::map[CH32_CAN1])
  {
    can->ProcessErrorInterrupt();
  }
}
#endif

#if defined(CAN2)
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void CAN2_TX_IRQHandler(void) __attribute__((interrupt));
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void CAN2_TX_IRQHandler(void)
{
  if (auto* can = LibXR::CH32CAN::map[CH32_CAN2])
  {
    can->ProcessTxInterrupt();
  }
}

// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void CAN2_RX0_IRQHandler(void) __attribute__((interrupt));
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void CAN2_RX0_IRQHandler(void)
{
  if (auto* can = LibXR::CH32CAN::map[CH32_CAN2])
  {
    can->ProcessRxInterrupt();
  }
}

// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void CAN2_RX1_IRQHandler(void) __attribute__((interrupt));
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void CAN2_RX1_IRQHandler(void)
{
  if (auto* can = LibXR::CH32CAN::map[CH32_CAN2])
  {
    can->ProcessRxInterrupt();
  }
}

// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void CAN2_SCE_IRQHandler(void) __attribute__((interrupt));
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void CAN2_SCE_IRQHandler(void)
{
  if (auto* can = LibXR::CH32CAN::map[CH32_CAN2])
  {
    can->ProcessErrorInterrupt();
  }
}
#endif
