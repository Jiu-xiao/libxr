// NOLINTBEGIN(cppcoreguidelines-pro-type-cstyle-cast,performance-no-int-to-ptr)
// ch32_usb_otgfs.cpp  (OTG FS)
#include "ch32_usb_dev.hpp"
#include "ch32_usb_endpoint.hpp"
#include "ch32_usb_rcc.hpp"
#include "ep.hpp"

using namespace LibXR;
using namespace LibXR::USB;

#if defined(USBFSD)

namespace
{

constexpr uint8_t OTG_FS_CLEARABLE_MASK = USBFS_UIF_FIFO_OV | USBFS_UIF_HST_SOF |
                                          USBFS_UIF_SUSPEND | USBFS_UIF_TRANSFER |
                                          USBFS_UIF_DETECT | USBFS_UIF_BUS_RST;

static void ch32_usbfs_delay_short();
static void EnableUsbFsControllerClock()
{
#if defined(__CH32H417_H) && defined(RCC_HBPeriph_OTG_FS)
  RCC_HBPeriphClockCmd(RCC_HBPeriph_OTG_FS, ENABLE);
#elif defined(RCC_AHBPeriph_USBFS)
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBFS, ENABLE);
#elif defined(RCC_AHBPeriph_USBOTGFS)
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBOTGFS, ENABLE);
#elif defined(RCC_HBPeriph_OTG_FS)
  RCC_HBPeriphClockCmd(RCC_HBPeriph_OTG_FS, ENABLE);
#endif
}

static void ch32_usb_clock48_m_config()
{
#if defined(__CH32H417_H)
  if ((RCC->PLLCFGR & RCC_SYSPLL_SEL) != RCC_SYSPLL_USBHS)
  {
    RCC_USBHS_PLLCmd(DISABLE);
    RCC_USBHSPLLCLKConfig((RCC->CTLR & RCC_HSERDY) ? RCC_USBHSPLLSource_HSE
                                                   : RCC_USBHSPLLSource_HSI);
    RCC_USBHSPLLReferConfig(RCC_USBHSPLLRefer_25M);
    RCC_USBHSPLLClockSourceDivConfig(RCC_USBHSPLL_IN_Div1);
    RCC_USBHS_PLLCmd(ENABLE);
    while ((RCC->CTLR & RCC_USBHS_PLLRDY) == 0)
    {
    }
  }

  RCC_USBFSCLKConfig(RCC_USBFSCLKSource_USBHSPLL);
  RCC_USBFS48ClockSourceDivConfig(RCC_USBFS_Div10);
#else
  RCC_ClocksTypeDef clk{};
  RCC_GetClocksFreq(&clk);

  const uint32_t SYSCLK_HZ = clk.SYSCLK_Frequency;

#if defined(RCC_USBCLKSource_PLLCLK_Div1) && defined(RCC_USBCLKSource_PLLCLK_Div2) && \
    defined(RCC_USBCLKSource_PLLCLK_Div3)
  if (SYSCLK_HZ == 144000000u)
  {
    RCC_USBCLKConfig(RCC_USBCLKSource_PLLCLK_Div3);
  }
  else if (SYSCLK_HZ == 96000000u)
  {
    RCC_USBCLKConfig(RCC_USBCLKSource_PLLCLK_Div2);
  }
  else if (SYSCLK_HZ == 48000000u)
  {
    RCC_USBCLKConfig(RCC_USBCLKSource_PLLCLK_Div1);
  }
#if defined(RCC_USB5PRE_JUDGE) && defined(RCC_USBCLKSource_PLLCLK_Div5)
  else if (SYSCLK_HZ == 240000000u)
  {
    ASSERT(RCC_USB5PRE_JUDGE() == SET);
    RCC_USBCLKConfig(RCC_USBCLKSource_PLLCLK_Div5);
  }
#endif
  else
  {
    ASSERT(false);
  }

#elif defined(RCC_USBCLK48MCLKSource_PLLCLK) && \
    defined(RCC_USBFSCLKSource_PLLCLK_Div1) &&  \
    defined(RCC_USBFSCLKSource_PLLCLK_Div2) && defined(RCC_USBFSCLKSource_PLLCLK_Div3)
  RCC_USBCLK48MConfig(RCC_USBCLK48MCLKSource_PLLCLK);

  if (SYSCLK_HZ == 144000000u)
  {
    RCC_USBFSCLKConfig(RCC_USBFSCLKSource_PLLCLK_Div3);
  }
  else if (SYSCLK_HZ == 96000000u)
  {
    RCC_USBFSCLKConfig(RCC_USBFSCLKSource_PLLCLK_Div2);
  }
  else if (SYSCLK_HZ == 48000000u)
  {
    RCC_USBFSCLKConfig(RCC_USBFSCLKSource_PLLCLK_Div1);
  }
  else
  {
    ASSERT(false);
  }

#else
  (void)SYSCLK_HZ;
#endif
#endif
}

