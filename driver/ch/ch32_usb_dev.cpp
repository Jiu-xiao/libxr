#include "ch32_usb_dev.hpp"

#include "ch32_usb_endpoint.hpp"
#include "ep.hpp"

using namespace LibXR;
using namespace LibXR::USB;

#if defined(USBFSD)

// NOLINTNEXTLINE
extern "C" __attribute__((interrupt)) void USBFS_IRQHandler(void)
{
  auto& map = LibXR::CH32EndpointOtgFs::map_otg_fs_;

  constexpr uint8_t OUT_IDX = static_cast<uint8_t>(LibXR::USB::Endpoint::Direction::OUT);
  constexpr uint8_t IN_IDX = static_cast<uint8_t>(LibXR::USB::Endpoint::Direction::IN);

  // USBFS: INT_FG[7:5] 是状态位(RO)，INT_FG[4:0] 为中断标志(W1C)。
  constexpr uint8_t CLEARABLE_MASK = USBFS_UIF_FIFO_OV | USBFS_UIF_HST_SOF |
                                     USBFS_UIF_SUSPEND | USBFS_UIF_TRANSFER |
                                     USBFS_UIF_DETECT | USBFS_UIF_BUS_RST;

  while (true)
  {
    // INT_FG(低8) + INT_ST(高8)，两寄存器地址相邻：0x...06 / 0x...07
    const uint16_t intfgst = *reinterpret_cast<volatile uint16_t*>(
        reinterpret_cast<uintptr_t>(&USBFSD->INT_FG));

    const uint8_t intflag = static_cast<uint8_t>(intfgst & 0x00FFu);
    const uint8_t intst = static_cast<uint8_t>((intfgst >> 8) & 0x00FFu);

    const uint8_t pending = static_cast<uint8_t>(intflag & CLEARABLE_MASK);
    if (pending == 0)
    {
      break;
    }

    uint8_t clear_mask = 0;

    if (pending & USBFS_UIF_BUS_RST)
    {
      USBFSD->DEV_ADDR = 0;  // NOLINT

      LibXR::CH32USBDeviceFS::self_->Deinit(true);
      LibXR::CH32USBDeviceFS::self_->Init(true);

      // EP0 toggle/state 复位
      if (map[0][OUT_IDX]) map[0][OUT_IDX]->SetState(LibXR::USB::Endpoint::State::IDLE);
      if (map[0][IN_IDX]) map[0][IN_IDX]->SetState(LibXR::USB::Endpoint::State::IDLE);

      if (map[0][OUT_IDX]) map[0][OUT_IDX]->tog_ = true;
      if (map[0][IN_IDX]) map[0][IN_IDX]->tog_ = true;

      USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_RES_NAK;  // NOLINT
      USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_RES_NAK;  // NOLINT

      clear_mask |= USBFS_UIF_BUS_RST;
    }

    if (pending & USBFS_UIF_SUSPEND)
    {
      LibXR::CH32USBDeviceFS::self_->Deinit(true);
      LibXR::CH32USBDeviceFS::self_->Init(true);

      if (map[0][OUT_IDX]) map[0][OUT_IDX]->SetState(LibXR::USB::Endpoint::State::IDLE);
      if (map[0][IN_IDX]) map[0][IN_IDX]->SetState(LibXR::USB::Endpoint::State::IDLE);

      if (map[0][OUT_IDX]) map[0][OUT_IDX]->tog_ = true;
      if (map[0][IN_IDX]) map[0][IN_IDX]->tog_ = true;

      USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_RES_NAK;  // NOLINT
      USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_RES_NAK;  // NOLINT

      clear_mask |= USBFS_UIF_SUSPEND;
    }

    if (pending & USBFS_UIF_TRANSFER)
    {
      const uint8_t token = intst & USBFS_UIS_TOKEN_MASK;  // MASK_UIS_TOKEN
      const uint8_t epnum = intst & USBFS_UIS_ENDP_MASK;   // MASK_UIS_ENDP

      auto& ep = map[epnum];

      switch (token)
      {
        case USBFS_UIS_TOKEN_SETUP:
        {
          USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_RES_NAK;  // NOLINT
          USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_RES_NAK;  // NOLINT

          if (map[0][OUT_IDX])
            map[0][OUT_IDX]->SetState(LibXR::USB::Endpoint::State::IDLE);
          if (map[0][IN_IDX]) map[0][IN_IDX]->SetState(LibXR::USB::Endpoint::State::IDLE);

          if (map[0][OUT_IDX]) map[0][OUT_IDX]->tog_ = true;
          if (map[0][IN_IDX]) map[0][IN_IDX]->tog_ = true;

          LibXR::CH32USBDeviceFS::self_->OnSetupPacket(
              true,
              reinterpret_cast<const SetupPacket*>(map[0][OUT_IDX]->GetBuffer().addr_));

          break;
        }

        case USBFS_UIS_TOKEN_OUT:
        {
          const uint16_t len = USBFSD->RX_LEN;  // NOLINT
          if (ep[OUT_IDX])
          {
            ep[OUT_IDX]->TransferComplete(len);
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

    // 本轮未显式处理的其它 W1C 标志，一并清掉（与 USBHS 版本一致的“兜底清除”风格）
    clear_mask |= static_cast<uint8_t>(pending & ~clear_mask);

    USBFSD->INT_FG = clear_mask;  // NOLINT  // UIF_* 写 1 清零
  }
}

CH32USBDeviceFS::CH32USBDeviceFS(
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
  self_ = this;
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

ErrorCode CH32USBDeviceFS::SetAddress(uint8_t address, USB::DeviceCore::Context context)
{
  if (context == USB::DeviceCore::Context::STATUS_IN)
  {
    USBFSD->DEV_ADDR = (USBFSD->DEV_ADDR & USBFS_UDA_GP_BIT) | address;  // NOLINT
    USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_RES_NAK;                          // NOLINT
    USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_RES_ACK;                          // NOLINT
  }
  return ErrorCode::OK;
}

void CH32USBDeviceFS::Start(bool)
{
  USBFSH->BASE_CTRL = USBFS_UC_RESET_SIE | USBFS_UC_CLR_ALL;                     // NOLINT
  USBFSH->BASE_CTRL = 0x00;                                                      // NOLINT
  USBFSD->INT_EN = USBFS_UIE_SUSPEND | USBFS_UIE_BUS_RST | USBFS_UIE_TRANSFER;   // NOLINT
  USBFSD->BASE_CTRL = USBFS_UC_DEV_PU_EN | USBFS_UC_INT_BUSY | USBFS_UC_DMA_EN;  // NOLINT
  USBFSD->UDEV_CTRL = USBFS_UD_PD_DIS | USBFS_UD_PORT_EN;                        // NOLINT
  NVIC_EnableIRQ(USBFS_IRQn);
}

void CH32USBDeviceFS::Stop(bool)
{
  USBFSH->BASE_CTRL = USBFS_UC_RESET_SIE | USBFS_UC_CLR_ALL;  // NOLINT
  USBFSD->BASE_CTRL = 0x00;                                   // NOLINT
  NVIC_DisableIRQ(USBFS_IRQn);
}

#endif
#if defined(USBHSD)
// NOLINTNEXTLINE
#if defined(USBHSD)

// NOLINTNEXTLINE
extern "C" __attribute__((interrupt)) void USBHS_IRQHandler(void)
{
  // 端点表
  auto& map = LibXR::CH32EndpointOtgHs::map_otg_hs_;

  constexpr uint8_t OUT_IDX = static_cast<uint8_t>(LibXR::USB::Endpoint::Direction::OUT);
  constexpr uint8_t IN_IDX = static_cast<uint8_t>(LibXR::USB::Endpoint::Direction::IN);

  while (true)
  {
    // INT_FG(低8) + INT_ST(高8)
    const uint16_t intfgst = *reinterpret_cast<volatile uint16_t*>(
        reinterpret_cast<uintptr_t>(&USBHSD->INT_FG));

    const uint8_t intflag = static_cast<uint8_t>(intfgst & 0x00FFu);
    const uint8_t intst = static_cast<uint8_t>((intfgst >> 8) & 0x00FFu);

    if (intflag == 0)
    {
      break;
    }

    uint8_t clear_mask = 0;

    if (intflag & USBHS_UIF_BUS_RST)
    {
      USBHSD->DEV_AD = 0;  // NOLINT

      LibXR::CH32USBDeviceHS::self_->Deinit(true);
      LibXR::CH32USBDeviceHS::self_->Init(true);

      // EP0 toggle/state 复位
      if (map[0][OUT_IDX]) map[0][OUT_IDX]->SetState(LibXR::USB::Endpoint::State::IDLE);
      if (map[0][IN_IDX]) map[0][IN_IDX]->SetState(LibXR::USB::Endpoint::State::IDLE);

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

      USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_TOG_DATA1 | USBHS_UEP_T_RES_NAK;  // NOLINT
      USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_NAK;  // NOLINT

      clear_mask |= USBHS_UIF_BUS_RST;
    }

    if (intflag & USBHS_UIF_SUSPEND)
    {
      LibXR::CH32USBDeviceHS::self_->Deinit(true);
      LibXR::CH32USBDeviceHS::self_->Init(true);

      if (map[0][OUT_IDX]) map[0][OUT_IDX]->SetState(LibXR::USB::Endpoint::State::IDLE);
      if (map[0][IN_IDX]) map[0][IN_IDX]->SetState(LibXR::USB::Endpoint::State::IDLE);

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

      USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_TOG_DATA1 | USBHS_UEP_T_RES_NAK;  // NOLINT
      USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_NAK;  // NOLINT

      clear_mask |= USBHS_UIF_SUSPEND;
    }

    if (intflag & USBHS_UIF_SETUP_ACT)
    {
      USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_TOG_DATA1 | USBHS_UEP_T_RES_NAK;  // NOLINT
      USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_NAK;  // NOLINT

      if (map[0][OUT_IDX]) map[0][OUT_IDX]->SetState(LibXR::USB::Endpoint::State::IDLE);
      if (map[0][IN_IDX]) map[0][IN_IDX]->SetState(LibXR::USB::Endpoint::State::IDLE);

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

      LibXR::CH32USBDeviceHS::self_->OnSetupPacket(
          true, reinterpret_cast<const LibXR::USB::SetupPacket*>(
                    map[0][OUT_IDX]->GetBuffer().addr_));

      clear_mask |= USBHS_UIF_SETUP_ACT;
    }

    if (intflag & USBHS_UIF_TRANSFER)
    {
      const uint8_t token = intst & USBHS_UIS_TOKEN_MASK;
      const uint8_t epnum = intst & USBHS_UIS_ENDP_MASK;

      auto& ep = map[epnum];

      switch (token)
      {
        case USBHS_UIS_TOKEN_SETUP:
        {
          USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_RES_NAK | USBHS_UEP_T_TOG_DATA1;  // NOLINT
          USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_RES_NAK | USBHS_UEP_R_TOG_DATA1;  // NOLINT

          if (map[0][OUT_IDX])
            map[0][OUT_IDX]->SetState(LibXR::USB::Endpoint::State::IDLE);
          if (map[0][IN_IDX]) map[0][IN_IDX]->SetState(LibXR::USB::Endpoint::State::IDLE);

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

          LibXR::CH32USBDeviceHS::self_->OnSetupPacket(
              true, reinterpret_cast<const LibXR::USB::SetupPacket*>(
                        map[0][OUT_IDX]->GetBuffer().addr_));
          break;
        }

        case USBHS_UIS_TOKEN_OUT:
        {
          const uint16_t len = USBHSD->RX_LEN;  // NOLINT

          if (ep[OUT_IDX])
          {
            ep[OUT_IDX]->TransferComplete(len);
          }
          break;
        }

        case USBHS_UIS_TOKEN_IN:
        {
          if (ep[IN_IDX])
          {
            ep[IN_IDX]->TransferComplete(0);
          }
          break;
        }

        case USBHS_UIS_TOKEN_SOF:
        default:
          break;
      }

      clear_mask |= USBHS_UIF_TRANSFER;
    }

    clear_mask |= static_cast<uint8_t>(intflag & ~(clear_mask));

    USBHSD->INT_FG = clear_mask;  // NOLINT
  }
}

#endif

CH32USBDeviceHS::CH32USBDeviceHS(
    const std::initializer_list<CH32USBDeviceHS::EPConfig> EP_CFGS, uint16_t vid,
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

  // EP0 必须 IN+OUT 共用同一个缓冲（64 Byte）
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

ErrorCode CH32USBDeviceHS::SetAddress(uint8_t address, USB::DeviceCore::Context context)
{
  if (context == USB::DeviceCore::Context::STATUS_IN)
  {
    USBHSD->DEV_AD = address;  // NOLINT
  }
  return ErrorCode::OK;
}

void CH32USBDeviceHS::Start(bool)
{
  // NOLINTBEGIN
  USBHSD->CONTROL = USBHS_UC_CLR_ALL | USBHS_UC_RESET_SIE;
  USBHSD->CONTROL &= ~USBHS_UC_RESET_SIE;
  USBHSD->HOST_CTRL = USBHS_UH_PHY_SUSPENDM;
  USBHSD->CONTROL = USBHS_UC_DMA_EN | USBHS_UC_INT_BUSY | USBHS_UC_SPEED_HIGH;
  USBHSD->INT_EN = USBHS_UIE_SETUP_ACT | USBHS_UIE_TRANSFER | USBHS_UIE_DETECT |
                   USBHS_UIE_SUSPEND | USBHS_UIE_ISO_ACT;
  USBHSD->CONTROL |= USBHS_UC_DEV_PU_EN;
  NVIC_EnableIRQ(USBHS_IRQn);
  // NOLINTEND
}

void CH32USBDeviceHS::Stop(bool)
{
  // NOLINTBEGIN
  USBHSD->CONTROL = USBHS_UC_CLR_ALL | USBHS_UC_RESET_SIE;
  USBHSD->CONTROL = 0;
  NVIC_DisableIRQ(USBHS_IRQn);
  // NOLINTEND
}

#endif
