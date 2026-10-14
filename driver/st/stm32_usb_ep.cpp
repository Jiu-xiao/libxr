#include "stm32_usb_ep.hpp"

#include "stm32_dcache.hpp"
using namespace LibXR;

#if defined(HAL_PCD_MODULE_ENABLED)

static inline bool is_power_of_two(unsigned int n) { return n > 0 && (n & (n - 1)) == 0; }

#if defined(USB_OTG_FS) || defined(USB_OTG_HS)
namespace
{
USB_OTG_INEndpointTypeDef* InEndpoint(PCD_HandleTypeDef* hpcd, uint8_t num)
{
  const uintptr_t base = reinterpret_cast<uintptr_t>(hpcd->Instance);
  return reinterpret_cast<USB_OTG_INEndpointTypeDef*>(base + USB_OTG_IN_ENDPOINT_BASE +
                                                      num * USB_OTG_EP_REG_SIZE);
}

USB_OTG_OUTEndpointTypeDef* OutEndpoint(PCD_HandleTypeDef* hpcd, uint8_t num)
{
  const uintptr_t base = reinterpret_cast<uintptr_t>(hpcd->Instance);
  return reinterpret_cast<USB_OTG_OUTEndpointTypeDef*>(base + USB_OTG_OUT_ENDPOINT_BASE +
                                                       num * USB_OTG_EP_REG_SIZE);
}

USB_OTG_DeviceTypeDef* Device(PCD_HandleTypeDef* hpcd)
{
  return reinterpret_cast<USB_OTG_DeviceTypeDef*>(
      reinterpret_cast<uintptr_t>(hpcd->Instance) + USB_OTG_DEVICE_BASE);
}

bool IsOtg(stm32_usb_dev_id_t id)
{
  bool otg = false;
#if defined(USB_OTG_FS)
  otg = otg || id == STM32_USB_OTG_FS;
#endif
#if defined(USB_OTG_HS)
  otg = otg || id == STM32_USB_OTG_HS;
#endif
  return otg;
}
}  // namespace

STM32Endpoint::STM32Endpoint(EPNumber ep_num, stm32_usb_dev_id_t id,
                             PCD_HandleTypeDef* hpcd, Direction dir, size_t fifo_size,
                             LibXR::RawData buffer)
    : Endpoint(ep_num, dir, buffer), hpcd_(hpcd), id_(id), fifo_size_(fifo_size)
{
  ASSERT(fifo_size >= 8);
  ASSERT(is_power_of_two(fifo_size) || fifo_size % 4 == 0);

#if defined(USB_OTG_HS)
  if (id == STM32_USB_OTG_HS)
  {
    map_otg_hs_[EPNumberToInt8(GetNumber())][static_cast<uint8_t>(dir)] = this;
  }
#endif
#if defined(USB_OTG_FS)
  if (id == STM32_USB_OTG_FS)
  {
    map_otg_fs_[EPNumberToInt8(GetNumber())][static_cast<uint8_t>(dir)] = this;
  }
#endif

  if (dir == Direction::IN)
  {
    HAL_PCDEx_SetTxFiFo(hpcd_, EPNumberToInt8(GetNumber()), fifo_size / 4);
  }
  else if (dir == Direction::OUT && ep_num == USB::Endpoint::EPNumber::EP0)
  {
    HAL_PCDEx_SetRxFiFo(hpcd_, fifo_size / 4);
  }
}
#else
namespace
{
bool IsOtg(stm32_usb_dev_id_t) { return false; }
}  // namespace
#endif

