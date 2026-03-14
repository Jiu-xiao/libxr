#include "esp_usb_ep.hpp"

#if SOC_USB_OTG_SUPPORTED && defined(CONFIG_IDF_TARGET_ESP32S3) && \
    CONFIG_IDF_TARGET_ESP32S3

#include <algorithm>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_usb.hpp"
#include "esp_usb_dev.hpp"

namespace Detail = LibXR::ESPUSBDetail;

namespace LibXR
{

ESP32USBEndpoint::ESP32USBEndpoint(ESP32USBDevice& device, EPNumber number,
                                   Direction direction, RawData buffer)
    : USB::Endpoint(number, direction, buffer), device_(device)
{
}

void ESP32USBEndpoint::Configure(const Config& cfg)
{
  ASSERT(cfg.direction == Direction::IN || cfg.direction == Direction::OUT);

  auto& ep_cfg = GetConfig();
  ep_cfg = cfg;
  ep_cfg.max_packet_size = Detail::ClampPacketSize(cfg.type, cfg.max_packet_size);
  if (ep_cfg.max_packet_size == 0U)
  {
    ep_cfg.max_packet_size = (cfg.type == Type::ISOCHRONOUS) ? 1023U : 64U;
  }

  auto buffer = GetBuffer();
  if (buffer.size_ < ep_cfg.max_packet_size)
  {
    ep_cfg.max_packet_size = static_cast<uint16_t>(buffer.size_);
  }

  ASSERT(ep_cfg.max_packet_size > 0U);

  if (device_.DmaEnabled() && (buffer.size_ > 0U) && !EnsureDmaShadow(buffer.size_))
  {
    SetState(State::ERROR);
    return;
  }

  if (cfg.direction == Direction::IN)
  {
    const bool is_bulk = (cfg.type == Type::BULK);
    if (!fifo_allocated_ &&
        !device_.AllocateTxFifo(EPNumberToInt8(GetNumber()), ep_cfg.max_packet_size,
                                is_bulk, fifo_words_))
    {
      SetState(State::ERROR);
      return;
    }
    fifo_allocated_ = true;
  }
  else
  {
    if (!device_.EnsureRxFifo(ep_cfg.max_packet_size))
    {
      SetState(State::ERROR);
      return;
    }
  }

  ActivateHardwareEndpoint();
  ResetTransferState();
  SetState(State::IDLE);
}

void ESP32USBEndpoint::Close()
{
  auto* dev = reinterpret_cast<usb_dwc_dev_t*>(Detail::kDwc2FsRegBase);
  const uint8_t ep_num = EPNumberToInt8(GetNumber());

  if (GetDirection() == Direction::IN)
  {
    if (ep_num == 0U)
    {
      if (dev->diepctl0_reg.epena)
      {
        Detail::DisableInEndpointAndWait(dev);
      }
    }
    else
    {
      dev->diepempmsk_reg.val &= ~(1UL << ep_num);
      if (dev->in_eps[ep_num - 1U].diepctl_reg.epena)
      {
        Detail::DisableInEndpointAndWait(dev, ep_num);
      }
    }
    dev->daintmsk_reg.val &= ~(1UL << ep_num);
  }
  else
  {
    if (ep_num == 0U)
    {
      Detail::DisableOutEndpointAndWait(dev->doepctl0_reg);
    }
    else
    {
      Detail::DisableOutEndpointAndWait(dev->out_eps[ep_num - 1U].doepctl_reg);
    }
    dev->daintmsk_reg.val &= ~(1UL << (16U + ep_num));
  }

  ResetTransferState();
  SetState(State::DISABLED);
}

ErrorCode ESP32USBEndpoint::Stall()
{
  if (GetState() == State::BUSY)
  {
    return ErrorCode::BUSY;
  }

  auto* dev = reinterpret_cast<usb_dwc_dev_t*>(Detail::kDwc2FsRegBase);
  const uint8_t ep_num = EPNumberToInt8(GetNumber());

  if (GetDirection() == Direction::IN)
  {
    if (ep_num == 0U)
    {
      dev->diepctl0_reg.stall = 1;
    }
    else
    {
      dev->in_eps[ep_num - 1U].diepctl_reg.stall = 1;
    }
  }
  else
  {
    if (ep_num == 0U)
    {
      dev->doepctl0_reg.stall = 1;
    }
    else
    {
      dev->out_eps[ep_num - 1U].doepctl_reg.stall = 1;
    }
  }

  SetState(State::STALLED);
  return ErrorCode::OK;
}

ErrorCode ESP32USBEndpoint::ClearStall()
{
  auto* dev = reinterpret_cast<usb_dwc_dev_t*>(Detail::kDwc2FsRegBase);
  const uint8_t ep_num = EPNumberToInt8(GetNumber());

  if (GetDirection() == Direction::IN)
  {
    if (ep_num == 0U)
    {
      dev->diepctl0_reg.stall = 0;
    }
    else
    {
      auto& reg = dev->in_eps[ep_num - 1U].diepctl_reg;
      reg.stall = 0;
      reg.setd0pid = 1;
    }
  }
  else
  {
    if (ep_num == 0U)
    {
      dev->doepctl0_reg.stall = 0;
    }
    else
    {
      auto& reg = dev->out_eps[ep_num - 1U].doepctl_reg;
      reg.stall = 0;
      reg.setd0pid = 1;
    }
  }

  SetState(State::IDLE);
  return ErrorCode::OK;
}

ErrorCode ESP32USBEndpoint::Transfer(size_t size)
{
  if (GetState() == State::BUSY)
  {
    return ErrorCode::BUSY;
  }

  auto buffer = GetBuffer();
  if (buffer.size_ < size)
  {
    return ErrorCode::NO_BUFF;
  }

  transfer_buffer_ = static_cast<uint8_t*>(buffer.addr_);
  transfer_request_size_ = size;
  transfer_actual_size_ = 0U;
  transfer_queued_size_ = 0U;

  const uint8_t ep_num = EPNumberToInt8(GetNumber());
  if (ep_num == 0U)
  {
    if (GetDirection() == Direction::IN)
    {
      device_.debug_.ep0_in_start_count++;
      device_.debug_.last_ep0_in_size = static_cast<uint16_t>(size);
      if (device_.DmaEnabled() && (size == 0U) &&
          Detail::IsSetLineCodingSetup(device_.debug_.last_setup_packet))
      {
        device_.debug_.line_in_zlp_start_count++;
        device_.debug_.line_state = 5U;
      }
    }
    else
    {
      device_.debug_.ep0_out_start_count++;
      device_.debug_.last_ep0_out_size = static_cast<uint16_t>(size);
      ep0_out_phase_ = (size == 0U) ? Ep0OutPhase::STATUS : Ep0OutPhase::DATA;
      if (device_.DmaEnabled() && (size > 0U) &&
          Detail::IsSetLineCodingSetup(device_.debug_.last_setup_packet))
      {
        device_.debug_.line_out_arm_count++;
        device_.debug_.line_state = 2U;
      }
    }
  }

  if (UseDoubleBuffer() && GetDirection() == Direction::IN && size > 0U)
  {
    SwitchBuffer();
  }

  if (!PrepareTransferBuffer(size))
  {
    ResetTransferState();
    return ErrorCode::NO_MEM;
  }

  SetState(State::BUSY);
  ProgramTransfer(size);
  return ErrorCode::OK;
}

size_t ESP32USBEndpoint::MaxTransferSize() const
{
  if (GetNumber() == EPNumber::EP0)
  {
    return MaxPacketSize();
  }
  return GetBuffer().size_;
}

void ESP32USBEndpoint::ResetHardwareState()
{
  fifo_allocated_ = false;
  fifo_words_ = 0U;
  ResetTransferState();
  SetState(State::DISABLED);
}

void ESP32USBEndpoint::HandleInInterrupt(bool in_isr)
{
  const uint8_t ep_num = EPNumberToInt8(GetNumber());
  usb_dwc_diepint_reg_t intr = {};
  auto* dev = reinterpret_cast<usb_dwc_dev_t*>(Detail::kDwc2FsRegBase);

  if (ep_num == 0U)
  {
    intr.val = dev->diepint0_reg.val;
    dev->diepint0_reg.val = intr.val;
  }
  else
  {
    intr.val = dev->in_eps[ep_num - 1U].diepint_reg.val;
    dev->in_eps[ep_num - 1U].diepint_reg.val = intr.val;
  }

  if (!device_.DmaEnabled() && intr.txfemp)
  {
    WriteMoreTxData();
    if (transfer_queued_size_ >= transfer_request_size_)
    {
      dev->diepempmsk_reg.val &= ~(1UL << ep_num);
    }
  }

  if (intr.xfercompl)
  {
    const bool rearm_ep0_setup =
        (ep_num == 0U) && device_.DmaEnabled() && (transfer_request_size_ == 0U) &&
        Detail::IsOutRequestSetup(device_.debug_.last_setup_packet);
    dev->diepempmsk_reg.val &= ~(1UL << ep_num);
    const size_t actual =
        device_.DmaEnabled() ? GetCompletedTransferSize() : transfer_request_size_;
    if (ep_num == 0U)
    {
      device_.debug_.ep0_in_complete_count++;
      if (device_.DmaEnabled() &&
          Detail::IsSetLineCodingSetup(device_.debug_.last_setup_packet))
      {
        device_.debug_.line_in_irq_count++;
        device_.debug_.line_state = 6U;
        device_.debug_.last_line_diepint = intr.val;
      }
    }
    OnTransferCompleteCallback(in_isr, actual);
    if (rearm_ep0_setup)
    {
      device_.ReloadSetupPacketCount();
    }
    if (GetState() != State::BUSY)
    {
      ResetTransferState();
    }
  }
}

void ESP32USBEndpoint::FinishPendingEp0InStatus(bool in_isr)
{
  if (!device_.DmaEnabled() || (device_.endpoint_map_.in[0] == nullptr))
  {
    return;
  }

  auto* ep0_in = static_cast<ESP32USBEndpoint*>(device_.endpoint_map_.in[0]);
  if ((ep0_in->GetState() != State::BUSY) || (ep0_in->transfer_request_size_ != 0U))
  {
    return;
  }

  if (Detail::IsSetLineCodingSetup(device_.debug_.last_setup_packet))
  {
    device_.debug_.line_in_manual_finish_count++;
    device_.debug_.line_state = 7U;
  }
  ep0_in->OnTransferCompleteCallback(in_isr, 0U);
  if (ep0_in->GetState() != State::BUSY)
  {
    ep0_in->ResetTransferState();
  }
}

void ESP32USBEndpoint::HandleOutInterrupt(bool in_isr)
{
  const uint8_t ep_num = EPNumberToInt8(GetNumber());
  usb_dwc_doepint_reg_t intr = {};
  auto* dev = reinterpret_cast<usb_dwc_dev_t*>(Detail::kDwc2FsRegBase);

  if (ep_num == 0U)
  {
    intr.val = dev->doepint0_reg.val;
    dev->doepint0_reg.val = intr.val;
  }
  else
  {
    intr.val = dev->out_eps[ep_num - 1U].doepint_reg.val;
    dev->out_eps[ep_num - 1U].doepint_reg.val = intr.val;
  }

  const bool ep0_setup_related =
      (ep_num == 0U) && (intr.setup || intr.stuppktrcvd || intr.back2backsetup);
  const bool has_active_ep0_transfer =
      (ep_num == 0U) && (GetState() == State::BUSY) &&
      ((transfer_buffer_ != nullptr) || (transfer_request_size_ > 0U));

  if (intr.xfercompl &&
      ((ep_num != 0U) || ((ep0_out_phase_ != Ep0OutPhase::SETUP) && !ep0_setup_related &&
                          has_active_ep0_transfer)))
  {
    const bool line_transfer =
        device_.DmaEnabled() && (ep_num == 0U) &&
        Detail::IsSetLineCodingSetup(device_.debug_.last_setup_packet);
    const size_t actual =
        device_.DmaEnabled() ? GetCompletedTransferSize() : transfer_actual_size_;
    ASSERT(FinishOutTransfer(actual));
    if (ep_num == 0U)
    {
      device_.debug_.ep0_out_complete_count++;
      device_.debug_.last_ep0_out_size = static_cast<uint16_t>(actual);
      if (line_transfer)
      {
        device_.debug_.line_out_irq_count++;
        device_.debug_.line_state = 3U;
        device_.debug_.last_line_out_actual = static_cast<uint16_t>(actual);
        device_.debug_.last_line_doepint = intr.val;
      }
      FinishPendingEp0InStatus(in_isr);
    }
    OnTransferCompleteCallback(in_isr, actual);
    if (line_transfer)
    {
      device_.debug_.line_out_cb_count++;
      device_.debug_.line_state = 4U;
    }
    if ((ep_num == 0U) && device_.DmaEnabled() && (transfer_request_size_ == 0U))
    {
      device_.ReloadSetupPacketCount();
    }
    if (GetState() != State::BUSY)
    {
      ResetTransferState();
    }
  }
  else if ((ep_num == 0U) && intr.xfercompl && ep0_setup_related &&
           Detail::IsSetLineCodingSetup(device_.debug_.last_setup_packet))
  {
    device_.debug_.line_setup_xfer_overlap_count++;
  }

  if (intr.setup)
  {
    device_.debug_.ep0_setup_irq_count++;
    if (device_.DmaEnabled())
    {
      ASSERT(Detail::CacheSyncDmaBuffer(device_.setup_packet_,
                                        ESP32USBDevice::kSetupDmaBufferBytes, false));
      device_.debug_.setup_data_count++;
      device_.debug_.setup_done_count++;
      std::memcpy(device_.debug_.last_setup_packet, device_.setup_packet_,
                  ESP32USBDevice::kSetupPacketBytes);
      if (Detail::IsSetLineCodingSetup(device_.setup_packet_))
      {
        device_.debug_.line_setup_irq_count++;
        device_.debug_.line_state = 1U;
        if (intr.xfercompl)
        {
          device_.debug_.line_setup_xfer_overlap_count++;
        }
      }
    }
    const auto* setup = reinterpret_cast<const USB::SetupPacket*>(device_.setup_packet_);
    const bool expect_out_data_stage =
        ((setup->bmRequestType & 0x80U) == 0U) && (setup->wLength > 0U);

    FinishPendingEp0InStatus(in_isr);
    ep0_out_phase_ = Ep0OutPhase::SETUP;
    ResetTransferState();
    SetState(State::IDLE);

    if (device_.endpoint_map_.in[0] != nullptr)
    {
      auto* ep0_in = static_cast<ESP32USBEndpoint*>(device_.endpoint_map_.in[0]);
      if (ep0_in->GetState() == State::BUSY)
      {
        ep0_in->ResetTransferState();
        ep0_in->SetState(State::IDLE);
      }
    }

    if (device_.DmaEnabled() && !expect_out_data_stage)
    {
      device_.ReloadSetupPacketCount();
    }
    device_.OnSetupPacket(in_isr, setup);
  }
}

void ESP32USBEndpoint::HandleRxData(size_t size)
{
  uint8_t* const dst = (transfer_buffer_ != nullptr)
                           ? (transfer_buffer_ + transfer_actual_size_)
                           : nullptr;
  const size_t remain = (transfer_request_size_ > transfer_actual_size_)
                            ? (transfer_request_size_ - transfer_actual_size_)
                            : 0U;
  const size_t copy_size = std::min(size, remain);
  const size_t drop_size = size - copy_size;

  if (copy_size > 0U)
  {
    Detail::ReadFifoPacket(
        Detail::GetEndpointFifo(reinterpret_cast<usb_dwc_dev_t*>(Detail::kDwc2FsRegBase),
                                0),
        dst, copy_size);
    transfer_actual_size_ += copy_size;
  }

  if (drop_size == 0U)
  {
    return;
  }

  uint8_t sink[64] = {};
  size_t remain_drop = drop_size;
  auto* fifo = Detail::GetEndpointFifo(
      reinterpret_cast<usb_dwc_dev_t*>(Detail::kDwc2FsRegBase), 0);
  while (remain_drop > 0U)
  {
    const size_t chunk = std::min(remain_drop, sizeof(sink));
    Detail::ReadFifoPacket(fifo, sink, chunk);
    remain_drop -= chunk;
  }
}

void ESP32USBEndpoint::ObserveDmaRxStatus(uint8_t pktsts, size_t size)
{
  if (!device_.DmaEnabled() || (GetDirection() != Direction::OUT))
  {
    return;
  }

  if (pktsts != Detail::kRxStatusData)
  {
    return;
  }

  dma_rx_status_seen_ = true;
  dma_rx_status_bytes_ = std::min(transfer_request_size_, dma_rx_status_bytes_ + size);
}

void ESP32USBEndpoint::ResetTransferState()
{
  transfer_buffer_ = nullptr;
  transfer_hw_buffer_ = nullptr;
  transfer_request_size_ = 0U;
  transfer_actual_size_ = 0U;
  transfer_queued_size_ = 0U;
  transfer_uses_shadow_ = false;
  transfer_direct_sync_size_ = 0U;
  dma_rx_status_bytes_ = 0U;
  dma_rx_status_seen_ = false;
}

bool ESP32USBEndpoint::EnsureDmaShadow(size_t size)
{
  const size_t need = Detail::AlignUp(size, Detail::kUsbDmaAlignment);
  if (need == 0U)
  {
    return true;
  }

  if ((dma_shadow_buffer_ != nullptr) && (dma_shadow_size_ >= need))
  {
    return true;
  }

  void* new_buffer =
      heap_caps_aligned_alloc(Detail::kUsbDmaAlignment, need, Detail::kDmaMemoryCaps);
  if (new_buffer == nullptr)
  {
    return false;
  }

  if (dma_shadow_buffer_ != nullptr)
  {
    heap_caps_free(dma_shadow_buffer_);
  }

  dma_shadow_buffer_ = static_cast<uint8_t*>(new_buffer);
  dma_shadow_size_ = need;
  return true;
}

bool ESP32USBEndpoint::PrepareTransferBuffer(size_t size)
{
  transfer_hw_buffer_ = transfer_buffer_;
  transfer_uses_shadow_ = false;
  transfer_direct_sync_size_ = 0U;

  if (!device_.DmaEnabled() || (size == 0U))
  {
    return true;
  }

  if (GetDirection() == Direction::IN)
  {
    if (Detail::CanUseDirectInDmaBuffer(transfer_buffer_, size))
    {
      return Detail::CacheSyncDmaBuffer(transfer_hw_buffer_, size, true, true);
    }
  }
  else
  {
    auto buffer = GetBuffer();
    if (Detail::CanUseDirectOutDmaBuffer(buffer.addr_, buffer.size_))
    {
      transfer_direct_sync_size_ = buffer.size_;
      return true;
    }
  }

  if (!EnsureDmaShadow(size))
  {
    return false;
  }

  transfer_hw_buffer_ = dma_shadow_buffer_;
  transfer_uses_shadow_ = true;

  if (GetDirection() == Direction::IN)
  {
    ASSERT(transfer_buffer_ != nullptr);
    std::memcpy(transfer_hw_buffer_, transfer_buffer_, size);
    return Detail::CacheSyncDmaBuffer(
        transfer_hw_buffer_, Detail::AlignUp(size, Detail::kUsbDmaAlignment), true);
  }

  return Detail::CacheSyncDmaBuffer(
      transfer_hw_buffer_, Detail::AlignUp(size, Detail::kUsbDmaAlignment), false);
}

bool ESP32USBEndpoint::FinishOutTransfer(size_t actual_size)
{
  transfer_actual_size_ = actual_size;

  if (!device_.DmaEnabled() || (actual_size == 0U))
  {
    return true;
  }

  ASSERT(transfer_hw_buffer_ != nullptr);

  if (!transfer_uses_shadow_)
  {
    if (transfer_direct_sync_size_ == 0U)
    {
      return true;
    }
    return Detail::CacheSyncDmaBuffer(transfer_hw_buffer_, transfer_direct_sync_size_,
                                      false);
  }

  ASSERT(transfer_buffer_ != nullptr);

  if (!Detail::CacheSyncDmaBuffer(transfer_hw_buffer_,
                                  Detail::AlignUp(actual_size, Detail::kUsbDmaAlignment),
                                  false))
  {
    return false;
  }

  std::memcpy(transfer_buffer_, transfer_hw_buffer_, actual_size);
  return true;
}

size_t ESP32USBEndpoint::GetRemainingTransferSize() const
{
  auto* dev = reinterpret_cast<usb_dwc_dev_t*>(Detail::kDwc2FsRegBase);
  const uint8_t ep_num = EPNumberToInt8(GetNumber());

  if (GetDirection() == Direction::IN)
  {
    if (ep_num == 0U)
    {
      return dev->dieptsiz0_reg.xfersize;
    }
    return dev->in_eps[ep_num - 1U].dieptsiz_reg.xfersize;
  }

  if (ep_num == 0U)
  {
    return dev->doeptsiz0_reg.xfersize;
  }
  return dev->out_eps[ep_num - 1U].doeptsiz_reg.xfersize;
}

size_t ESP32USBEndpoint::GetCompletedTransferSize() const
{
  if ((GetDirection() == Direction::OUT) && device_.DmaEnabled() && dma_rx_status_seen_)
  {
    return dma_rx_status_bytes_;
  }

  const size_t remaining = GetRemainingTransferSize();
  return (remaining >= transfer_request_size_) ? 0U
                                               : (transfer_request_size_ - remaining);
}

void ESP32USBEndpoint::ActivateHardwareEndpoint()
{
  auto* dev = reinterpret_cast<usb_dwc_dev_t*>(Detail::kDwc2FsRegBase);
  const uint8_t ep_num = EPNumberToInt8(GetNumber());
  auto& cfg = GetConfig();

  if (GetDirection() == Direction::IN)
  {
    if (ep_num == 0U)
    {
      usb_dwc_diepctl0_reg_t ctl = {};
      ctl.mps = Detail::EncodeEp0Mps(cfg.max_packet_size);
      ctl.usbactep = 1;
      ctl.eptype = static_cast<uint32_t>(cfg.type);
      ctl.txfnum = 0;
      dev->diepctl0_reg.val = ctl.val;
    }
    else
    {
      usb_dwc_diepctl_reg_t ctl = {};
      ctl.mps = cfg.max_packet_size;
      ctl.usbactep = 1;
      ctl.eptype = static_cast<uint32_t>(cfg.type);
      ctl.txfnum = ep_num;
      ctl.setd0pid = 1;
      dev->in_eps[ep_num - 1U].diepctl_reg.val = ctl.val;
    }
    dev->daintmsk_reg.val |= (1UL << ep_num);
  }
  else
  {
    if (ep_num == 0U)
    {
      usb_dwc_doepctl0_reg_t ctl = {};
      ctl.mps = Detail::EncodeEp0Mps(cfg.max_packet_size);
      ctl.usbactep = 1;
      ctl.eptype = static_cast<uint32_t>(cfg.type);
      dev->doepctl0_reg.val = ctl.val;
    }
    else
    {
      usb_dwc_doepctl_reg_t ctl = {};
      ctl.mps = cfg.max_packet_size;
      ctl.usbactep = 1;
      ctl.eptype = static_cast<uint32_t>(cfg.type);
      ctl.setd0pid = 1;
      dev->out_eps[ep_num - 1U].doepctl_reg.val = ctl.val;
    }
    dev->daintmsk_reg.val |= (1UL << (16U + ep_num));
  }
}

void ESP32USBEndpoint::ProgramTransfer(size_t size)
{
  auto* dev = reinterpret_cast<usb_dwc_dev_t*>(Detail::kDwc2FsRegBase);
  const uint8_t ep_num = EPNumberToInt8(GetNumber());
  const uint16_t pkt_count = Detail::PacketCount(size, MaxPacketSize());

  if (GetDirection() == Direction::IN)
  {
    if (ep_num == 0U)
    {
      dev->dieptsiz0_reg.xfersize = static_cast<uint32_t>(size);
      dev->dieptsiz0_reg.pktcnt = pkt_count;
      if (device_.DmaEnabled() && (size > 0U))
      {
        dev->diepdma0_reg.dmaaddr =
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(transfer_hw_buffer_));
      }
      if (device_.DmaEnabled() && (size == 0U) &&
          (device_.runtime_.pending_address != 0xFFU))
      {
        dev->dcfg_reg.devaddr = device_.runtime_.pending_address;
        device_.runtime_.pending_address = 0xFFU;
      }
      Detail::StartEndpointTransfer(dev->diepctl0_reg);
    }
    else
    {
      auto& tsiz = dev->in_eps[ep_num - 1U].dieptsiz_reg;
      tsiz.xfersize = static_cast<uint32_t>(size);
      tsiz.pktcnt = pkt_count;
      tsiz.mc = 1U;
      if (device_.DmaEnabled() && (size > 0U))
      {
        dev->in_eps[ep_num - 1U].diepdma_reg.dmaddr =
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(transfer_hw_buffer_));
      }
      Detail::StartEndpointTransfer(dev->in_eps[ep_num - 1U].diepctl_reg);
    }

