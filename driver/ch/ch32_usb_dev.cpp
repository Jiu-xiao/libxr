#include "ch32_usb_dev.hpp"

#include "ch32_usb_endpoint.hpp"
#include "ep.hpp"

using namespace LibXR;
using namespace LibXR::USB;

#if defined(USBFSD)

extern "C" __attribute__((interrupt)) void USBFS_IRQHandler(void)
{
  uint8_t intflag = USBFSD->INT_FG;
  uint8_t intst = USBFSD->INT_ST;

  auto &map = LibXR::CH32Endpoint::map_dev_;

  if (intflag & USBFS_UIF_TRANSFER)
  {
    uint8_t token = intst & USBFS_UIS_TOKEN_MASK;
    uint8_t epnum = intst & USBFS_UIS_ENDP_MASK;

    auto ep = map[epnum];
    USBFSD->INT_FG = USBFS_UIF_TRANSFER;  // 清中断标志

    if (ep)
    {
      switch (token)
      {
        // --- SETUP阶段：只分发SETUP包到上层处理 ---
        case USBFS_UIS_TOKEN_SETUP:
          USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_RES_NAK;
          USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_RES_ACK;
          LibXR::CH32Endpoint::map_dev_[0][0]->tog_ = true;
          LibXR::CH32Endpoint::map_dev_[0][1]->tog_ = true;
          // 分发EP0 SETUP事件
          LibXR::CH32USBDeviceFS::self_->OnSetupPacket(
              true, reinterpret_cast<const SetupPacket *>(
                        LibXR::CH32Endpoint::map_dev_[0][0]->GetBuffer().addr_));
          break;

        // --- DATA阶段 ---
        case USBFS_UIS_TOKEN_OUT:
        {
          uint16_t len = USBFSD->RX_LEN;
          ep[static_cast<uint8_t>(Endpoint::Direction::OUT)]->TransferComplete(len);
          break;
        }
        case USBFS_UIS_TOKEN_IN:
        {
          ep[static_cast<uint8_t>(Endpoint::Direction::IN)]->TransferComplete(0);
          break;
        }

        // 其他情况略
        default:
          break;
      }
    }
  }
  else if (intflag & USBFS_UIF_BUS_RST)
  {
    USBFSD->INT_FG = USBFS_UIF_BUS_RST;
    USBFSD->DEV_ADDR = 0;
    LibXR::CH32USBDeviceFS::self_->Deinit();
    LibXR::CH32USBDeviceFS::self_->Init();
    LibXR::CH32Endpoint::map_dev_[0][0]->tog_ = true;
    LibXR::CH32Endpoint::map_dev_[0][1]->tog_ = true;
    USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_RES_NAK;
    USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_RES_ACK;
  }
  else if (intflag & USBFS_UIF_SUSPEND)
  {
    USBFSD->INT_FG = USBFS_UIF_SUSPEND;
    LibXR::CH32USBDeviceFS::self_->Deinit();
    LibXR::CH32USBDeviceFS::self_->Init();
    LibXR::CH32Endpoint::map_dev_[0][0]->tog_ = true;
    LibXR::CH32Endpoint::map_dev_[0][1]->tog_ = true;
    USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_RES_NAK;
    USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_RES_ACK;
  }
  else
  {
    // 其他事件，直接清除
    USBFSD->INT_FG = intflag;
  }
}

CH32USBDeviceFS::CH32USBDeviceFS(
    const std::initializer_list<LibXR::RawData> EP_CFGS,
    USB::DeviceDescriptor::PacketSize0 packet_size, uint16_t vid, uint16_t pid,
    uint16_t bcd,
    const std::initializer_list<const USB::DescriptorStrings::LanguagePack *> LANG_LIST,
    const std::initializer_list<const std::initializer_list<USB::ConfigDescriptorItem *>>
        CONFIGS)
    : USB::EndpointPool(EP_CFGS.size() * 2),
      USB::DeviceCore(*this, USB::USBSpec::USB_2_0, USB::Speed::FULL, packet_size, vid,
                      pid, bcd, LANG_LIST, CONFIGS)
{
  self_ = this;
  ASSERT(EP_CFGS.size() > 0 && EP_CFGS.size() <= 8);

  auto cfgs_itr = EP_CFGS.begin();

  // EP0 必须 IN+OUT 单缓冲, buffer 128
  ASSERT(cfgs_itr->size_ == 64);

  auto ep0_out = new CH32Endpoint(USB::Endpoint::EPNumber::EP0, CH32_USB_FS_DEV,
                                  USB::Endpoint::Direction::OUT, *cfgs_itr);
  auto ep0_in = new CH32Endpoint(USB::Endpoint::EPNumber::EP0, CH32_USB_FS_DEV,
                                 USB::Endpoint::Direction::IN, *cfgs_itr);

  USB::EndpointPool::SetEndpoint0(ep0_in, ep0_out);

  cfgs_itr++;
  USB::Endpoint::EPNumber ep_index = USB::Endpoint::EPNumber::EP1;

  while (cfgs_itr != EP_CFGS.end())
  {
    ASSERT(cfgs_itr->size_ == 128 || cfgs_itr->size_ == 256);
    auto ep_out = new CH32Endpoint(ep_index, CH32_USB_FS_DEV,
                                   USB::Endpoint::Direction::OUT, *cfgs_itr);
    USB::EndpointPool::Put(ep_out);

    auto ep_in = new CH32Endpoint(ep_index, CH32_USB_FS_DEV, USB::Endpoint::Direction::IN,
                                  *cfgs_itr);
    USB::EndpointPool::Put(ep_in);

    ep_index = USB::Endpoint::NextEPNumber(ep_index);
    cfgs_itr++;
  }
}

ErrorCode CH32USBDeviceFS::SetAddress(uint8_t address, USB::DeviceCore::Context context)
{
  if (context == USB::DeviceCore::Context::STATUS_IN)
  {
    USBFSD->DEV_ADDR = (USBFSD->DEV_ADDR & USBFS_UDA_GP_BIT) | address;
    USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_RES_NAK;
    USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_RES_ACK;
  }
  return ErrorCode::OK;
}

void CH32USBDeviceFS::Start()
{
  USBFSH->BASE_CTRL = USBFS_UC_RESET_SIE | USBFS_UC_CLR_ALL;
  USBFSH->BASE_CTRL = 0x00;
  USBFSD->INT_EN = USBFS_UIE_SUSPEND | USBFS_UIE_BUS_RST | USBFS_UIE_TRANSFER;
  USBFSD->BASE_CTRL = USBFS_UC_DEV_PU_EN | USBFS_UC_INT_BUSY | USBFS_UC_DMA_EN;
  USBFSD->UDEV_CTRL = USBFS_UD_PD_DIS | USBFS_UD_PORT_EN;
  NVIC_EnableIRQ(USBFS_IRQn);
}

void CH32USBDeviceFS::Stop()
{
  USBFSH->BASE_CTRL = USBFS_UC_RESET_SIE | USBFS_UC_CLR_ALL;
  USBFSD->BASE_CTRL = 0x00;
  NVIC_DisableIRQ(USBFS_IRQn);
}

#endif
