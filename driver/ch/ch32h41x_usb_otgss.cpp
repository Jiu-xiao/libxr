#include "ch32_usb_dev.hpp"

#include <cstdint>

#include "ch32_usb_endpoint.hpp"
#include "ep.hpp"

using namespace LibXR;

#if defined(LIBXR_CH32_HAS_USB_OTG_SS)

extern "C" __attribute__((weak)) void H41xUsbSsDiagStage(uint32_t stage,
                                                         uint32_t link_status,
                                                         uint32_t usb_status)
{
  UNUSED(stage);
  UNUSED(link_status);
  UNUSED(usb_status);
}

namespace
{

using OtgSsEndpointMap = decltype(LibXR::CH32EndpointOtgSs::map_otg_ss_);
using Direction = LibXR::USB::Endpoint::Direction;
using EPNumber = LibXR::USB::Endpoint::EPNumber;

constexpr uint8_t OUT_IDX = static_cast<uint8_t>(Direction::OUT);
constexpr uint8_t IN_IDX = static_cast<uint8_t>(Direction::IN);

constexpr uint8_t LMP_HP = 0u;
constexpr uint32_t LMP_SUBTYPE_MASK = (0x0Fu << 5);
constexpr uint32_t LMP_SET_LINK_FUNC = (0x01u << 5);
constexpr uint32_t LMP_U2_INACT_TOUT = (0x02u << 5);
constexpr uint32_t LMP_PORT_CAP = (0x04u << 5);
constexpr uint32_t LMP_PORT_CFG = (0x05u << 5);
constexpr uint32_t LMP_PORT_CFG_RES = (0x06u << 5);
constexpr uint32_t LMP_LINK_SPEED = (1u << 9);
constexpr uint32_t NUM_HP_BUF = (4u << 0);
constexpr uint32_t DOWN_STREAM = (1u << 16);
constexpr uint32_t UP_STREAM = (2u << 16);
constexpr uint16_t USBSS_PHY_REG12_ACTIVE = 0x67C8u;
constexpr uint16_t USBSS_PHY_REG12_LOW_POWER =
    static_cast<uint16_t>(USBSS_PHY_REG12_ACTIVE & ~(1u << 9));

struct UsbSsRuntimeState
{
  bool u1_enable = false;
  bool u2_enable = false;
  bool link_low_power = false;
};

UsbSsRuntimeState usb_ss_runtime_state{};

static void UsbSsDebugCaptureRegs(uint32_t) {}
static void UsbSsDebugCaptureRegs() {}
static void UsbSsDebugRecordStage(uint32_t stage)
{
  H41xUsbSsDiagStage(stage, USBSSD->LINK_STATUS, USBSSD->USB_STATUS);
}
static void UsbSsDebugReset() {}
static void UsbSsDebugNoteSetup(const LibXR::USB::SetupPacket*) {}

inline volatile uint32_t& UsbSsPhyCfgCr()
{
  return *reinterpret_cast<volatile uint32_t*>(0x400341F8u);
}

inline volatile uint32_t& UsbSsPhyCfgDat()
{
  return *reinterpret_cast<volatile uint32_t*>(0x400341FCu);
}

static volatile USBSS_EP_RX_TypeDef* GetRxEndpoint(EPNumber ep_num)
{
  ASSERT(ep_num >= EPNumber::EP1 &&
         ep_num < static_cast<EPNumber>(CH32EndpointOtgSs::EP_OTG_SS_MAX_SIZE));
  return reinterpret_cast<volatile USBSS_EP_RX_TypeDef*>(
      reinterpret_cast<volatile uint8_t*>(&USBSSD->EP1_RX) +
      (static_cast<uint8_t>(ep_num) - 1u) *
          (sizeof(USBSS_EP_TX_TypeDef) + sizeof(USBSS_EP_RX_TypeDef)));
}

static size_t GetRxTransferSize(EPNumber ep_num)
{
  if (ep_num == EPNumber::EP0)
  {
    return static_cast<size_t>(USBSSD->UEP0_RX_CTRL & USBSS_EP0_RX_LEN_MASK);
  }

  const auto* rx = GetRxEndpoint(ep_num);
  const size_t packets = static_cast<size_t>(rx->UEP_RX_CHAIN_NUMP & 0x0Fu);
  const size_t last_packet_len = static_cast<size_t>(rx->UEP_RX_CHAIN_LEN);
  const size_t packet_size = static_cast<size_t>(rx->UEP_RX_DMA_OFS);
  if (packets <= 1u)
  {
    return last_packet_len;
  }
  return ((packets - 1u) * packet_size) + last_packet_len;
}

static uint32_t GetUsbSsRefConfigForHse()
{
  switch (static_cast<uint32_t>(HSE_VALUE))
  {
    case 20000000u:
      return RCC_USBSSPLLRefer_20M;
    case 24000000u:
      return RCC_USBSSPLLRefer_24M;
    case 25000000u:
      return RCC_USBSSPLLRefer_25M;
    case 30000000u:
      return RCC_USBSSPLLRefer_30M;
    case 32000000u:
      return RCC_USBSSPLLRefer_32M;
    case 40000000u:
      return RCC_USBSSPLLRefer_40M;
    case 60000000u:
      return RCC_USBSSPLLRefer_60M;
    case 80000000u:
      return RCC_USBSSPLLRefer_80M;
    default:
      ASSERT(false);
      return RCC_USBSSPLLRefer_25M;
  }
}

static void ConfigureUsbSsPhyRegister(uint8_t port_num, uint8_t addr, uint16_t data)
{
  if (port_num != 0u)
  {
    return;
  }

  UsbSsPhyCfgCr() = (1u << 23) | (static_cast<uint32_t>(addr) << 16) | data;
  UsbSsPhyCfgDat() = 0x01u;
}

static void ConfigureUsbSsPhyDefaults()
{
  ConfigureUsbSsPhyRegister(0u, 0x03u, 0x7C12u);
  ConfigureUsbSsPhyRegister(0u, 0x0Du, 0x79AAu);
  ConfigureUsbSsPhyRegister(0u, 0x15u, 0x4430u);
  ConfigureUsbSsPhyRegister(0u, 0x13u, 0x0010u);
  *reinterpret_cast<volatile uint32_t*>(0x5003C018u) = 0xB0054000u;
}

static void ConfigureUsbSsPhyLinkPower(bool low_power)
{
  usb_ss_runtime_state.link_low_power = false;
  UNUSED(low_power);
  ConfigureUsbSsPhyRegister(0u, 0x12u, USBSS_PHY_REG12_ACTIVE);
}

static void UsbSsDelayUs(uint32_t us)
{
  const uint32_t loops = ((SystemCoreClock / 1000000u) + 3u) / 4u;
  for (uint32_t remain = us; remain != 0u; --remain)
  {
    for (volatile uint32_t i = loops; i != 0u; --i)
    {
      __NOP();
    }
  }
}

static void EnableUsbSsClockTree()
{
  RCC_USBSSPLLReferConfig(GetUsbSsRefConfigForHse());
  RCC_USBSS_PLLCmd(ENABLE);
  while ((RCC->CTLR & RCC_USBSS_PLLRDY) == 0u)
  {
  }

  RCC_HBPeriphClockCmd(RCC_HBPeriph_USBSS, ENABLE);
  RCC_PIPECmd(ENABLE);
  RCC_UTMIcmd(ENABLE);
}

static void DisableUsbSsClockTree()
{
  RCC_HBPeriphClockCmd(RCC_HBPeriph_USBSS, DISABLE);
  RCC_UTMIcmd(DISABLE);
  RCC_PIPECmd(DISABLE);
  if ((RCC->PLLCFGR & RCC_SYSPLL_SEL) != RCC_SYSPLL_USBSS)
  {
    RCC_USBSS_PLLCmd(DISABLE);
  }
}

static void ResetEp0State(OtgSsEndpointMap& map)
{
  auto* out0 = map[0][OUT_IDX];
  auto* in0 = map[0][IN_IDX];

  ASSERT(out0 != nullptr);
  ASSERT(in0 != nullptr);

  out0->SetState(LibXR::USB::Endpoint::State::IDLE);
  out0->seq_ = 0u;
  in0->SetState(LibXR::USB::Endpoint::State::IDLE);
  in0->seq_ = 0u;

  USBSSD->UEP0_TX_CTRL = 0u;
  USBSSD->UEP0_RX_CTRL = 0u;
  USBSSD->UEP0_TX_DMA = reinterpret_cast<uint32_t>(in0->GetBuffer().addr_);
  USBSSD->UEP0_RX_DMA = reinterpret_cast<uint32_t>(out0->GetBuffer().addr_);
  USBSSD->UEP0_TX_DMA_OFS = in0->MaxPacketSize();
  USBSSD->UEP0_RX_DMA_OFS = out0->MaxPacketSize();
}

static void ResetUsbSsDataPath()
{
  USBSSD->USB_CONTROL |= USBSS_FORCE_RST;
  USBSSD->USB_STATUS = USBSS_UIF_TRANSFER;
  USBSSD->USB_CONTROL =
      USBSS_UIE_TRANSFER | USBSS_UDIE_SETUP | USBSS_UDIE_STATUS | USBSS_DMA_EN |
      USBSS_SETUP_FLOW;
}

static void SetUsbSsLowPowerFeatures(bool u1_enable, bool u2_enable)
{
  usb_ss_runtime_state.u1_enable = u1_enable;
  usb_ss_runtime_state.u2_enable = u2_enable;

  uint32_t link_cfg = USBSSD->LINK_CFG & ~(LINK_U1_ALLOW | LINK_U2_ALLOW);
  if (u1_enable)
  {
    link_cfg |= LINK_U1_ALLOW;
  }
  if (u2_enable)
  {
    link_cfg |= LINK_U2_ALLOW;
  }
  USBSSD->LINK_CFG = link_cfg;
}

static void ResetUsbSsRuntimeState()
{
  ConfigureUsbSsPhyLinkPower(false);
  usb_ss_runtime_state.u1_enable = false;
  usb_ss_runtime_state.u2_enable = false;
  USBSSD->LINK_CFG &= ~(LINK_U1_ALLOW | LINK_U2_ALLOW);
}

static void ReconfigureUsbSsEndpoints(bool rearm_busy_out)
{
  auto& map = LibXR::CH32EndpointOtgSs::map_otg_ss_;
  bool rearm_out[LibXR::CH32EndpointOtgSs::EP_OTG_SS_MAX_SIZE] = {};

  USBSSD->USB_CONTROL |= USBSS_USB_CLR_ALL;
  USBSSD->USB_CONTROL &= ~USBSS_USB_CLR_ALL;
  USBSSD->UEP_TX_EN = 0u;
  USBSSD->UEP_RX_EN = 0u;
  USBSSD->UEP0_TX_CTRL = 0u;
  USBSSD->UEP0_RX_CTRL = 0u;

  for (uint8_t ep = 0u; ep < LibXR::CH32EndpointOtgSs::EP_OTG_SS_MAX_SIZE; ++ep)
  {
    auto* out = map[ep][OUT_IDX];
    auto* in = map[ep][IN_IDX];

    if (out != nullptr)
    {
      rearm_out[ep] = rearm_busy_out &&
                      (out->GetState() == LibXR::USB::Endpoint::State::BUSY) &&
                      (ep != 0u);
      LibXR::USB::Endpoint::Config cfg = {out->GetDirection(), out->GetType(),
                                          out->MaxPacketSize(), out->UseDoubleBuffer()};
      out->Configure(cfg);
    }

    if (in != nullptr)
    {
      LibXR::USB::Endpoint::Config cfg = {in->GetDirection(), in->GetType(),
                                          in->MaxPacketSize(), in->UseDoubleBuffer()};
      in->Configure(cfg);
    }
  }

  ResetEp0State(map);

  for (uint8_t ep = 1u; ep < LibXR::CH32EndpointOtgSs::EP_OTG_SS_MAX_SIZE; ++ep)
  {
    if (!rearm_out[ep])
    {
      continue;
    }

    auto* out = map[ep][OUT_IDX];
    ASSERT(out != nullptr);
    (void)out->Transfer(out->MaxTransferSize());
  }
}

static void ResetEp0TransferStateOnSetup(OtgSsEndpointMap& map)
{
  auto* out0 = map[0][OUT_IDX];
  auto* in0 = map[0][IN_IDX];
  ASSERT(out0 != nullptr);
  ASSERT(in0 != nullptr);

  out0->SetState(LibXR::USB::Endpoint::State::IDLE);
  in0->SetState(LibXR::USB::Endpoint::State::IDLE);
  out0->seq_ = 0u;
  in0->seq_ = 0u;
}

static void HandleLinkInterrupt()
{
  uint32_t link_int = USBSSD->LINK_INT_FLAG;
  const uint32_t link_state = USBSSD->LINK_STATUS & LINK_STATE_MASK;

  if (link_int & LINK_IF_STATE_CHG)
  {
    USBSSD->LINK_INT_FLAG = LINK_IF_STATE_CHG;
    UsbSsDebugRecordStage(0x5310u | (link_state & 0x0Fu));

    if (link_state == LINK_STATE_RXDET)
    {
      ResetUsbSsRuntimeState();
    }
    else if (link_state == LINK_STATE_RECOVERY)
    {
    }
    else if (link_state == LINK_STATE_DISABLE)
    {
      USBSSD->LINK_CTRL &= ~LINK_GO_DISABLED;
    }
    else if (link_state == LINK_STATE_U3 || link_state == LINK_STATE_U2 ||
             link_state == LINK_STATE_U1)
    {
      ConfigureUsbSsPhyLinkPower(false);
    }
    else if (link_state == LINK_STATE_HOTRST)
    {
      ResetUsbSsRuntimeState();
      USBSSD->LINK_CTRL &= ~LINK_HOT_RESET;
      ResetUsbSsDataPath();
      ReconfigureUsbSsEndpoints(true);
    }
    else if (link_state == LINK_STATE_INACTIVE)
    {
      ResetUsbSsRuntimeState();
    }
  }

  if (link_int & LINK_IF_TERM_PRES)
  {
    USBSSD->LINK_INT_FLAG = LINK_IF_TERM_PRES;
    UsbSsDebugRecordStage(0x5321u);
  }

  if (link_int & LINK_IF_RX_LMP_TOUT)
  {
    USBSSD->LINK_INT_FLAG = LINK_IF_RX_LMP_TOUT;
    USBSSD->LINK_CTRL |= LINK_GO_DISABLED;
    USBSSD->LINK_CTRL |= LINK_GO_RX_DET;
    UsbSsDebugRecordStage(0x5322u);
  }

  if (link_int & LINK_IF_TX_LMP)
  {
    USBSSD->LINK_INT_FLAG = LINK_IF_TX_LMP;
    USBSSD->LINK_LMP_TX_DATA0 = LMP_LINK_SPEED | LMP_PORT_CAP | LMP_HP;
    USBSSD->LINK_LMP_TX_DATA1 =
        (USBSSD->LINK_CFG & LINK_DOWN_MODE) ? (DOWN_STREAM | NUM_HP_BUF)
                                            : (UP_STREAM | NUM_HP_BUF);
    USBSSD->LINK_LMP_TX_DATA2 = 0u;
    UsbSsDebugRecordStage(0x5323u);
  }

  if (link_int & LINK_IF_RX_LMP)
  {
    USBSSD->LINK_INT_FLAG = LINK_IF_RX_LMP;
    const uint32_t lmp_data0 = USBSSD->LINK_LMP_RX_DATA0;
    UsbSsDebugRecordStage(0x5324u);

    if ((USBSSD->LINK_CFG & LINK_DOWN_MODE) == 0u)
    {
      if ((lmp_data0 & LMP_SUBTYPE_MASK) == LMP_PORT_CFG)
      {
        USBSSD->LINK_LMP_TX_DATA0 = LMP_LINK_SPEED | LMP_PORT_CFG_RES | LMP_HP;
        USBSSD->LINK_LMP_TX_DATA1 = 0u;
        USBSSD->LINK_LMP_TX_DATA2 = 0u;
        USBSSD->LINK_LMP_PORT_CAP |= LINK_LMP_TX_CAP_VLD;
      }
      else if ((lmp_data0 & LMP_SUBTYPE_MASK) == LMP_U2_INACT_TOUT)
      {
        USBSSD->LINK_U2_INACT_TIMER = static_cast<uint8_t>((lmp_data0 >> 9) & 0xFFu);
      }
      else if ((lmp_data0 & LMP_SUBTYPE_MASK) == LMP_SET_LINK_FUNC)
      {
        // Keep the link in U0 during bring-up. The current SS path still
        // loses stability once the host starts negotiating U1/U2 transitions.
        SetUsbSsLowPowerFeatures(false, false);
      }
    }
  }

  if (link_int & LINK_IF_WARM_RST)
  {
    USBSSD->LINK_INT_FLAG = LINK_IF_WARM_RST;
    if ((USBSSD->LINK_STATUS & LINK_RX_WARM_RST) != 0u)
    {
      ResetUsbSsRuntimeState();
      ResetUsbSsDataPath();
      ReconfigureUsbSsEndpoints(true);
      USBSSD->LINK_CTRL |= LINK_GO_DISABLED;
      __NOP();
      __NOP();
      __NOP();
      __NOP();
      USBSSD->LINK_CTRL &= ~LINK_GO_DISABLED;
    }
    UsbSsDebugRecordStage(0x5325u);
  }

  UsbSsDebugCaptureRegs();
}

static void ClearUsbSsInterrupts()
{
  USBSSD->USB_STATUS = USBSSD->USB_STATUS &
                       (USBSS_UIF_FIFO_RXOV | USBSS_UIF_FIFO_TXOV | USBSS_UIF_ITP |
                        USBSS_UIF_RX_PING | USBSS_UDIF_STATUS | USBSS_UDIF_SETUP |
                        USBSS_UIF_TRANSFER);
}

}  // namespace