#if defined(USB_BASE) || defined(USB_DRD_FS)
STM32Endpoint::STM32Endpoint(EPNumber ep_num, stm32_usb_dev_id_t id,
                             PCD_HandleTypeDef* hpcd, Direction dir,
                             size_t hw_buffer_offset, size_t hw_buffer_size,
                             LibXR::RawData buffer)
    : Endpoint(ep_num, dir, buffer), hpcd_(hpcd), id_(id), hw_buffer_size_(hw_buffer_size)
{
  ASSERT(hw_buffer_size >= 8);
  ASSERT(is_power_of_two(hw_buffer_size));

  map_fs_[EPNumberToInt8(GetNumber())][static_cast<uint8_t>(dir)] = this;

  HAL_PCDEx_PMAConfig(hpcd_, EPNumberToAddr(GetNumber(), dir), PCD_SNG_BUF,
                      hw_buffer_offset);
}
#endif

void STM32Endpoint::SetScratch(stm32_usb_dev_id_t id, RawData scratch)
{
  scratch_[id] = scratch;
}

STM32Endpoint* STM32Endpoint::Find(PCD_HandleTypeDef* hpcd, uint8_t epnum, bool is_in)
{
  const auto id = STM32USBDeviceGetID(hpcd);
  const uint8_t num = epnum & 0x7FU;
  const uint8_t dir = is_in ? 1U : 0U;
#if defined(USB_OTG_HS)
  if (id == STM32_USB_OTG_HS)
    return num < EP_OTG_HS_MAX_SIZE ? map_otg_hs_[num][dir] : nullptr;
#endif
#if defined(USB_OTG_FS)
  if (id == STM32_USB_OTG_FS)
    return num < EP_OTG_FS_MAX_SIZE ? map_otg_fs_[num][dir] : nullptr;
#endif
#if defined(USB_BASE) || defined(USB_DRD_FS)
  if (id == STM32_USB_FS_DEV)
    return num < EP_OTG_FS_MAX_SIZE ? map_fs_[num][dir] : nullptr;
#endif
  return nullptr;
}

uint8_t STM32Endpoint::HalAddress() const
{
  return EPNumberToAddr(GetNumber(), GetConfig().direction);
}

PCD_EPTypeDef* STM32Endpoint::HalEndpoint() const
{
  const uint8_t num = EPNumberToInt8(GetNumber());
  return IsIn() ? &hpcd_->IN_ep[num] : &hpcd_->OUT_ep[num];
}

size_t STM32Endpoint::MaxPacketSizeLimit(Type type) const
{
  size_t limit = type == Type::CONTROL ? 64U : UINT16_MAX;
#if defined(USB_OTG_FS) || defined(USB_OTG_HS)
  limit = LibXR::min(limit, fifo_size_);
#endif
#if defined(USB_BASE) || defined(USB_DRD_FS)
  limit = LibXR::min(limit, hw_buffer_size_);
#endif
  return limit;
}

ErrorCode STM32Endpoint::ConfigureHardware(const Config& cfg, Granted& granted)
{
  UNUSED(granted);
  hw_tag_ = 0;
  SetStopStage(StopStage::NONE);
  const uint16_t packet = cfg.max_packet_size < 8U ? 8U : cfg.max_packet_size;
  return HAL_PCD_EP_Open(hpcd_, HalAddress(), packet, static_cast<uint8_t>(cfg.type)) ==
                 HAL_OK
             ? ErrorCode::OK
             : ErrorCode::FAILED;
}

void STM32Endpoint::CloseHardware(bool in_isr)
{
  UNUSED(in_isr);
  hw_tag_ = 0;
  SetStopStage(StopStage::NONE);
  (void)HAL_PCD_EP_Close(hpcd_, HalAddress());
}

