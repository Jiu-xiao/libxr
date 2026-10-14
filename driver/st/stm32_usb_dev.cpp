#include "stm32_usb_dev.hpp"

#if defined(HAL_PCD_MODULE_ENABLED) && !(defined(USB_OTG_FS) || defined(USB_OTG_HS))

using namespace LibXR;

stm32_usb_dev_id_t STM32USBDeviceGetID(PCD_HandleTypeDef* hpcd)
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

extern "C" void __real_HAL_PCD_IRQHandler(PCD_HandleTypeDef* hpcd);
extern "C" HAL_StatusTypeDef __real_HAL_PCD_Init(PCD_HandleTypeDef* hpcd);

// The link also wraps HAL_PCD_Init for the OTG runtime; FSDEV still initializes through HAL.
extern "C" HAL_StatusTypeDef __wrap_HAL_PCD_Init(PCD_HandleTypeDef* hpcd)
{
  return __real_HAL_PCD_Init(hpcd);
}

// The link wraps HAL_PCD_IRQHandler, so the BSP's unmodified *_it.c enters here. HAL
// callbacks inside the batch only record facts; the owner runs once the batch ends.
extern "C" void __wrap_HAL_PCD_IRQHandler(PCD_HandleTypeDef* hpcd)
{
  const auto id = STM32USBDeviceGetID(hpcd);
  if (id >= STM32_USB_DEV_ID_NUM || STM32USBDevice::map_[id] == nullptr)
  {
    __real_HAL_PCD_IRQHandler(hpcd);
    return;
  }
  USB::EndpointPool::InterruptScope batch(*STM32USBDevice::map_[id]);
  STM32Endpoint::BeforeInterrupt(id, hpcd);
  __real_HAL_PCD_IRQHandler(hpcd);
  STM32Endpoint::AfterInterrupt(id, hpcd);
}

void STM32USBDevice::Stop(bool in_isr)
{
  {
    USB::EndpointPool::HardwareScope hardware(this);
    HAL_PCD_Stop(hpcd_);
    if (map_[id_] == this)
    {
      map_[id_] = nullptr;
    }
  }
  // With the controller stopped no stop confirmation will arrive any more.
  STM32Endpoint::ForceStopAll(id_, hpcd_, in_isr);
}

void STM32USBDevice::OnStopTimeout(bool in_isr)
{
  {
    USB::EndpointPool::HardwareScope hardware(this);
    HAL_PCD_Stop(hpcd_);
  }
  STM32Endpoint::ForceStopAll(id_, hpcd_, in_isr);
  USB::EndpointPool::HardwareScope hardware(this);
  HAL_PCD_Start(hpcd_);
}

void STM32USBDevice::OnStopsSettled()
{
  if (WantsBusTime()) return;
  USB::EndpointPool::HardwareScope hardware(this);
  hpcd_->Instance->CNTR &= ~USB_CNTR_SOFM;
}

void STM32USBDevice::RequireInterruptWrap()
{
  // Read through a volatile pointer so the comparison is made on the linked addresses.
  void (*volatile handler)(PCD_HandleTypeDef*) = &HAL_PCD_IRQHandler;
  REQUIRE(handler == &__wrap_HAL_PCD_IRQHandler);
}

extern "C" void HAL_PCD_SOFCallback(PCD_HandleTypeDef* hpcd)
{
  auto* usb = STM32USBDevice::FindDevice(hpcd);
  if (!usb) return;
  usb->OnSof(true, static_cast<uint16_t>(hpcd->Instance->FNR & 0x7ffU));
}

extern "C" void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef* hpcd)
{
  auto id = STM32USBDeviceGetID(hpcd);

  if (id >= STM32_USB_DEV_ID_NUM)
  {
    return;
  }

  auto usb = STM32USBDevice::map_[id];

  if (!usb)
  {
    return;
  }

  usb->OnSetupPacket(true, reinterpret_cast<USB::SetupPacket*>(hpcd->Setup));
}

extern "C" void HAL_PCD_ResetCallback(PCD_HandleTypeDef* hpcd)
{
  auto* usb = STM32USBDevice::FindDevice(hpcd);
  if (usb) usb->OnBusReset(true);
}

extern "C" void HAL_PCD_SuspendCallback(PCD_HandleTypeDef* hpcd)
{
  auto* usb = STM32USBDevice::FindDevice(hpcd);
  if (usb) usb->OnSuspend(true);
}

extern "C" void HAL_PCD_ResumeCallback(PCD_HandleTypeDef* hpcd)
{
  auto* usb = STM32USBDevice::FindDevice(hpcd);
  if (usb) usb->OnResume(true);
}

extern "C" void HAL_PCD_ConnectCallback(PCD_HandleTypeDef* hpcd) { UNUSED(hpcd); }

extern "C" void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef* hpcd) { UNUSED(hpcd); }