extern "C" __attribute__((interrupt("WCH-Interrupt-fast"))) void USBSS_LINK_IRQHandler(void)
{
  if (LibXR::CH32USBOtgSS::self_ == nullptr)
  {
    USBSSD->LINK_INT_FLAG = USBSSD->LINK_INT_FLAG;
    return;
  }
  HandleLinkInterrupt();
}

extern "C" __attribute__((interrupt("WCH-Interrupt-fast"))) void USBSS_IRQHandler(void)
{
  auto* usb = LibXR::CH32USBOtgSS::self_;
  if (usb == nullptr || !usb->IsInited())
  {
    ClearUsbSsInterrupts();
    return;
  }

  auto& map = LibXR::CH32EndpointOtgSs::map_otg_ss_;
  const uint32_t status = USBSSD->USB_STATUS;
  UsbSsDebugCaptureRegs(status);

  if (((status & USBSS_UDIF_SETUP) != 0u) && ((status & USBSS_UDIF_STATUS) == 0u))
  {
    ResetEp0TransferStateOnSetup(map);

    auto* out0 = map[0][OUT_IDX];
    const auto* setup =
        reinterpret_cast<const LibXR::USB::SetupPacket*>(out0->GetBuffer().addr_);
    UsbSsDebugNoteSetup(setup);
    UsbSsDebugRecordStage(0x5330u);
    usb->OnSetupPacket(true, setup);
    USBSSD->USB_STATUS = USBSS_UDIF_SETUP;
    return;
  }

  if ((status & USBSS_UDIF_STATUS) != 0u)
  {
    UsbSsDebugRecordStage(0x5331u);
    auto* in0 = map[0][IN_IDX];
    auto* out0 = map[0][OUT_IDX];
    if (in0 != nullptr && in0->GetState() == LibXR::USB::Endpoint::State::BUSY)
    {
      in0->TransferComplete(0u);
    }
    else if (out0 != nullptr && out0->GetState() == LibXR::USB::Endpoint::State::BUSY)
    {
      out0->TransferComplete(0u);
    }

    USBSSD->UEP0_TX_CTRL = 0u;
    USBSSD->UEP0_RX_CTRL = 0u;
    UsbSsDebugCaptureRegs();
    USBSSD->USB_STATUS = USBSS_UDIF_STATUS;
    return;
  }

  if ((status & USBSS_UIF_TRANSFER) == 0u)
  {
    USBSSD->USB_STATUS = status &
                         (USBSS_UIF_FIFO_RXOV | USBSS_UIF_FIFO_TXOV | USBSS_UIF_ITP |
                          USBSS_UIF_RX_PING);
    return;
  }

  const uint8_t ep_num = static_cast<uint8_t>((status & USBSS_EP_ID_MASK) >> 8);
  const bool is_in = (status & USBSS_EP_DIR_MASK) != 0u;
  if (ep_num >= LibXR::CH32EndpointOtgSs::EP_OTG_SS_MAX_SIZE)
  {
    USBSSD->USB_STATUS = USBSS_UIF_TRANSFER;
    return;
  }

  auto* ep = map[ep_num][is_in ? IN_IDX : OUT_IDX];
  if (ep == nullptr)
  {
    USBSSD->USB_STATUS = USBSS_UIF_TRANSFER;
    return;
  }

  const size_t transfer_size =
      is_in ? 0u : GetRxTransferSize(static_cast<EPNumber>(ep_num));
  if (ep_num == 0u)
  {
    if (is_in)
    {
      UsbSsDebugRecordStage(0x5332u);
    }
    else
    {
      UsbSsDebugRecordStage(0x5333u);
    }
  }
  ep->TransferComplete(transfer_size);
  UsbSsDebugCaptureRegs();
  USBSSD->USB_STATUS = USBSS_UIF_TRANSFER;
}