ErrorCode STM32Endpoint::StartHardware(const HwRequest& request, bool in_isr)
{
  UNUSED(in_isr);
  const bool is_in = IsIn();
  PCD_EPTypeDef* ep = HalEndpoint();
  SetRedirected(false);
  stopped_actual_ = 0;
  hw_tag_ = request.tag;
  hw_length_ = request.length;
  hw_slot_ = request.slot;
  if (STM32USBUsesDma(hpcd_))
  {
    // Clean before an OUT DMA as well, so dirty CPU lines cannot later be evicted over
    // received bytes. Invalidation follows the real completion.
    STM32_CleanDCacheByAddr(request.slot.addr_,
                            is_in ? request.length : request.slot.size_);
  }
  ep->xfer_buff = static_cast<uint8_t*>(request.slot.addr_);
  ep->xfer_len = request.length;
  ep->xfer_count = 0U;
  ep->is_in = is_in ? 1U : 0U;
  ep->num = EPNumberToInt8(GetNumber());
#if defined(USB_OTG_FS) || defined(USB_OTG_HS)
  if (hpcd_->Init.dma_enable == 1U)
    ep->dma_addr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ep->xfer_buff));
  const auto result = USB_EPStartXfer(hpcd_->Instance, ep, hpcd_->Init.dma_enable);
  if (result == HAL_OK && is_in && hpcd_->Init.dma_enable == 0U && ep->num != 0U &&
      request.length != 0U && GetConfig().type != Type::ISOCHRONOUS)
    WriteFirstPacket(ep);
#else
  if (is_in)
  {
    ep->xfer_fill_db = 0U;
    ep->xfer_len_db = 0U;
  }
  const auto result = USB_EPStartXfer(hpcd_->Instance, ep);
#endif
  if (result != HAL_OK)
  {
    hw_tag_ = 0;
    return ErrorCode::FAILED;
  }
  return ErrorCode::OK;
}

#if defined(USB_OTG_FS) || defined(USB_OTG_HS)
void STM32Endpoint::WriteFirstPacket(PCD_EPTypeDef* ep)
{
  // The first packet goes to the TxFIFO now instead of in the next TXFE interrupt, which
  // could only run after the rest of this owner pass. HAL's TXFE handling continues
  // with the remaining packets from xfer_buff/xfer_count.
  const uint32_t length = LibXR::min(ep->xfer_len - ep->xfer_count, ep->maxpacket);
  auto* in_ep = InEndpoint(hpcd_, ep->num);
  if ((in_ep->DTXFSTS & USB_OTG_DTXFSTS_INEPTFSAV) < (length + 3U) / 4U) return;
  (void)USB_WritePacket(hpcd_->Instance, ep->xfer_buff, ep->num,
                        static_cast<uint16_t>(length), 0U);
  ep->xfer_buff += length;
  ep->xfer_count += length;
  if (ep->xfer_count >= ep->xfer_len) Device(hpcd_)->DIEPEMPMSK &= ~(1UL << ep->num);
}
#endif

void STM32Endpoint::Redirect()
{
  PCD_EPTypeDef* ep = HalEndpoint();
  RawData scratch = scratch_[id_];
  REQUIRE(scratch.addr_ != nullptr);
  ep->xfer_buff = static_cast<uint8_t*>(scratch.addr_);
  SetRedirected(true);
}