    if (!device_.DmaEnabled() && (size > 0U))
    {
      WriteMoreTxData();
      if ((ep_num != 0U) && (transfer_queued_size_ < transfer_request_size_))
      {
        dev->diepempmsk_reg.val |= (1UL << ep_num);
      }
    }
  }
  else
  {
    if (ep_num == 0U)
    {
      dev->doeptsiz0_reg.val = 0U;
      dev->doeptsiz0_reg.xfersize =
          static_cast<uint32_t>((size == 0U) ? MaxPacketSize() : size);
      dev->doeptsiz0_reg.pktcnt = pkt_count;
      if (device_.DmaEnabled() && (size > 0U))
      {
        dev->doepdma0_reg.dmaaddr =
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(transfer_hw_buffer_));
      }
      Detail::StartEndpointTransfer(dev->doepctl0_reg);
    }
    else
    {
      auto& tsiz = dev->out_eps[ep_num - 1U].doeptsiz_reg;
      tsiz.xfersize = static_cast<uint32_t>(size);
      tsiz.pktcnt = pkt_count;
      tsiz.rxdpid = 0U;
      if (device_.DmaEnabled() && (size > 0U))
      {
        dev->out_eps[ep_num - 1U].doepdma_reg.dmaaddr =
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(transfer_hw_buffer_));
      }
      Detail::StartEndpointTransfer(dev->out_eps[ep_num - 1U].doepctl_reg);
    }
  }
}