CH32USBOtgSS::CH32USBOtgSS(
    const std::initializer_list<CH32USBOtgSS::EPConfig> EP_CFGS, uint16_t vid,
    uint16_t pid, uint16_t bcd,
    const std::initializer_list<const USB::DescriptorStrings::LanguagePack*> LANG_LIST,
    const std::initializer_list<const std::initializer_list<USB::ConfigDescriptorItem*>>
        CONFIGS,
    USB::USBSpec spec, ConstRawData uid)
    : USB::EndpointPool(EP_CFGS.size() * 2u),
      USB::DeviceCore(*this, spec, USB::Speed::SUPER,
                      USB::DeviceDescriptor::PacketSize0::SIZE_512, vid, pid, bcd,
                      LANG_LIST, CONFIGS, uid)
{
  ASSERT(EP_CFGS.size() > 0u &&
         EP_CFGS.size() <= LibXR::CH32EndpointOtgSs::EP_OTG_SS_MAX_SIZE);

  auto cfgs_itr = EP_CFGS.begin();
  ASSERT(cfgs_itr->buffer_tx.addr_ != nullptr && cfgs_itr->buffer_rx.addr_ != nullptr);
  ASSERT(cfgs_itr->buffer_tx.size_ >= 512u && cfgs_itr->buffer_rx.size_ >= 512u);

  auto* ep0_out =
      new CH32EndpointOtgSs(USB::Endpoint::EPNumber::EP0, Direction::OUT,
                            cfgs_itr->buffer_rx, cfgs_itr->rx_max_burst);
  auto* ep0_in =
      new CH32EndpointOtgSs(USB::Endpoint::EPNumber::EP0, Direction::IN,
                            cfgs_itr->buffer_tx, cfgs_itr->tx_max_burst);
  USB::EndpointPool::SetEndpoint0(ep0_in, ep0_out);

  USB::Endpoint::EPNumber ep_index = USB::Endpoint::EPNumber::EP1;
  for (++cfgs_itr; cfgs_itr != EP_CFGS.end();
       ++cfgs_itr, ep_index = USB::Endpoint::NextEPNumber(ep_index))
  {
    if (cfgs_itr->buffer_rx.addr_ != nullptr && cfgs_itr->buffer_rx.size_ > 0u)
    {
      auto* ep_out = new CH32EndpointOtgSs(ep_index, Direction::OUT,
                                           cfgs_itr->buffer_rx, cfgs_itr->rx_max_burst);
      USB::EndpointPool::Put(ep_out);
    }

    if (cfgs_itr->buffer_tx.addr_ != nullptr && cfgs_itr->buffer_tx.size_ > 0u)
    {
      auto* ep_in = new CH32EndpointOtgSs(ep_index, Direction::IN,
                                          cfgs_itr->buffer_tx, cfgs_itr->tx_max_burst);
      USB::EndpointPool::Put(ep_in);
    }
  }
}

