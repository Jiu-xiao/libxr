#include "stm32_usb_dev.hpp"

#include "stm32_dcache.hpp"
#include "stm32_usb_irq.h"
#if defined(HAL_PCD_MODULE_ENABLED)

using namespace LibXR;

#if defined(USB_BASE) || defined(USB_DRD_FS)
static constexpr uintptr_t STM32USBDevFsInterruptMask()
{
  uintptr_t mask = 0U;
#if defined(USB_CNTR_DDISCM)
  mask |= USB_CNTR_DDISCM;
#endif
#if defined(USB_CNTR_THR512M)
  mask |= USB_CNTR_THR512M;
#endif
#if defined(USB_CNTR_CTRM)
  mask |= USB_CNTR_CTRM;
#endif
#if defined(USB_CNTR_PMAOVRM)
  mask |= USB_CNTR_PMAOVRM;
#endif
#if defined(USB_CNTR_ERRM)
  mask |= USB_CNTR_ERRM;
#endif
#if defined(USB_CNTR_WKUPM)
  mask |= USB_CNTR_WKUPM;
#endif
#if defined(USB_CNTR_SUSPM)
  mask |= USB_CNTR_SUSPM;
#endif
#if defined(USB_CNTR_RESETM)
  mask |= USB_CNTR_RESETM;
#endif
#if defined(USB_CNTR_SOFM)
  mask |= USB_CNTR_SOFM;
#endif
#if defined(USB_CNTR_ESOFM)
  mask |= USB_CNTR_ESOFM;
#endif
#if defined(USB_CNTR_L1REQM)
  mask |= USB_CNTR_L1REQM;
#endif
  return mask;
}
#endif

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

extern "C" void STM32USBDeviceIrqHandler(PCD_HandleTypeDef* hpcd)
{
  const stm32_usb_dev_id_t id = STM32USBDeviceGetID(hpcd);
  if (id < STM32_USB_DEV_ID_NUM && STM32USBDevice::map_[id] != nullptr)
  {
    STM32USBDevice::map_[id]->HandleIrq();
  }
}

void STM32USBDevice::Start(bool)
{
  map_[id_] = this;
  if (HAL_PCD_Start(hpcd_) == HAL_OK)
  {
    desired_interrupt_state_ = ReadInterruptEnableState();
    if (irq_domain_masked_)
    {
#if defined(STM32G0) && defined(USB_UCPD1_2_IRQn)
      if (!hal_irq_active_)
#endif
      {
        WriteInterruptEnableState(0U);
      }
    }
    return;
  }

  desired_interrupt_state_ = 0U;
  WriteInterruptEnableState(0U);
  map_[id_] = nullptr;
}

void STM32USBDevice::Stop(bool)
{
  (void)HAL_PCD_Stop(hpcd_);
  desired_interrupt_state_ = 0U;
  WriteInterruptEnableState(0U);
  if (map_[id_] == this)
  {
    map_[id_] = nullptr;
  }
}

void STM32USBDevice::HandleIrq()
{
  if (map_[id_] != this)
  {
    return;
  }

  (void)RunIrq(
      [this]() noexcept
      {
        ASSERT(!hal_irq_active_);
        hal_irq_active_ = true;
        transfer_callback_barrier_ = HasPendingTransferBarrierInterrupt();
#if defined(STM32G0) && defined(USB_UCPD1_2_IRQn)
        // G0 HAL validates the shared USB/UCPD line before dispatch. The vector is
        // already active here, so expose the owned USB mask only for the HAL call.
        WriteInterruptEnableState(desired_interrupt_state_);
#endif
        HAL_PCD_IRQHandler(hpcd_);
#if defined(STM32G0) && defined(USB_UCPD1_2_IRQn)
        desired_interrupt_state_ = ReadInterruptEnableState();
        WriteInterruptEnableState(0U);
#endif
        transfer_callback_barrier_ = false;
        hal_irq_active_ = false;
      },
      true);
}

void STM32USBDevice::LockInterruptDomain(void* context) noexcept
{
  auto* self = static_cast<STM32USBDevice*>(context);
  self->admission_primask_ = __get_PRIMASK();
  __disable_irq();
}

void STM32USBDevice::UnlockInterruptDomain(void* context) noexcept
{
  auto* self = static_cast<STM32USBDevice*>(context);
  __set_PRIMASK(self->admission_primask_);
}

