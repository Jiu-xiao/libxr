#include "stm32_usb_dev.hpp"

#if defined(HAL_PCD_MODULE_ENABLED)

using namespace LibXR;

stm32_usb_dev_id_t STM32USBDeviceGetID(PCD_HandleTypeDef *hpcd)
{
  for (int i = 0; i < STM32_USB_DEV_ID_NUM; i++)
  {
    if (STM32USBDevice::map_[i] && STM32USBDevice::map_[i]->hpcd_ == hpcd)
    {
      return static_cast<stm32_usb_dev_id_t>(i);
    }
  }
  return STM32_USB_DEV_ID_NUM;
}

extern "C" void HAL_PCD_SOFCallback(PCD_HandleTypeDef *hpcd) { UNUSED(hpcd); }

extern "C" void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *hpcd)
{
  auto id = STM32USBDeviceGetID(hpcd);

  ASSERT(id < STM32_USB_DEV_ID_NUM);

  auto usb = STM32USBDevice::map_[id];

  if (!usb)
  {
    return;
  }

#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
  SCB_InvalidateDCache_by_Addr(hpcd->Setup,
                               static_cast<int32_t>(sizeof(USB::SetupPacket)));
#endif

  usb->OnSetupPacket(true, reinterpret_cast<USB::SetupPacket *>(hpcd->Setup));
}

extern "C" void HAL_PCD_ResetCallback(PCD_HandleTypeDef *hpcd)
{
  auto id = STM32USBDeviceGetID(hpcd);

  ASSERT(id < STM32_USB_DEV_ID_NUM);

  auto usb = STM32USBDevice::map_[id];

  if (!usb)
  {
    return;
  }

  usb->Deinit();
  usb->Init();
}

extern "C" void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *hpcd)
{
  auto id = STM32USBDeviceGetID(hpcd);

  ASSERT(id < STM32_USB_DEV_ID_NUM);

  auto usb = STM32USBDevice::map_[id];

  if (!usb)
  {
    return;
  }
  usb->Deinit();
}

extern "C" void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *hpcd)
{
  auto id = STM32USBDeviceGetID(hpcd);

  ASSERT(id < STM32_USB_DEV_ID_NUM);

  auto usb = STM32USBDevice::map_[id];

  if (!usb)
  {
    return;
  }
  usb->Init();
}

extern "C" void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *hpcd) { UNUSED(hpcd); }

extern "C" void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *hpcd) { UNUSED(hpcd); }

#if (defined(USB_OTG_FS))

STM32USBDeviceOtgFS::STM32USBDeviceOtgFS(
    PCD_HandleTypeDef *hpcd, size_t rx_fifo_size,
    const std::initializer_list<LibXR::RawData> RX_EP_CFGS,
    const std::initializer_list<EPInConfig> TX_EP_CFGS,
    USB::DeviceDescriptor::PacketSize0 packet_size, uint16_t vid, uint16_t pid,
    uint16_t bcd,
    const std::initializer_list<const USB::DescriptorStrings::LanguagePack *> LANG_LIST,
    const std::initializer_list<const std::initializer_list<USB::ConfigDescriptorItem *>>
        CONFIGS)
    : STM32USBDevice(hpcd, STM32_USB_OTG_FS, RX_EP_CFGS.size() + TX_EP_CFGS.size(),
                     packet_size, vid, pid, bcd, LANG_LIST, CONFIGS)
{
  ASSERT(RX_EP_CFGS.size() > 0 && RX_EP_CFGS.size() <= STM32Endpoint::EP_OTG_FS_MAX_SIZE);
  ASSERT(TX_EP_CFGS.size() > 0 && TX_EP_CFGS.size() <= STM32Endpoint::EP_OTG_FS_MAX_SIZE);
  ASSERT(64 * RX_EP_CFGS.size() <= rx_fifo_size);

  auto rx_cfgs_itr = RX_EP_CFGS.begin();
  auto tx_cfgs_itr = TX_EP_CFGS.begin();

  auto ep0_in = new STM32Endpoint(USB::Endpoint::EPNumber::EP0, id_, hpcd_,
                                  USB::Endpoint::Direction::IN, (*tx_cfgs_itr).fifo_size,
                                  (*tx_cfgs_itr).buffer);

  auto ep0_out =
      new STM32Endpoint(USB::Endpoint::EPNumber::EP0, id_, hpcd_,
                        USB::Endpoint::Direction::OUT, rx_fifo_size, (*rx_cfgs_itr));

  USB::EndpointPool::SetEndpoint0(ep0_in, ep0_out);

  rx_cfgs_itr++;
  tx_cfgs_itr++;

  USB::Endpoint::EPNumber rx_ep_index = USB::Endpoint::EPNumber::EP1;

  size_t fifo_used_size = rx_fifo_size;

  for (; rx_cfgs_itr != RX_EP_CFGS.end(); rx_cfgs_itr++)
  {
    auto ep = new STM32Endpoint(rx_ep_index, id_, hpcd_, USB::Endpoint::Direction::OUT,
                                rx_fifo_size, (*rx_cfgs_itr));
    USB::EndpointPool::Put(ep);
    rx_ep_index = USB::Endpoint::NextEPNumber(rx_ep_index);
  }

  USB::Endpoint::EPNumber tx_ep_index = USB::Endpoint::EPNumber::EP1;

  for (; tx_cfgs_itr != TX_EP_CFGS.end(); tx_cfgs_itr++)
  {
    auto ep = new STM32Endpoint(tx_ep_index, id_, hpcd_, USB::Endpoint::Direction::IN,
                                (*tx_cfgs_itr).fifo_size, (*tx_cfgs_itr).buffer);
    USB::EndpointPool::Put(ep);
    tx_ep_index = USB::Endpoint::NextEPNumber(tx_ep_index);
    fifo_used_size += (*tx_cfgs_itr).fifo_size;
  }

  if (fifo_used_size > USB_OTG_FS_TOTAL_FIFO_SIZE)
  {
    ASSERT(false);
  }
}