static void ch32_usbfs_rcc_enable()
{
  ch32_usb_clock48_m_config();
  EnableUsbFsControllerClock();
}

static void ch32_usbfs_delay_short()
{
  for (volatile uint32_t i = 0; i < 8000u; ++i)
  {
    asm volatile("nop");
  }
}

static void ResetEp0AfterRecover(CH32EndpointOtgFs* out0, CH32EndpointOtgFs* in0)
{
  ASSERT(out0 != nullptr);
  ASSERT(in0 != nullptr);

  out0->SetState(LibXR::USB::Endpoint::State::IDLE);
  in0->SetState(LibXR::USB::Endpoint::State::IDLE);
  out0->tog_ = true;
  in0->tog_ = true;
  USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_RES_NAK;
  USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_RES_ACK;
}

static void ch32_usbfs_apply_device_registers()
{
#if defined(USBFS_CR_OTG_EN) && defined(USBFS_CR_IDPU)
  USBFSD->OTG_CR = USBFS_CR_OTG_EN | USBFS_CR_IDPU;
#endif
  USBFSD->INT_EN = USBFS_UIE_SUSPEND | USBFS_UIE_BUS_RST | USBFS_UIE_TRANSFER;
}

static void ch32_usbfs_detach_device()
{
  USBFSD->UDEV_CTRL = 0x00;
  USBFSD->BASE_CTRL = USBFS_UC_INT_BUSY | USBFS_UC_DMA_EN;
}

static void ch32_usbfs_enable_device_logic()
{
  USBFSD->BASE_CTRL = USBFS_UC_DEV_PU_EN | USBFS_UC_INT_BUSY | USBFS_UC_DMA_EN;
}

static void ch32_usbfs_enable_device_port()
{
  USBFSD->UDEV_CTRL = USBFS_UD_PD_DIS | USBFS_UD_PORT_EN;
}

static void ch32_usbfs_clear_pending_flags() { USBFSD->INT_FG = OTG_FS_CLEARABLE_MASK; }
static void ClearPendingOtgFsInterrupts()
{
  while (true)
  {
    const uint16_t INTFGST = *reinterpret_cast<volatile uint16_t*>(
        reinterpret_cast<uintptr_t>(&USBFSD->INT_FG));
    const uint8_t INTFLAG = static_cast<uint8_t>(INTFGST & 0x00FFu);
    const uint8_t PENDING = static_cast<uint8_t>(INTFLAG & OTG_FS_CLEARABLE_MASK);
    if (PENDING == 0u)
    {
      break;
    }
    USBFSD->INT_FG = PENDING;
  }
}

static void RestoreUsbFsEndpointState()
{
  auto& ep_map = LibXR::CH32EndpointOtgFs::map_otg_fs_;
  constexpr uint8_t OUT_IDX = static_cast<uint8_t>(LibXR::USB::Endpoint::Direction::OUT);
  constexpr uint8_t IN_IDX = static_cast<uint8_t>(LibXR::USB::Endpoint::Direction::IN);
  constexpr uint8_t N_EP =
      static_cast<uint8_t>(LibXR::CH32EndpointOtgFs::EP_OTG_FS_MAX_SIZE);

  bool rearm_out[N_EP] = {};

  for (uint8_t ep = 0; ep < N_EP; ++ep)
  {
    auto* out = ep_map[ep][OUT_IDX];
    auto* in = ep_map[ep][IN_IDX];

    if (out != nullptr)
    {
      rearm_out[ep] = out->GetState() == LibXR::USB::Endpoint::State::BUSY;
      out->Configure({out->GetDirection(), out->GetType(), out->MaxPacketSize(),
                      out->UseDoubleBuffer()});
    }

    if (in != nullptr)
    {
      in->Configure({in->GetDirection(), in->GetType(), in->MaxPacketSize(),
                     in->UseDoubleBuffer()});
    }
  }

  auto* out0 = ep_map[0][OUT_IDX];
  auto* in0 = ep_map[0][IN_IDX];
  if (out0 != nullptr && in0 != nullptr)
  {
    ResetEp0AfterRecover(out0, in0);
  }

  for (uint8_t ep = 1; ep < N_EP; ++ep)
  {
    if (!rearm_out[ep])
    {
      continue;
    }

    auto* out = ep_map[ep][OUT_IDX];
    if (out == nullptr)
    {
      continue;
    }

    (void)out->Transfer(out->MaxTransferSize());
  }
}

}  // namespace

// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" __attribute__((interrupt("WCH-Interrupt-fast"))) void USBFS_IRQHandler(void)
{
  auto* usb = LibXR::CH32USBOtgFS::self_;
  if (usb == nullptr || !usb->IsInited())
  {
    ClearPendingOtgFsInterrupts();
    return;
  }

  auto& map = LibXR::CH32EndpointOtgFs::map_otg_fs_;

  constexpr uint8_t OUT_IDX = static_cast<uint8_t>(LibXR::USB::Endpoint::Direction::OUT);
  constexpr uint8_t IN_IDX = static_cast<uint8_t>(LibXR::USB::Endpoint::Direction::IN);
  auto* out0 = map[0][OUT_IDX];
  auto* in0 = map[0][IN_IDX];
  ASSERT(out0 != nullptr);
  ASSERT(in0 != nullptr);

  while (true)
  {
    const uint16_t INTFGST = *reinterpret_cast<volatile uint16_t*>(
        reinterpret_cast<uintptr_t>(&USBFSD->INT_FG));
    const uint8_t INTFLAG = static_cast<uint8_t>(INTFGST & 0x00FFu);
    const uint8_t INTST = static_cast<uint8_t>((INTFGST >> 8) & 0x00FFu);
    const uint8_t PENDING = static_cast<uint8_t>(INTFLAG & OTG_FS_CLEARABLE_MASK);

    if (PENDING == 0u)
    {
      break;
    }

    uint8_t clear_mask = 0;

    if (PENDING & USBFS_UIF_BUS_RST)
    {
      USBFSD->DEV_ADDR = 0;

      usb->Deinit(true);
      usb->Init(true);
      RestoreUsbFsEndpointState();

      clear_mask |= USBFS_UIF_BUS_RST;
    }

    if (PENDING & USBFS_UIF_SUSPEND)
    {
      usb->Deinit(true);
      usb->Init(true);
      RestoreUsbFsEndpointState();

      clear_mask |= USBFS_UIF_SUSPEND;
    }

    if (PENDING & USBFS_UIF_TRANSFER)
    {
      const uint8_t TOKEN = INTST & USBFS_UIS_TOKEN_MASK;
      const uint8_t EPNUM = INTST & USBFS_UIS_ENDP_MASK;

      auto& ep = map[EPNUM];

      switch (TOKEN)
      {
        case USBFS_UIS_TOKEN_SETUP:
        {
          USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_RES_NAK;
          USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_RES_NAK;

          out0->SetState(LibXR::USB::Endpoint::State::IDLE);
          in0->SetState(LibXR::USB::Endpoint::State::IDLE);
          out0->tog_ = true;
          in0->tog_ = true;

          usb->OnSetupPacket(true, reinterpret_cast<const SetupPacket*>(out0->GetBuffer().addr_));
          break;
        }

        case USBFS_UIS_TOKEN_OUT:
        {
          const uint16_t LEN = USBFSD->RX_LEN;
          if (ep[OUT_IDX])
          {
            ep[OUT_IDX]->TransferComplete(LEN);
          }
          break;
        }

        case USBFS_UIS_TOKEN_IN:
        {
          if (ep[IN_IDX])
          {
            ep[IN_IDX]->TransferComplete(0);
          }
          break;
        }

        default:
          break;
      }

      clear_mask |= USBFS_UIF_TRANSFER;
    }

    clear_mask |= static_cast<uint8_t>(PENDING & ~clear_mask);
    USBFSD->INT_FG = clear_mask;
  }
}