uintptr_t STM32USBDevice::MaskInterruptDomain(void* context) noexcept
{
  auto* self = static_cast<STM32USBDevice*>(context);
  ASSERT(!self->irq_domain_masked_);
  const uintptr_t saved_state = self->ReadInterruptEnableState();
  self->desired_interrupt_state_ = saved_state;
  self->irq_domain_masked_ = true;
  self->WriteInterruptEnableState(0U);
  return saved_state;
}

void STM32USBDevice::RestoreInterruptDomain(void* context, uintptr_t saved_state) noexcept
{
  auto* self = static_cast<STM32USBDevice*>(context);
  ASSERT(self->irq_domain_masked_);
  UNUSED(saved_state);
  self->irq_domain_masked_ = false;
  self->WriteInterruptEnableState(self->desired_interrupt_state_);
}

bool STM32USBDevice::IsOtgDevice() const noexcept
{
#if defined(USB_OTG_FS)
  if (id_ == STM32_USB_OTG_FS)
  {
    return true;
  }
#endif
#if defined(USB_OTG_HS)
  if (id_ == STM32_USB_OTG_HS)
  {
    return true;
  }
#endif
  return false;
}

uintptr_t STM32USBDevice::ReadInterruptEnableState() const noexcept
{
#if defined(USB_OTG_FS) || defined(USB_OTG_HS)
  if (IsOtgDevice())
  {
    return hpcd_->Instance->GAHBCFG & USB_OTG_GAHBCFG_GINT;
  }
#endif
#if defined(USB_BASE) || defined(USB_DRD_FS)
  return hpcd_->Instance->CNTR & STM32USBDevFsInterruptMask();
#else
  return 0U;
#endif
}

void STM32USBDevice::WriteInterruptEnableState(uintptr_t state) noexcept
{
#if defined(USB_OTG_FS) || defined(USB_OTG_HS)
  if (IsOtgDevice())
  {
    if ((state & USB_OTG_GAHBCFG_GINT) != 0U)
    {
      hpcd_->Instance->GAHBCFG |= USB_OTG_GAHBCFG_GINT;
    }
    else
    {
      hpcd_->Instance->GAHBCFG &= ~USB_OTG_GAHBCFG_GINT;
    }
    return;
  }
#endif
#if defined(USB_BASE) || defined(USB_DRD_FS)
  const uintptr_t mask = STM32USBDevFsInterruptMask();
  hpcd_->Instance->CNTR = (hpcd_->Instance->CNTR & ~mask) | (state & mask);
#else
  UNUSED(state);
#endif
}

bool STM32USBDevice::HasPendingTransferBarrierInterrupt() const noexcept
{
#if defined(USB_OTG_FS) || defined(USB_OTG_HS)
  if (IsOtgDevice())
  {
    if ((desired_interrupt_state_ & USB_OTG_GAHBCFG_GINT) == 0U)
    {
      return false;
    }
    const uint32_t pending = hpcd_->Instance->GINTSTS & hpcd_->Instance->GINTMSK;
    uint32_t lifecycle_mask = 0U;
#if defined(USB_OTG_GINTSTS_USBRST)
    lifecycle_mask |= USB_OTG_GINTSTS_USBRST;
#endif
#if defined(USB_OTG_GINTSTS_ENUMDNE)
    lifecycle_mask |= USB_OTG_GINTSTS_ENUMDNE;
#endif
    return (pending & lifecycle_mask) != 0U;
  }
#endif
#if defined(USB_BASE) || defined(USB_DRD_FS)
  const uint32_t pending = hpcd_->Instance->ISTR;
  const bool reset_pending = (desired_interrupt_state_ & USB_CNTR_RESETM) != 0U &&
                             (pending & USB_ISTR_RESET) != 0U;
  return reset_pending;
#else
  return false;
#endif
}

extern "C" void HAL_PCD_SOFCallback(PCD_HandleTypeDef* hpcd) { UNUSED(hpcd); }

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

  ASSERT(usb->IsHalIrqActive());
  if (!usb->AcceptTransferCallback())
  {
    return;
  }

  if (STM32USBUsesDma(hpcd))
  {
    STM32_InvalidateDCacheByAddr(hpcd->Setup, sizeof(USB::SetupPacket));
  }

  usb->GetEndpoint0In()->SetState(USB::Endpoint::State::IDLE);
  usb->GetEndpoint0Out()->SetState(USB::Endpoint::State::IDLE);

  usb->OnSetupPacket(true, reinterpret_cast<USB::SetupPacket*>(hpcd->Setup));
}