USB::Endpoint::StopResult STM32Endpoint::StopHardware(bool in_isr)
{
  UNUSED(in_isr);
  const uint8_t num = EPNumberToInt8(GetNumber());
  PCD_EPTypeDef* ep = HalEndpoint();
#if defined(USB_OTG_FS) || defined(USB_OTG_HS)
  if (IsOtg(id_))
  {
    auto* device = Device(hpcd_);
    if (IsIn())
    {
      auto* in_ep = InEndpoint(hpcd_, num);
      if (hpcd_->Init.dma_enable == 0U)
      {
        // HAL refills the TxFIFO from the slot only on TXFE, and only below xfer_len.
        device->DIEPEMPMSK &= ~(1UL << num);
        ep->xfer_len = ep->xfer_count;
      }
      if ((in_ep->DIEPCTL & USB_OTG_DIEPCTL_EPENA) == 0U)
        return StopResult::DONE;  // Finished; its XFRC, if pending, is a late fact.
      // Reference-manual order: SNAK, wait for NAK effective, then EPDIS and EPDISD.
      // Both waits are interrupts (INEPNE, EPDISD); AfterInterrupt advances them.
      device->DIEPMSK |= USB_OTG_DIEPMSK_INEPNEM;
      in_ep->DIEPCTL |= USB_OTG_DIEPCTL_SNAK;
      SetStopStage(StopStage::IN_NAK);
      ArmStopTimeout();
      return StopResult::PENDING;
    }
    auto* out_ep = OutEndpoint(hpcd_, num);
    if (num != 0U && (out_ep->DOEPCTL & USB_OTG_DOEPCTL_EPENA) != 0U)
    {
      // An OUT endpoint is disabled under global OUT NAK: SGONAK, wait for GONAKEFF,
      // EPDIS, wait for EPDISD (HAL then clears the global NAK).
      if ((hpcd_->Instance->GINTSTS & USB_OTG_GINTSTS_BOUTNAKEFF) != 0U)
      {
        out_ep->DOEPCTL |= USB_OTG_DOEPCTL_SNAK | USB_OTG_DOEPCTL_EPDIS;
        SetStopStage(StopStage::OUT_DISABLE);
      }
      else
      {
        hpcd_->Instance->GINTMSK |= USB_OTG_GINTMSK_GONAKEFFM;
        device->DCTL |= USB_OTG_DCTL_SGONAK;
        SetStopStage(StopStage::OUT_GLOBAL_NAK);
      }
      ArmStopTimeout();
      return StopResult::PENDING;
    }
    // EP0 OUT cannot be disabled, and a finished OUT endpoint has nothing to stop.
    // Without DMA the IRQ copies each packet to xfer_buff: point it at the scratch area
    // and NAK further packets; the slot is never written again.
    out_ep->DOEPCTL |= USB_OTG_DOEPCTL_SNAK;
    stopped_actual_ = ep->xfer_count;
    Redirect();
    return StopResult::DONE;
  }
#endif
#if defined(USB_BASE) || defined(USB_DRD_FS)
  if (IsIn())
  {
    PCD_SET_EP_TX_STATUS(hpcd_->Instance, num, USB_EP_TX_NAK);
    ep->xfer_len = 0U;  // No further PMA refill from the slot.
  }
  else
  {
    PCD_SET_EP_RX_STATUS(hpcd_->Instance, num, USB_EP_RX_NAK);
    stopped_actual_ = ep->xfer_count;
    Redirect();
  }
#else
  UNUSED(num);
  UNUSED(ep);
#endif
  return StopResult::DONE;
}

size_t STM32Endpoint::ReapProgress(const HwRequest& request, bool& retired)
{
  retired = false;
  hw_tag_ = 0;
  if (!IsIn()) return stopped_actual_;
#if defined(USB_OTG_FS) || defined(USB_OTG_HS)
  if (IsOtg(id_))
  {
    auto* in_ep = InEndpoint(hpcd_, EPNumberToInt8(GetNumber()));
    // Only a transfer complete not yet handled by HAL proves the host took everything.
    retired = (in_ep->DIEPINT & USB_OTG_DIEPINT_XFRC) != 0U;
    if (retired) return request.length;
    const uint32_t remaining =
        (in_ep->DIEPTSIZ & USB_OTG_DIEPTSIZ_PKTCNT) >> USB_OTG_DIEPTSIZ_PKTCNT_Pos;
    const size_t packet = MaxPacketSize();
    const size_t total =
        request.length == 0U ? 1U : (request.length + packet - 1U) / packet;
    // PKTCNT drops when a packet leaves the FIFO; the last one may lack the host's ACK.
    size_t sent = total > remaining ? total - remaining : 0U;
    if (sent != 0U) --sent;
    return LibXR::min(sent * packet, request.length);
  }
#endif
  UNUSED(request);
  return 0U;
}

ErrorCode STM32Endpoint::StallHardware(bool in_isr)
{
  UNUSED(in_isr);
  return HAL_PCD_EP_SetStall(hpcd_, HalAddress()) == HAL_OK ? ErrorCode::OK
                                                            : ErrorCode::FAILED;
}

