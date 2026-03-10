// NOLINTBEGIN(cppcoreguidelines-pro-type-cstyle-cast,performance-no-int-to-ptr)
// ch32_usb_otghs.cpp  (OTG HS)
#include "ch32_usb_dev.hpp"
#include "ch32_usb_endpoint.hpp"
#include "ep.hpp"

using namespace LibXR;
using namespace LibXR::USB;

#if defined(USBHSD)

namespace
{

static void ch32_usb_clock48m_config()
{
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
}

static void ch32_usbhs_rcc_enable()
{
  ch32_usb_clock48m_config();

#if defined(RCC_HSBHSPLLCLKSource_HSE) && defined(RCC_USBPLL_Div2) && \
    defined(RCC_USBHSPLLCKREFCLK_4M)
  RCC_USBHSPLLCLKConfig(RCC_HSBHSPLLCLKSource_HSE);
  RCC_USBHSConfig(RCC_USBPLL_Div2);
  RCC_USBHSPLLCKREFCLKConfig(RCC_USBHSPLLCKREFCLK_4M);
  RCC_USBHSPHYPLLALIVEcmd(ENABLE);
#endif

#if defined(RCC_AHBPeriph_USBHS)
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBHS, ENABLE);
#endif
#if defined(RCC_AHBPeriph_USBFS)
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBFS, ENABLE);
#endif
}

}  // namespace

// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" __attribute__((interrupt)) void USBHS_IRQHandler(void)
{
  auto& map = LibXR::CH32EndpointOtgHs::map_otg_hs_;

  constexpr uint8_t OUT_IDX = static_cast<uint8_t>(LibXR::USB::Endpoint::Direction::OUT);
  constexpr uint8_t IN_IDX = static_cast<uint8_t>(LibXR::USB::Endpoint::Direction::IN);

  while (true)
  {
    const uint16_t INTFGST = *reinterpret_cast<volatile uint16_t*>(
        reinterpret_cast<uintptr_t>(&USBHSD->INT_FG));

    const uint8_t INTFLAG = static_cast<uint8_t>(INTFGST & 0x00FFu);
    const uint8_t INTST = static_cast<uint8_t>((INTFGST >> 8) & 0x00FFu);

    if (INTFLAG == 0)
    {
      break;
    }

    uint8_t clear_mask = 0;

    if (INTFLAG & USBHS_UIF_BUS_RST)
    {
      USBHSD->DEV_AD = 0;

      LibXR::CH32USBOtgHS::self_->Deinit(true);
      LibXR::CH32USBOtgHS::self_->Init(true);

      if (map[0][OUT_IDX])
      {
        map[0][OUT_IDX]->SetState(LibXR::USB::Endpoint::State::IDLE);
      }
      if (map[0][IN_IDX])
      {
        map[0][IN_IDX]->SetState(LibXR::USB::Endpoint::State::IDLE);
      }

      if (map[0][OUT_IDX])
      {
        map[0][OUT_IDX]->tog0_ = true;
        map[0][OUT_IDX]->tog1_ = false;
      }
      if (map[0][IN_IDX])
      {
        map[0][IN_IDX]->tog0_ = true;
        map[0][IN_IDX]->tog1_ = false;
      }

      USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_TOG_DATA1 | USBHS_UEP_T_RES_NAK;
      USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_NAK;

      clear_mask |= USBHS_UIF_BUS_RST;
    }

    if (INTFLAG & USBHS_UIF_SUSPEND)
    {
      LibXR::CH32USBOtgHS::self_->Deinit(true);
      LibXR::CH32USBOtgHS::self_->Init(true);

      if (map[0][OUT_IDX])
      {
        map[0][OUT_IDX]->SetState(LibXR::USB::Endpoint::State::IDLE);
      }
      if (map[0][IN_IDX])
      {
        map[0][IN_IDX]->SetState(LibXR::USB::Endpoint::State::IDLE);
      }

      if (map[0][OUT_IDX])
      {
        map[0][OUT_IDX]->tog0_ = true;
        map[0][OUT_IDX]->tog1_ = false;
      }
      if (map[0][IN_IDX])
      {
        map[0][IN_IDX]->tog0_ = true;
        map[0][IN_IDX]->tog1_ = false;
      }

      USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_TOG_DATA1 | USBHS_UEP_T_RES_NAK;
      USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_NAK;

      clear_mask |= USBHS_UIF_SUSPEND;
    }

    if (INTFLAG & USBHS_UIF_SETUP_ACT)
    {
      USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_TOG_DATA1 | USBHS_UEP_T_RES_NAK;
      USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_NAK;

      if (map[0][OUT_IDX])
      {
        map[0][OUT_IDX]->SetState(LibXR::USB::Endpoint::State::IDLE);
      }
      if (map[0][IN_IDX])
      {
        map[0][IN_IDX]->SetState(LibXR::USB::Endpoint::State::IDLE);
      }

      if (map[0][OUT_IDX])
      {
        map[0][OUT_IDX]->tog0_ = true;
        map[0][OUT_IDX]->tog1_ = false;
      }
      if (map[0][IN_IDX])
      {
        map[0][IN_IDX]->tog0_ = true;
        map[0][IN_IDX]->tog1_ = false;
      }

      LibXR::CH32USBOtgHS::self_->OnSetupPacket(
          true, reinterpret_cast<const LibXR::USB::SetupPacket*>(
                    map[0][OUT_IDX]->GetBuffer().addr_));

      clear_mask |= USBHS_UIF_SETUP_ACT;
    }

    if (INTFLAG & USBHS_UIF_TRANSFER)
    {
      const uint8_t TOKEN = INTST & USBHS_UIS_TOKEN_MASK;
      const uint8_t EPNUM = INTST & USBHS_UIS_ENDP_MASK;

      auto& ep = map[EPNUM];

      switch (TOKEN)
      {
        case USBHS_UIS_TOKEN_SETUP:
        {
          USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_RES_NAK | USBHS_UEP_T_TOG_DATA1;
          USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_RES_NAK | USBHS_UEP_R_TOG_DATA1;

          if (map[0][OUT_IDX])
          {
            map[0][OUT_IDX]->SetState(LibXR::USB::Endpoint::State::IDLE);
          }
          if (map[0][IN_IDX])
          {
            map[0][IN_IDX]->SetState(LibXR::USB::Endpoint::State::IDLE);
          }

          if (map[0][OUT_IDX])
          {
            map[0][OUT_IDX]->tog0_ = true;
            map[0][OUT_IDX]->tog1_ = false;
          }
          if (map[0][IN_IDX])
          {
            map[0][IN_IDX]->tog0_ = true;
            map[0][IN_IDX]->tog1_ = false;
          }

          LibXR::CH32USBOtgHS::self_->OnSetupPacket(
              true, reinterpret_cast<const LibXR::USB::SetupPacket*>(
                        map[0][OUT_IDX]->GetBuffer().addr_));
          break;
        }

        case USBHS_UIS_TOKEN_OUT:
        {
          // NAK is not a completed data OUT transaction.
          if (((INTST & USBHS_UIS_IS_NAK) == 0) && ep[OUT_IDX])
          {
            const uint16_t LEN = USBHSD->RX_LEN;
            ep[OUT_IDX]->TransferComplete(LEN);
          }
          break;
        }

        case USBHS_UIS_TOKEN_IN:
        {
          // NAK is not a completed data IN transaction.
          if (((INTST & USBHS_UIS_IS_NAK) == 0) && ep[IN_IDX])
          {
            ep[IN_IDX]->TransferComplete(0);
          }
          break;
        }

        default:
          break;
      }

      clear_mask |= USBHS_UIF_TRANSFER;
    }

    clear_mask |= static_cast<uint8_t>(INTFLAG & ~(clear_mask));
    USBHSD->INT_FG = clear_mask;
  }
}