extern "C" void HAL_PCD_ResetCallback(PCD_HandleTypeDef* hpcd)
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

  ASSERT(usb->IsHalIrqActive());
  if (!usb->IsHalIrqActive())
  {
    return;
  }

  usb->MarkTransferCallbackBarrier();
  usb->Deinit(true);
  usb->Init(true);
}

extern "C" void HAL_PCD_SuspendCallback(PCD_HandleTypeDef* hpcd)
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

  ASSERT(usb->IsHalIrqActive());
  if (!usb->IsHalIrqActive())
  {
    return;
  }

  // Suspend preserves the current USB configuration and endpoint transfers. A normal
  // resume does not include another SET_CONFIGURATION request.
}

extern "C" void HAL_PCD_ResumeCallback(PCD_HandleTypeDef* hpcd)
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

  ASSERT(usb->IsHalIrqActive());
  if (!usb->IsHalIrqActive())
  {
    return;
  }
  // Resume continues the generation that was active before suspend.
}

extern "C" void HAL_PCD_ConnectCallback(PCD_HandleTypeDef* hpcd) { UNUSED(hpcd); }

extern "C" void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef* hpcd) { UNUSED(hpcd); }

#if (defined(USB_OTG_FS))

STM32USBDeviceOtgFS::STM32USBDeviceOtgFS(
    PCD_HandleTypeDef* hpcd, size_t rx_fifo_size,
    const std::initializer_list<LibXR::RawData> RX_EP_CFGS,
    const std::initializer_list<EPInConfig> TX_EP_CFGS,
    USB::DeviceDescriptor::PacketSize0 packet_size, uint16_t vid, uint16_t pid,
    uint16_t bcd,
    const std::initializer_list<const USB::DescriptorStrings::LanguagePack*> LANG_LIST,
    const std::initializer_list<const std::initializer_list<USB::ConfigDescriptorItem*>>
        CONFIGS,
    ConstRawData uid)
    : STM32USBDevice(hpcd, STM32_USB_OTG_FS, RX_EP_CFGS.size() + TX_EP_CFGS.size(),
                     packet_size, vid, pid, bcd, LANG_LIST, CONFIGS, uid)
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

  if (context == USB::DeviceCore::Context::STATUS_IN_ARMED)
  {
    ans = HAL_PCD_SetAddress(hpcd_, address);
  }
  return (ans == HAL_OK) ? ErrorCode::OK : ErrorCode::FAILED;
}

#endif

#if (defined(USB_OTG_HS))

STM32USBDeviceOtgHS::STM32USBDeviceOtgHS(
    PCD_HandleTypeDef* hpcd, size_t rx_fifo_size,
    const std::initializer_list<LibXR::RawData> RX_EP_CFGS,
    const std::initializer_list<EPInConfig> TX_EP_CFGS,
    USB::DeviceDescriptor::PacketSize0 packet_size, uint16_t vid, uint16_t pid,
    uint16_t bcd,
    const std::initializer_list<const USB::DescriptorStrings::LanguagePack*> LANG_LIST,
    const std::initializer_list<const std::initializer_list<USB::ConfigDescriptorItem*>>
        CONFIGS,
    ConstRawData uid)
    : STM32USBDevice(
          hpcd, STM32_USB_OTG_HS, RX_EP_CFGS.size() + TX_EP_CFGS.size(), packet_size, vid,
          pid, bcd, LANG_LIST, CONFIGS, uid,
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

  if (context == USB::DeviceCore::Context::STATUS_IN_ARMED)
  {
    ans = HAL_PCD_SetAddress(hpcd_, address);
  }
  return (ans == HAL_OK) ? ErrorCode::OK : ErrorCode::FAILED;
}

#endif

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

ErrorCode STM32USBDeviceDevFs::SetAddress(uint8_t address,
                                          USB::DeviceCore::Context context)
{
  HAL_StatusTypeDef ans = HAL_OK;

  if (context == USB::DeviceCore::Context::STATUS_IN_COMPLETE)
  {
    ans = HAL_PCD_SetAddress(hpcd_, address);
  }
  return (ans == HAL_OK) ? ErrorCode::OK : ErrorCode::FAILED;
}
#endif

#endif