ErrorCode STM32USBDeviceOtgFS::SetAddress(uint8_t address,
                                          USB::DeviceCore::Context context)
{
  HAL_StatusTypeDef ans = HAL_OK;

  if (context == USB::DeviceCore::Context::SETUP)
  {
    ans = HAL_PCD_SetAddress(hpcd_, address);
  }
  return (ans == HAL_OK) ? ErrorCode::OK : ErrorCode::FAILED;
}

#endif

#if (defined(USB_OTG_HS))

STM32USBDeviceOtgHS::STM32USBDeviceOtgHS(
    PCD_HandleTypeDef *hpcd, size_t rx_fifo_size,
    const std::initializer_list<LibXR::RawData> RX_EP_CFGS,
    const std::initializer_list<EPInConfig> TX_EP_CFGS,
    USB::DeviceDescriptor::PacketSize0 packet_size, uint16_t vid, uint16_t pid,
    uint16_t bcd,
    const std::initializer_list<const USB::DescriptorStrings::LanguagePack *> LANG_LIST,
    const std::initializer_list<const std::initializer_list<USB::ConfigDescriptorItem *>>
        CONFIGS)
    : STM32USBDevice(
          hpcd, STM32_USB_OTG_HS, RX_EP_CFGS.size() + TX_EP_CFGS.size(), packet_size, vid,
          pid, bcd, LANG_LIST, CONFIGS,
          hpcd->Init.speed == PCD_SPEED_HIGH ? USB::Speed::HIGH : USB::Speed::FULL)
{
  ASSERT(RX_EP_CFGS.size() > 0 && RX_EP_CFGS.size() <= STM32Endpoint::EP_OTG_HS_MAX_SIZE);
  ASSERT(TX_EP_CFGS.size() > 0 && TX_EP_CFGS.size() <= STM32Endpoint::EP_OTG_HS_MAX_SIZE);
  ASSERT(64 * RX_EP_CFGS.size() <= rx_fifo_size);

  auto rx_cfgs_itr = RX_EP_CFGS.begin();
  auto tx_cfgs_itr = TX_EP_CFGS.begin();

  auto ep0_in = new STM32Endpoint(USB::Endpoint::EPNumber::EP0, id_, hpcd_,
                                  USB::Endpoint::Direction::IN, (*tx_cfgs_itr).fifo_size,
                                  (*tx_cfgs_itr).buffer);

  auto ep0_out =
      new STM32Endpoint(USB::Endpoint::EPNumber::EP0, id_, hpcd_,
                        USB::Endpoint::Direction::OUT, rx_fifo_size, (*rx_cfgs_itr));

  USB::EndpointPool::SetEndpoint0(ep0_in, ep0_out);

  rx_cfgs_itr++;
  tx_cfgs_itr++;

  USB::Endpoint::EPNumber rx_ep_index = USB::Endpoint::EPNumber::EP1;

  size_t fifo_used_size = rx_fifo_size;

  for (; rx_cfgs_itr != RX_EP_CFGS.end(); rx_cfgs_itr++)
  {
    auto ep = new STM32Endpoint(rx_ep_index, id_, hpcd_, USB::Endpoint::Direction::OUT,
                                rx_fifo_size, (*rx_cfgs_itr));
    USB::EndpointPool::Put(ep);
    rx_ep_index = USB::Endpoint::NextEPNumber(rx_ep_index);
  }

  USB::Endpoint::EPNumber tx_ep_index = USB::Endpoint::EPNumber::EP1;

  for (; tx_cfgs_itr != TX_EP_CFGS.end(); tx_cfgs_itr++)
  {
    auto ep = new STM32Endpoint(tx_ep_index, id_, hpcd_, USB::Endpoint::Direction::IN,
                                (*tx_cfgs_itr).fifo_size, (*tx_cfgs_itr).buffer);
    USB::EndpointPool::Put(ep);
    tx_ep_index = USB::Endpoint::NextEPNumber(tx_ep_index);
    fifo_used_size += (*tx_cfgs_itr).fifo_size;
  }

  if (fifo_used_size > USB_OTG_HS_TOTAL_FIFO_SIZE)
  {
    ASSERT(false);
  }
}