ErrorCode STM32Endpoint::ClearStallHardware(bool in_isr)
{
  UNUSED(in_isr);
  if (GetNumber() == EPNumber::EP0) return ErrorCode::OK;  // Cleared by the next SETUP.
  return HAL_PCD_EP_ClrStall(hpcd_, HalAddress()) == HAL_OK ? ErrorCode::OK
                                                            : ErrorCode::FAILED;
}

void STM32Endpoint::OnTransferDone(ErrorCode result)
{
  const uint32_t tag = hw_tag_;
  if (tag == 0U) return;  // Stopped, or a duplicate.
  hw_tag_ = 0;
  size_t actual = hw_length_;
  if (!IsIn())
  {
    actual = HalEndpoint()->xfer_count;
    if (STM32USBUsesDma(hpcd_)) STM32_InvalidateDCacheByAddr(hw_slot_.addr_, actual);
  }
  if (result != ErrorCode::OK) actual = 0U;
  OnHardwareComplete(true, tag, actual, result);
}

void STM32Endpoint::SetStopStage(StopStage stage)
{
  stop_stage_ = stage;
  if (stage != StopStage::NONE)
    stop_mask_[id_] |= MaskBit();
  else
    stop_mask_[id_] &= ~MaskBit();
}

void STM32Endpoint::SetRedirected(bool redirected)
{
  redirected_ = redirected;
  if (redirected)
    redirect_mask_[id_] |= MaskBit();
  else
    redirect_mask_[id_] &= ~MaskBit();
}

void STM32Endpoint::VisitRedirected(stm32_usb_dev_id_t id, PCD_HandleTypeDef* hpcd)
{
  // One HAL pass copies at most one received packet per endpoint; keep abandoned
  // endpoints writing at the start of the scratch area.
  for (uint32_t bits = redirect_mask_[id], bit = 0; bits != 0U; ++bit, bits >>= 1U)
  {
    if ((bits & 1U) == 0U) continue;
    STM32Endpoint* ep = Find(hpcd, static_cast<uint8_t>(bit & 15U), bit >= 16U);
    if (ep != nullptr)
      ep->HalEndpoint()->xfer_buff = static_cast<uint8_t*>(scratch_[id].addr_);
  }
}

void STM32Endpoint::ArmStopTimeout()
{
#if defined(USB_OTG_FS) || defined(USB_OTG_HS)
  // SOF counts the frames a stop has been waiting (escalation in DeviceCore).
  hpcd_->Instance->GINTMSK |= USB_OTG_GINTMSK_SOFM;
#endif
}

void STM32Endpoint::ConfirmStop(bool in_isr)
{
  if (!IsIn())
  {
    // Packets still in the RxFIFO for this endpoint go to the scratch area.
    stopped_actual_ = HalEndpoint()->xfer_count;
    Redirect();
  }
  SetStopStage(StopStage::NONE);
  OnHardwareStopped(in_isr);
}