void ESP32USBEndpoint::WriteMoreTxData()
{
  if (transfer_buffer_ == nullptr || transfer_queued_size_ >= transfer_request_size_)
  {
    return;
  }

  auto* dev = reinterpret_cast<usb_dwc_dev_t*>(Detail::kDwc2FsRegBase);
  const uint8_t ep_num = EPNumberToInt8(GetNumber());
  const volatile uint32_t* fifo = Detail::GetEndpointFifo(dev, ep_num);

  while (transfer_queued_size_ < transfer_request_size_)
  {
    uint16_t fifo_words = 0U;
    if (ep_num == 0U)
    {
      fifo_words = dev->dtxfsts0_reg.ineptxfspcavail;
    }
    else
    {
      fifo_words = dev->in_eps[ep_num - 1U].dtxfsts_reg.ineptxfspcavail;
    }

    const size_t remaining = transfer_request_size_ - transfer_queued_size_;
    const size_t packet_size = std::min(remaining, static_cast<size_t>(MaxPacketSize()));
    if (packet_size > static_cast<size_t>(fifo_words) * 4U)
    {
      break;
    }

    Detail::WriteFifoPacket(const_cast<volatile uint32_t*>(fifo),
                            transfer_buffer_ + transfer_queued_size_, packet_size);
    transfer_queued_size_ += packet_size;
  }
}

}  // namespace LibXR

#endif