LibXR::ErrorCode CH32USBOtgSS::SetAddress(uint8_t address,
                                          USB::DeviceCore::Context context)
{
  switch (context)
  {
    case USB::DeviceCore::Context::SETUP_BEFORE_STATUS:
      UsbSsDebugRecordStage(0x5341u);
      break;

    case USB::DeviceCore::Context::STATUS_IN_ARMED:
      UsbSsDebugRecordStage(0x5342u);
      break;

    case USB::DeviceCore::Context::STATUS_IN_COMPLETE:
      UsbSsDebugRecordStage(0x5343u);
      break;

    default:
      break;
  }
  if (context == USB::DeviceCore::Context::STATUS_IN_COMPLETE)
  {
    USBSSD->USB_CONTROL &= 0x00FFFFFFu;
    USBSSD->USB_CONTROL |= static_cast<uint32_t>(address) << 24;
    UsbSsDebugCaptureRegs();
  }
  return LibXR::ErrorCode::OK;
}

void CH32USBOtgSS::Start(bool)
{
  UsbSsDebugReset();
  UsbSsDebugRecordStage(0x5301u);
  EnableUsbSsClockTree();
  ConfigureUsbSsPhyDefaults();
  usb_ss_runtime_state = {};

  USBSSD->LINK_CFG = LINK_RX_EQ_EN | LINK_TX_DEEMPH_MASK | LINK_PHY_RESET;
  USBSSD->LINK_CTRL = LINK_P2_MODE | LINK_GO_DISABLED;
  USBSSD->LINK_CFG =
      LINK_RX_EQ_EN | LINK_TX_DEEMPH_MASK | LINK_LTSSM_MODE | LINK_TOUT_MODE;
  USBSSD->LINK_LPM_CR |= LINK_LPM_EN;
  USBSSD->LINK_CFG |= LINK_RX_TERM_EN;
  USBSSD->LINK_INT_CTRL = LINK_IE_TX_LMP | LINK_IE_RX_LMP | LINK_IE_RX_LMP_TOUT |
                          LINK_IE_STATE_CHG | LINK_IE_WARM_RST | LINK_IE_TERM_PRES;
  USBSSD->LINK_CTRL = LINK_P2_MODE;
  USBSSD->LINK_U1_WKUP_TMR = 120u;
  USBSSD->LINK_U1_WKUP_FILTER = 50u;
  USBSSD->LINK_U2_WKUP_FILTER = 0u;
  USBSSD->LINK_U3_WKUP_FILTER = 0u;

  ResetUsbSsDataPath();
  ReconfigureUsbSsEndpoints(false);
  self_ = this;
  UsbSsDebugRecordStage(0x5302u);

  NVIC_SetAllocateIRQ(USBSS_IRQn, Core_ID_V5F);
  NVIC_SetAllocateIRQ(USBSS_LINK_IRQn, Core_ID_V5F);
  NVIC_ClearPendingIRQ(USBSS_IRQn);
  NVIC_ClearPendingIRQ(USBSS_LINK_IRQn);
  NVIC_EnableIRQ(USBSS_IRQn);
  NVIC_EnableIRQ(USBSS_LINK_IRQn);
  UsbSsDebugRecordStage(0x5303u);
}

