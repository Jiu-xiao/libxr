#include "stm32_usb_ep.hpp"

#include "stm32_dcache.hpp"
using namespace LibXR;

#if defined(HAL_PCD_MODULE_ENABLED) && !(defined(USB_OTG_FS) || defined(USB_OTG_HS))

static inline bool is_power_of_two(unsigned int n) { return n > 0 && (n & (n - 1)) == 0; }

namespace
{
bool IsOtg(stm32_usb_dev_id_t) { return false; }
}  // namespace

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

void STM32Endpoint::SetScratch(stm32_usb_dev_id_t id, RawData scratch)
{
  scratch_[id] = scratch;
}

STM32Endpoint* STM32Endpoint::Find(PCD_HandleTypeDef* hpcd, uint8_t epnum, bool is_in)
{
  const auto id = STM32USBDeviceGetID(hpcd);
  const uint8_t num = epnum & 0x7FU;
  const uint8_t dir = is_in ? 1U : 0U;
  if (id == STM32_USB_FS_DEV)
    return num < EP_OTG_FS_MAX_SIZE ? map_fs_[num][dir] : nullptr;
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
  limit = LibXR::min(limit, hw_buffer_size_);
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
  if (is_in)
  {
    ep->xfer_fill_db = 0U;
    ep->xfer_len_db = 0U;
  }
  const auto result = USB_EPStartXfer(hpcd_->Instance, ep);
  if (result != HAL_OK)
  {
    hw_tag_ = 0;
    return ErrorCode::FAILED;
  }
  return ErrorCode::OK;
}


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
  return StopResult::DONE;
}

size_t STM32Endpoint::ReapProgress(const HwRequest& request, bool& retired)
{
  retired = false;
  hw_tag_ = 0;
  if (!IsIn()) return stopped_actual_;
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
  UNUSED(id);
  UNUSED(hpcd);
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