CH32USBOtgHS::CH32USBOtgHS(
    const std::initializer_list<CH32USBOtgHS::EPConfig> EP_CFGS, uint16_t vid,
    uint16_t pid, uint16_t bcd,
    const std::initializer_list<const USB::DescriptorStrings::LanguagePack*> LANG_LIST,
    const std::initializer_list<const std::initializer_list<USB::ConfigDescriptorItem*>>
        CONFIGS,
    ConstRawData uid)
    : USB::EndpointPool(EP_CFGS.size() * 2),
      USB::DeviceCore(*this, USB::USBSpec::USB_2_1, USB::Speed::HIGH,
                      USB::DeviceDescriptor::PacketSize0::SIZE_64, vid, pid, bcd,
                      LANG_LIST, CONFIGS, uid)
{
  self_ = this;
  ASSERT(EP_CFGS.size() > 0 && EP_CFGS.size() <= CH32EndpointOtgHs::EP_OTG_HS_MAX_SIZE);

  auto cfgs_itr = EP_CFGS.begin();

  ASSERT(cfgs_itr->buffer_tx.size_ == 64 && cfgs_itr->double_buffer == false);

  auto ep0_out =
      new CH32EndpointOtgHs(USB::Endpoint::EPNumber::EP0, USB::Endpoint::Direction::OUT,
                            cfgs_itr->buffer_rx, false);
  auto ep0_in =
      new CH32EndpointOtgHs(USB::Endpoint::EPNumber::EP0, USB::Endpoint::Direction::IN,
                            cfgs_itr->buffer_tx, false);

  USB::EndpointPool::SetEndpoint0(ep0_in, ep0_out);

  USB::Endpoint::EPNumber ep_index = USB::Endpoint::EPNumber::EP1;

  for (++cfgs_itr, ep_index = USB::Endpoint::EPNumber::EP1; cfgs_itr != EP_CFGS.end();
       ++cfgs_itr, ep_index = USB::Endpoint::NextEPNumber(ep_index))
  {
    if (!cfgs_itr->double_buffer)
    {
      auto ep_out = new CH32EndpointOtgHs(ep_index, USB::Endpoint::Direction::OUT,
                                          cfgs_itr->buffer_rx, false);
      USB::EndpointPool::Put(ep_out);

      auto ep_in = new CH32EndpointOtgHs(ep_index, USB::Endpoint::Direction::IN,
                                         cfgs_itr->buffer_tx, false);
      USB::EndpointPool::Put(ep_in);
    }
    else
    {
      auto ep = new CH32EndpointOtgHs(
          ep_index,
          cfgs_itr->is_in ? USB::Endpoint::Direction::IN : USB::Endpoint::Direction::OUT,
          cfgs_itr->buffer_tx, true);
      USB::EndpointPool::Put(ep);
    }
  }
}

ErrorCode CH32USBOtgHS::SetAddress(uint8_t address, USB::DeviceCore::Context context)
{
  if (context == USB::DeviceCore::Context::STATUS_IN)
  {
    USBHSD->DEV_AD = address;
  }
  return ErrorCode::OK;
}

void CH32USBOtgHS::Start(bool)
{
  ch32_usbhs_rcc_enable();
  USBHSD->CONTROL = USBHS_UC_CLR_ALL | USBHS_UC_RESET_SIE;
  USBHSD->CONTROL &= ~USBHS_UC_RESET_SIE;
  USBHSD->HOST_CTRL = USBHS_UH_PHY_SUSPENDM;
  USBHSD->CONTROL = USBHS_UC_DMA_EN | USBHS_UC_INT_BUSY | USBHS_UC_SPEED_HIGH;
  USBHSD->INT_EN = USBHS_UIE_SETUP_ACT | USBHS_UIE_TRANSFER | USBHS_UIE_DETECT |
                   USBHS_UIE_SUSPEND | USBHS_UIE_ISO_ACT;
  USBHSD->CONTROL |= USBHS_UC_DEV_PU_EN;
  NVIC_EnableIRQ(USBHS_IRQn);
}

void CH32USBOtgHS::Stop(bool)
{
  USBHSD->CONTROL = USBHS_UC_CLR_ALL | USBHS_UC_RESET_SIE;
  USBHSD->CONTROL = 0;
  NVIC_DisableIRQ(USBHS_IRQn);
}

#endif  // defined(USBHSD)

// NOLINTEND(cppcoreguidelines-pro-type-cstyle-cast,performance-no-int-to-ptr)