ErrorCode STM32USBDeviceOtgHS::SetAddress(uint8_t address,
                                          USB::DeviceCore::Context context)
{
  HAL_StatusTypeDef ans = HAL_OK;

  if (context == USB::DeviceCore::Context::SETUP)
  {
    ans = HAL_PCD_SetAddress(hpcd_, address);
  }
  return (ans == HAL_OK) ? ErrorCode::OK : ErrorCode::FAILED;
}

#endif

#if defined(USB_BASE)
STM32USBDeviceDevFs::STM32USBDeviceDevFs(
    PCD_HandleTypeDef *hpcd, const std::initializer_list<EPConfig> EP_CFGS,
    USB::DeviceDescriptor::PacketSize0 packet_size, uint16_t vid, uint16_t pid,
    uint16_t bcd,
    const std::initializer_list<const USB::DescriptorStrings::LanguagePack *> LANG_LIST,
    const std::initializer_list<const std::initializer_list<USB::ConfigDescriptorItem *>>
        CONFIGS)
    : STM32USBDevice(hpcd, STM32_USB_FS_DEV, EP_CFGS.size() * 2, packet_size, vid, pid,
                     bcd, LANG_LIST, CONFIGS)
{
  ASSERT(EP_CFGS.size() > 0 && EP_CFGS.size() <= hpcd->Init.dev_endpoints);

  auto cfgs_itr = EP_CFGS.begin();

  ASSERT(cfgs_itr->double_buffer == false);

  size_t buffer_offset = 0x20;

  auto ep0_out = new STM32Endpoint(
      USB::Endpoint::EPNumber::EP0, id_, hpcd_, USB::Endpoint::Direction::OUT,
      buffer_offset, (*cfgs_itr).hw_buffer_size2, false, (*cfgs_itr).buffer2);

  buffer_offset += (*cfgs_itr).hw_buffer_size2;

  auto ep0_in = new STM32Endpoint(
      USB::Endpoint::EPNumber::EP0, id_, hpcd_, USB::Endpoint::Direction::IN,
      buffer_offset, (*cfgs_itr).hw_buffer_size1, false, (*cfgs_itr).buffer1);

  buffer_offset += (*cfgs_itr).hw_buffer_size1;

  USB::EndpointPool::SetEndpoint0(ep0_in, ep0_out);

  cfgs_itr++;

  USB::Endpoint::EPNumber ep_index = USB::Endpoint::EPNumber::EP1;

  while (cfgs_itr != EP_CFGS.end())
  {
    if (cfgs_itr->double_buffer)
    {
      ASSERT(cfgs_itr->buffer1.size_ % 2 == 0);
      auto ep =
          new STM32Endpoint(ep_index, id_, hpcd_,
                            cfgs_itr->double_buffer_is_in ? USB::Endpoint::Direction::IN
                                                          : USB::Endpoint::Direction::OUT,
                            buffer_offset, (*cfgs_itr).hw_buffer_size1,
                            (*cfgs_itr).double_buffer, (*cfgs_itr).buffer1);
      USB::EndpointPool::Put(ep);
      ep_index = USB::Endpoint::NextEPNumber(ep_index);
      buffer_offset += (*cfgs_itr).hw_buffer_size1 * 2;
      cfgs_itr++;
    }
    else
    {
      ASSERT(cfgs_itr->buffer1.size_ % 2 == 0);
      ASSERT(cfgs_itr->buffer2.size_ % 2 == 0);

      auto ep_in = new STM32Endpoint(ep_index, id_, hpcd_, USB::Endpoint::Direction::IN,
                                     buffer_offset, (*cfgs_itr).hw_buffer_size1, false,
                                     (*cfgs_itr).buffer1);
      USB::EndpointPool::Put(ep_in);
      buffer_offset += (*cfgs_itr).hw_buffer_size1;
      auto ep_out = new STM32Endpoint(ep_index, id_, hpcd_, USB::Endpoint::Direction::OUT,
                                      buffer_offset, (*cfgs_itr).hw_buffer_size2, false,
                                      (*cfgs_itr).buffer2);
      USB::EndpointPool::Put(ep_out);
      buffer_offset += (*cfgs_itr).hw_buffer_size2;
      ep_index = USB::Endpoint::NextEPNumber(ep_index);
      cfgs_itr++;
    }
  }

  ASSERT(USB::Endpoint::EPNumberToInt8(ep_index) <= hpcd->Init.dev_endpoints);
  ASSERT(buffer_offset <= 512);
}

ErrorCode STM32USBDeviceDevFs::SetAddress(uint8_t address,
                                          USB::DeviceCore::Context context)
{
  HAL_StatusTypeDef ans = HAL_OK;

  if (context == USB::DeviceCore::Context::STATUS_IN)
  {
    ans = HAL_PCD_SetAddress(hpcd_, address);
  }
  return (ans == HAL_OK) ? ErrorCode::OK : ErrorCode::FAILED;
}
#endif

#endif