void CH32USBOtgSS::Stop(bool)
{
  UsbSsDebugRecordStage(0x5304u);
  NVIC_DisableIRQ(USBSS_LINK_IRQn);
  NVIC_DisableIRQ(USBSS_IRQn);
  self_ = nullptr;

  USBSSD->USB_CONTROL = USBSS_FORCE_RST;
  USBSSD->LINK_CFG |= LINK_PHY_RESET | U3_LINK_RESET;
  UsbSsDelayUs(100u);
  USBSSD->USB_CONTROL &= ~USBSS_FORCE_RST;
  USBSSD->LINK_CFG &= ~(LINK_PHY_RESET | U3_LINK_RESET);

  DisableUsbSsClockTree();
  usb_ss_runtime_state = {};
  UsbSsDebugRecordStage(0x5305u);
}

void CH32USBOtgSS::SetSuperSpeedFeature(uint16_t selector, bool enable)
{
  UNUSED(enable);
  switch (selector)
  {
    case USB::FEATURE_U1_ENABLE:
    case USB::FEATURE_U2_ENABLE:
      // Keep the link pinned in U0 during bring-up. The CH32H41x SS path is not
      // stable yet once Windows starts enabling U1/U2 through standard device
      // features, so ACK the request but do not expose low-power entry.
      SetUsbSsLowPowerFeatures(false, false);
      break;

    default:
      break;
  }
}

uint16_t CH32USBOtgSS::GetSuperSpeedDeviceStatus() const
{
  return (usb_ss_runtime_state.u1_enable ? (1u << 2) : 0u) |
         (usb_ss_runtime_state.u2_enable ? (1u << 3) : 0u);
}

void CH32USBOtgSS::SetIsochronousDelay(uint16_t delay) { USBSSD->LINK_ISO_DLY = delay; }

void CH32USBOtgSS::OnSetSelData(const uint8_t* data, size_t size)
{
  UNUSED(data);
  UNUSED(size);
}

#endif  // defined(LIBXR_CH32_HAS_USB_OTG_SS)