#if defined(USB_BASE) || defined(USB_DRD_FS)
STM32USBDeviceDevFs::STM32USBDeviceDevFs(
    PCD_HandleTypeDef* hpcd, const std::initializer_list<EPConfig> EP_CFGS,
    USB::DeviceDescriptor::PacketSize0 packet_size, uint16_t vid, uint16_t pid,
    uint16_t bcd,
    const std::initializer_list<const USB::DescriptorStrings::LanguagePack*> LANG_LIST,
    const std::initializer_list<const std::initializer_list<USB::ConfigDescriptorItem*>>
        CONFIGS,
    ConstRawData uid)
    : STM32USBDevice(hpcd, STM32_USB_FS_DEV, EP_CFGS.size() * 2, packet_size, vid, pid,
                     bcd, LANG_LIST, CONFIGS, uid)
{
  ASSERT(EP_CFGS.size() > 0 && EP_CFGS.size() <= hpcd->Init.dev_endpoints);

  // Holds the packets one IRQ pass copies for an abandoned OUT request (double
  // buffering can deliver two).
  size_t largest = 0;
  for (const auto& cfg : EP_CFGS)
    largest = LibXR::max(largest, LibXR::max(cfg.hw_buffer_size1, cfg.hw_buffer_size2));
  STM32Endpoint::SetScratch(id_, {new uint8_t[2U * largest], 2U * largest});

  auto cfgs_itr = EP_CFGS.begin();

#if defined(PMA_START_ADDR)
  size_t buffer_offset = PMA_START_ADDR;
#else
  size_t buffer_offset = BTABLE_ADDRESS + hpcd_->Init.dev_endpoints * 8U;  // 字节
#endif

  auto ep0_out = new STM32Endpoint(USB::Endpoint::EPNumber::EP0, id_, hpcd_,
                                   USB::Endpoint::Direction::OUT, buffer_offset,
                                   (*cfgs_itr).hw_buffer_size2, (*cfgs_itr).buffer2);

  buffer_offset += (*cfgs_itr).hw_buffer_size2;

  auto ep0_in = new STM32Endpoint(USB::Endpoint::EPNumber::EP0, id_, hpcd_,
                                  USB::Endpoint::Direction::IN, buffer_offset,
                                  (*cfgs_itr).hw_buffer_size1, (*cfgs_itr).buffer1);

  buffer_offset += (*cfgs_itr).hw_buffer_size1;

  USB::EndpointPool::SetEndpoint0(ep0_in, ep0_out);

  cfgs_itr++;

  USB::Endpoint::EPNumber ep_index = USB::Endpoint::EPNumber::EP1;

  while (cfgs_itr != EP_CFGS.end())
  {
    if (cfgs_itr->hw_buffer_size2 == 0)
    {
      ASSERT(cfgs_itr->buffer1.size_ % 2 == 0);
      auto ep = new STM32Endpoint(
          ep_index, id_, hpcd_,
          cfgs_itr->double_buffer_is_in ? USB::Endpoint::Direction::IN
                                        : USB::Endpoint::Direction::OUT,
          buffer_offset, (*cfgs_itr).hw_buffer_size1, (*cfgs_itr).buffer1);
      USB::EndpointPool::Put(ep);
      ep_index = USB::Endpoint::NextEPNumber(ep_index);
      buffer_offset += (*cfgs_itr).hw_buffer_size1;
      cfgs_itr++;
    }
    else
    {
      ASSERT(cfgs_itr->buffer1.size_ % 2 == 0);
      ASSERT(cfgs_itr->buffer2.size_ % 2 == 0);

      auto ep_in = new STM32Endpoint(ep_index, id_, hpcd_, USB::Endpoint::Direction::IN,
                                     buffer_offset, (*cfgs_itr).hw_buffer_size1,
                                     (*cfgs_itr).buffer1);
      USB::EndpointPool::Put(ep_in);
      buffer_offset += (*cfgs_itr).hw_buffer_size1;
      auto ep_out = new STM32Endpoint(ep_index, id_, hpcd_, USB::Endpoint::Direction::OUT,
                                      buffer_offset, (*cfgs_itr).hw_buffer_size2,
                                      (*cfgs_itr).buffer2);
      USB::EndpointPool::Put(ep_out);
      buffer_offset += (*cfgs_itr).hw_buffer_size2;
      ep_index = USB::Endpoint::NextEPNumber(ep_index);
      cfgs_itr++;
    }
  }

  ASSERT(USB::Endpoint::EPNumberToInt8(ep_index) < hpcd->Init.dev_endpoints);
  ASSERT(buffer_offset <= LIBXR_STM32_USB_PMA_SIZE);
}

ErrorCode STM32USBDeviceDevFs::SetAddress(uint8_t address, USB::ControlContext context)
{
  HAL_StatusTypeDef ans = HAL_OK;

  if (context == USB::ControlContext::STATUS_IN_COMPLETE)
  {
    ans = HAL_PCD_SetAddress(hpcd_, address);
  }
  return (ans == HAL_OK) ? ErrorCode::OK : ErrorCode::FAILED;
}
#endif

#endif