CH32USBOtgFS::CH32USBOtgFS(
    const std::initializer_list<EPConfig> EP_CFGS,
    USB::DeviceDescriptor::PacketSize0 packet_size, uint16_t vid, uint16_t pid,
    uint16_t bcd,
    const std::initializer_list<const USB::DescriptorStrings::LanguagePack*> LANG_LIST,
    const std::initializer_list<const std::initializer_list<USB::ConfigDescriptorItem*>>
        CONFIGS,
    ConstRawData uid)
    : USB::EndpointPool(EP_CFGS.size() * 2),
      USB::DeviceCore(*this, USB::USBSpec::USB_2_1, USB::Speed::FULL, packet_size, vid,
                      pid, bcd, LANG_LIST, CONFIGS, uid)
{
  ASSERT(EP_CFGS.size() > 0 && EP_CFGS.size() <= CH32EndpointOtgFs::EP_OTG_FS_MAX_SIZE);

  auto cfgs_itr = EP_CFGS.begin();

  auto ep0_out =
      new CH32EndpointOtgFs(USB::Endpoint::EPNumber::EP0, USB::Endpoint::Direction::OUT,
                            cfgs_itr->buffer, false);
  auto ep0_in =
      new CH32EndpointOtgFs(USB::Endpoint::EPNumber::EP0, USB::Endpoint::Direction::IN,
                            cfgs_itr->buffer, false);

  USB::EndpointPool::SetEndpoint0(ep0_in, ep0_out);

  USB::Endpoint::EPNumber ep_index = USB::Endpoint::EPNumber::EP1;

  for (++cfgs_itr, ep_index = USB::Endpoint::EPNumber::EP1; cfgs_itr != EP_CFGS.end();
       ++cfgs_itr, ep_index = USB::Endpoint::NextEPNumber(ep_index))
  {
    if (cfgs_itr->is_in == -1)
    {
      auto ep_out = new CH32EndpointOtgFs(ep_index, USB::Endpoint::Direction::OUT,
                                          cfgs_itr->buffer, false);
      USB::EndpointPool::Put(ep_out);

      auto ep_in = new CH32EndpointOtgFs(ep_index, USB::Endpoint::Direction::IN,
                                         cfgs_itr->buffer, false);
      USB::EndpointPool::Put(ep_in);
    }
    else
    {
      auto ep = new CH32EndpointOtgFs(
          ep_index,
          cfgs_itr->is_in ? USB::Endpoint::Direction::IN : USB::Endpoint::Direction::OUT,
          cfgs_itr->buffer, true);
      USB::EndpointPool::Put(ep);
    }
  }
}

LibXR::ErrorCode CH32USBOtgFS::SetAddress(uint8_t address,
                                          USB::DeviceCore::Context context)
{
  if (context == USB::DeviceCore::Context::STATUS_IN_COMPLETE)
  {
    USBFSD->DEV_ADDR = (USBFSD->DEV_ADDR & USBFS_UDA_GP_BIT) | address;
    USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_RES_NAK;
    USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_RES_ACK;
  }
  return LibXR::ErrorCode::OK;
}

void CH32USBOtgFS::Start(bool)
{
  ch32_usbfs_rcc_enable();
  ch32_usbfs_apply_device_registers();
  ch32_usbfs_enable_device_logic();
  RestoreUsbFsEndpointState();

  ch32_usbfs_enable_device_port();

  NVIC_EnableIRQ(USBFS_IRQn);
  self_ = this;
}

void CH32USBOtgFS::Stop(bool)
{
  NVIC_DisableIRQ(USBFS_IRQn);
  NVIC_ClearPendingIRQ(USBFS_IRQn);

  USBFSD->INT_EN = 0x00;
  ch32_usbfs_detach_device();
  USBFSD->UDEV_CTRL = 0x00;
  ch32_usbfs_clear_pending_flags();
  USBFSH->BASE_CTRL = USBFS_UC_RESET_SIE | USBFS_UC_CLR_ALL;
  ch32_usbfs_delay_short();
  USBFSD->BASE_CTRL = 0x00;
  self_ = nullptr;
}

#endif  // defined(USBFSD)

// NOLINTEND(cppcoreguidelines-pro-type-cstyle-cast,performance-no-int-to-ptr)