void STM32Endpoint::VisitStopping(stm32_usb_dev_id_t id, PCD_HandleTypeDef* hpcd)
{
#if defined(USB_OTG_FS) || defined(USB_OTG_HS)
  const bool global_nak = (hpcd->Instance->GINTSTS & USB_OTG_GINTSTS_BOUTNAKEFF) != 0U;
  bool out_waiting = false;
  for (uint32_t bits = stop_mask_[id], bit = 0; bits != 0U; ++bit, bits >>= 1U)
  {
    if ((bits & 1U) == 0U) continue;
    const uint8_t num = static_cast<uint8_t>(bit & 15U);
    STM32Endpoint* ep = Find(hpcd, num, bit >= 16U);
    if (ep == nullptr) continue;
    auto* in_ep = InEndpoint(hpcd, num);
    auto* out_ep = OutEndpoint(hpcd, num);
    switch (ep->stop_stage_)
    {
      case StopStage::IN_NAK:
        if ((in_ep->DIEPCTL & USB_OTG_DIEPCTL_EPENA) == 0U)
          ep->ConfirmStop(true);  // Completed before the NAK; the FIFO is empty.
        else if ((in_ep->DIEPCTL & USB_OTG_DIEPCTL_NAKSTS) != 0U)
        {
          in_ep->DIEPCTL |= USB_OTG_DIEPCTL_SNAK | USB_OTG_DIEPCTL_EPDIS;
          ep->SetStopStage(StopStage::IN_DISABLE);
        }
        break;
      case StopStage::IN_DISABLE:
        // HAL flushed the TxFIFO while handling EPDISD in this pass.
        if ((in_ep->DIEPCTL & USB_OTG_DIEPCTL_EPENA) == 0U &&
            (in_ep->DIEPINT & USB_OTG_DIEPINT_EPDISD) == 0U)
          ep->ConfirmStop(true);
        break;
      case StopStage::OUT_GLOBAL_NAK:
        if ((out_ep->DOEPCTL & USB_OTG_DOEPCTL_EPENA) == 0U)
          ep->ConfirmStop(true);
        else if (global_nak)
        {
          out_ep->DOEPCTL |= USB_OTG_DOEPCTL_SNAK | USB_OTG_DOEPCTL_EPDIS;
          ep->SetStopStage(StopStage::OUT_DISABLE);
          out_waiting = true;
        }
        else
          out_waiting = true;
        break;
      case StopStage::OUT_DISABLE:
        if ((out_ep->DOEPCTL & USB_OTG_DOEPCTL_EPENA) == 0U &&
            (out_ep->DOEPINT & USB_OTG_DOEPINT_EPDISD) == 0U)
          ep->ConfirmStop(true);
        else
          out_waiting = true;
        break;
      case StopStage::NONE:
        break;
    }
  }
  // HAL clears the global OUT NAK on EPDISD; clear it here if no disable is pending.
  if (!out_waiting && (hpcd->Instance->GINTSTS & USB_OTG_GINTSTS_BOUTNAKEFF) != 0U)
    Device(hpcd)->DCTL |= USB_OTG_DCTL_CGONAK;
#else
  UNUSED(id);
  UNUSED(hpcd);
#endif
}

void STM32Endpoint::ForceStopAll(stm32_usb_dev_id_t id, PCD_HandleTypeDef* hpcd,
                                 bool in_isr)
{
  // The controller has been stopped: nothing touches a slot any more.
  for (uint32_t bits = stop_mask_[id], bit = 0; bits != 0U; ++bit, bits >>= 1U)
  {
    if ((bits & 1U) == 0U) continue;
    STM32Endpoint* ep = Find(hpcd, static_cast<uint8_t>(bit & 15U), bit >= 16U);
    if (ep != nullptr) ep->ConfirmStop(in_isr);
  }
  stop_mask_[id] = 0U;
}

// --- HAL C callbacks ---

extern "C" void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef* hpcd, uint8_t epnum)
{
  if (auto* ep = STM32Endpoint::Find(hpcd, epnum, true))
    ep->OnTransferDone(ErrorCode::OK);
}

extern "C" void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef* hpcd, uint8_t epnum)
{
  if (auto* ep = STM32Endpoint::Find(hpcd, epnum, false))
    ep->OnTransferDone(ErrorCode::OK);
}

extern "C" void HAL_PCD_ISOINIncompleteCallback(PCD_HandleTypeDef* hpcd, uint8_t epnum)
{
  if (auto* ep = STM32Endpoint::Find(hpcd, epnum, true))
    ep->OnTransferDone(ErrorCode::FAILED);
}

extern "C" void HAL_PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef* hpcd, uint8_t epnum)
{
  if (auto* ep = STM32Endpoint::Find(hpcd, epnum, false))
    ep->OnTransferDone(ErrorCode::FAILED);
}
#endif
