#include "esp_usb_dev.hpp"

#if SOC_USB_OTG_SUPPORTED && defined(CONFIG_IDF_TARGET_ESP32S3) && \
    CONFIG_IDF_TARGET_ESP32S3

#include <cstring>
#include <new>

#include "esp32s3/rom/usb/cdc_acm.h"
#include "esp32s3/rom/usb/usb_dc.h"
#include "esp32s3/rom/usb/usb_device.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_private/usb_phy.h"
#include "esp_usb.hpp"
#include "esp_usb_ep.hpp"
#include "soc/interrupts.h"

namespace Detail = LibXR::ESPUSBDetail;

namespace LibXR
{

ESP32USBDevice::ESP32USBDevice(
    const std::initializer_list<EPConfig> ep_cfgs,
    USB::DeviceDescriptor::PacketSize0 packet_size, uint16_t vid, uint16_t pid,
    uint16_t bcd,
    const std::initializer_list<const USB::DescriptorStrings::LanguagePack*> lang_list,
    const std::initializer_list<const std::initializer_list<USB::ConfigDescriptorItem*>>
        configs,
    ConstRawData uid)
    : USB::EndpointPool(ep_cfgs.size() * 2U),
      USB::DeviceCore(*this, USB::USBSpec::USB_2_1, USB::Speed::FULL, packet_size, vid,
                      pid, bcd, lang_list, configs, uid)
{
  ASSERT(ep_cfgs.size() > 0U && ep_cfgs.size() <= kEndpointCount);

  auto cfg_it = ep_cfgs.begin();

  auto* ep0_out = new ESP32USBEndpoint(*this, USB::Endpoint::EPNumber::EP0,
                                       USB::Endpoint::Direction::OUT, cfg_it->buffer);
  auto* ep0_in = new ESP32USBEndpoint(*this, USB::Endpoint::EPNumber::EP0,
                                      USB::Endpoint::Direction::IN, cfg_it->buffer);

  SetEndpoint0(ep0_in, ep0_out);
  endpoint_map_.in[0] = ep0_in;
  endpoint_map_.out[0] = ep0_out;

  auto ep_num = USB::Endpoint::EPNumber::EP1;
  for (++cfg_it; cfg_it != ep_cfgs.end();
       ++cfg_it, ep_num = USB::Endpoint::NextEPNumber(ep_num))
  {
    ASSERT(USB::Endpoint::EPNumberToInt8(ep_num) < kEndpointCount);

    if (cfg_it->direction_hint == EPConfig::DirectionHint::BothDirections)
    {
      auto* ep_out = new ESP32USBEndpoint(*this, ep_num, USB::Endpoint::Direction::OUT,
                                          cfg_it->buffer);
      auto* ep_in = new ESP32USBEndpoint(*this, ep_num, USB::Endpoint::Direction::IN,
                                         cfg_it->buffer);
      endpoint_map_.out[USB::Endpoint::EPNumberToInt8(ep_num)] = ep_out;
      endpoint_map_.in[USB::Endpoint::EPNumberToInt8(ep_num)] = ep_in;
      Put(ep_out);
      Put(ep_in);
    }
    else
    {
      const auto direction = (cfg_it->direction_hint == EPConfig::DirectionHint::InOnly)
                                 ? USB::Endpoint::Direction::IN
                                 : USB::Endpoint::Direction::OUT;
      auto* ep = new ESP32USBEndpoint(*this, ep_num, direction, cfg_it->buffer);
      if (direction == USB::Endpoint::Direction::IN)
      {
        endpoint_map_.in[USB::Endpoint::EPNumberToInt8(ep_num)] = ep;
      }
      else
      {
        endpoint_map_.out[USB::Endpoint::EPNumberToInt8(ep_num)] = ep;
      }
      Put(ep);
    }
  }
}

void ESP32USBDevice::Init(bool in_isr)
{
  ResetFifoState();
  USB::DeviceCore::Init(in_isr);
  runtime_.core_inited = true;
}

void ESP32USBDevice::Deinit(bool in_isr)
{
  USB::DeviceCore::Deinit(in_isr);
  runtime_.core_inited = false;
}

ErrorCode ESP32USBDevice::SetAddress(uint8_t address, USB::DeviceCore::Context context)
{
  if (context == USB::DeviceCore::Context::SETUP_BEFORE_STATUS)
  {
    auto* dev = reinterpret_cast<usb_dwc_dev_t*>(Detail::kDwc2FsRegBase);
    dev->dcfg_reg.devaddr = address;
  }
  return ErrorCode::OK;
}

void ESP32USBDevice::Start(bool)
{
  if (runtime_.started)
  {
    return;
  }

  EnsureRomUsbCleaned();
  ASSERT(EnsurePhyReady());
  ASSERT(EnsureInterruptReady());

  InitializeCore();
  if (runtime_.core_inited)
  {
    USB::DeviceCore::Deinit(false);
    USB::DeviceCore::Init(false);
  }

  auto* dev = reinterpret_cast<usb_dwc_dev_t*>(Detail::kDwc2FsRegBase);
  dev->dctl_reg.sftdiscon = 0;
  dev->gahbcfg_reg.glbllntrmsk = 1;
  if (runtime_.intr_handle != nullptr)
  {
    esp_intr_enable(runtime_.intr_handle);
  }
  runtime_.started = true;
}

void ESP32USBDevice::Stop(bool)
{
  auto* dev = reinterpret_cast<usb_dwc_dev_t*>(Detail::kDwc2FsRegBase);
  dev->gahbcfg_reg.glbllntrmsk = 0;
  dev->gintmsk_reg.val = 0;
  if (runtime_.intr_handle != nullptr)
  {
    esp_intr_disable(runtime_.intr_handle);
  }
  dev->dctl_reg.sftdiscon = 1;
  runtime_.started = false;
}

void IRAM_ATTR ESP32USBDevice::IsrEntry(void* arg)
{
  auto* self = static_cast<ESP32USBDevice*>(arg);
  if (self != nullptr)
  {
    self->HandleInterrupt();
  }
}

bool ESP32USBDevice::EnsurePhyReady()
{
  if (runtime_.phy_ready)
  {
    return true;
  }

  usb_phy_config_t phy_conf = {
      .controller = USB_PHY_CTRL_OTG,
      .target = USB_PHY_TARGET_INT,
      .otg_mode = USB_OTG_MODE_DEVICE,
      .otg_speed = USB_PHY_SPEED_UNDEFINED,
      .ext_io_conf = nullptr,
      .otg_io_conf = nullptr,
  };

  usb_phy_handle_t handle = nullptr;
  if (usb_new_phy(&phy_conf, &handle) != ESP_OK)
  {
    return false;
  }

  runtime_.phy_handle = handle;
  runtime_.phy_ready = true;
  return true;
}

bool ESP32USBDevice::EnsureInterruptReady()
{
  if (runtime_.irq_ready)
  {
    return true;
  }

  if (esp_intr_alloc(ETS_USB_INTR_SOURCE, ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_INTRDISABLED,
                     IsrEntry, this, &runtime_.intr_handle) != ESP_OK)
  {
    runtime_.intr_handle = nullptr;
    return false;
  }

  runtime_.irq_ready = true;
  return true;
}

void ESP32USBDevice::EnsureRomUsbCleaned()
{
  if (runtime_.rom_usb_cleaned)
  {
    return;
  }

  usb_dev_deinit();
  usb_dw_ctrl_deinit();
  uart_acm_dev = nullptr;
  runtime_.rom_usb_cleaned = true;
}

void ESP32USBDevice::InitializeCore()
{
  auto* dev = reinterpret_cast<usb_dwc_dev_t*>(Detail::kDwc2FsRegBase);

  dev->gahbcfg_reg.glbllntrmsk = 0;

  while (!dev->grstctl_reg.ahbidle)
  {
  }

  dev->gusbcfg_reg.forcehstmode = 0;
  dev->gusbcfg_reg.forcedevmode = 1;
  dev->gusbcfg_reg.physel = 1;
  dev->gusbcfg_reg.usbtrdtim = 5;
  dev->gusbcfg_reg.toutcal = 7;
  dev->gusbcfg_reg.srpcap = 0;
  dev->gusbcfg_reg.hnpcap = 0;

  dev->grstctl_reg.csftrst = 1;
  while (dev->grstctl_reg.csftrst)
  {
  }
  while (!dev->grstctl_reg.ahbidle)
  {
  }

  dev->pcgcctl_reg.stoppclk = 0;
  dev->pcgcctl_reg.gatehclk = 0;
  dev->pcgcctl_reg.pwrclmp = 0;
  dev->pcgcctl_reg.rstpdwnmodule = 0;
  dev->gotgctl_reg.avalidoven = 0;
  dev->gotgctl_reg.bvalidoven = 1;
  dev->gotgctl_reg.bvalidovval = 1;

  ClearTxFifoRegisters();

  FlushFifos();
  ResetDeviceState();

  dev->gintsts_reg.val = 0xFFFFFFFFU;
  dev->gotgint_reg.val = 0xFFFFFFFFU;
  dev->gintmsk_reg.val = 0U;
  dev->gahbcfg_reg.hbstlen = Detail::kDmaBurstIncr4;
  dev->gahbcfg_reg.dmaen = DmaEnabled() ? 1U : 0U;
  dev->gahbcfg_reg.nptxfemplvl = 1;
  dev->gahbcfg_reg.ptxfemplvl = 1;
  dev->dcfg_reg.descdma = 0;
  dev->dcfg_reg.nzstsouthshk = 1;
  dev->gintmsk_reg.usbrstmsk = 1;
  dev->gintmsk_reg.enumdonemsk = 1;
  dev->gintmsk_reg.usbsuspmsk = 1;
  dev->gintmsk_reg.wkupintmsk = 1;
  dev->gintmsk_reg.rxflvlmsk = DmaEnabled() ? 0U : 1U;
  dev->gintmsk_reg.oepintmsk = 1;
  dev->gintmsk_reg.iepintmsk = 1;
  dev->gintmsk_reg.otgintmsk = 1;
}

void ESP32USBDevice::ClearTxFifoRegisters()
{
  auto* dev = reinterpret_cast<usb_dwc_dev_t*>(Detail::kDwc2FsRegBase);
  dev->gnptxfsiz_reg.val = 0U;
  const size_t tx_fifo_reg_count =
      sizeof(dev->dieptxf_regs) / sizeof(dev->dieptxf_regs[0]);
  for (size_t i = 0U; i < tx_fifo_reg_count; ++i)
  {
    dev->dieptxf_regs[i].val = 0U;
  }
}

void ESP32USBDevice::FlushFifos()
{
  auto* dev = reinterpret_cast<usb_dwc_dev_t*>(Detail::kDwc2FsRegBase);
  dev->grstctl_reg.txfnum = Detail::kFlushAllTxFifo;
  dev->grstctl_reg.txfflsh = 1;
  while (dev->grstctl_reg.txfflsh)
  {
  }
  dev->grstctl_reg.rxfflsh = 1;
  while (dev->grstctl_reg.rxfflsh)
  {
  }
}

void ESP32USBDevice::ResetFifoState()
{
  fifo_state_.depth_words = Detail::GetHardwareFifoDepthWords();
  ASSERT(fifo_state_.depth_words > 0U);
  fifo_state_.rx_words =
      Detail::CalcConfiguredRxFifoWords(64U, kEndpointCount, DmaEnabled());
  fifo_state_.tx_next_words = fifo_state_.rx_words;
  std::memset(fifo_state_.tx_words, 0, sizeof(fifo_state_.tx_words));
  std::memset(fifo_state_.tx_bound, 0, sizeof(fifo_state_.tx_bound));
  fifo_state_.allocated_in = 0U;
}

void ESP32USBDevice::ResetDeviceState()
{
  ResetFifoState();
  ResetControlState();

  auto* dev = reinterpret_cast<usb_dwc_dev_t*>(Detail::kDwc2FsRegBase);
  dev->grxfsiz_reg.rxfdep = fifo_state_.rx_words;
  ClearTxFifoRegisters();
}

void ESP32USBDevice::ResetEndpointHardwareState()
{
  for (uint8_t i = 0; i < kEndpointCount; ++i)
  {
    if (endpoint_map_.in[i] != nullptr)
    {
      static_cast<ESP32USBEndpoint*>(endpoint_map_.in[i])->ResetHardwareState();
    }
    if (endpoint_map_.out[i] != nullptr)
    {
      static_cast<ESP32USBEndpoint*>(endpoint_map_.out[i])->ResetHardwareState();
    }
  }
}

void ESP32USBDevice::ReloadSetupPacketCount()
{
  auto* dev = reinterpret_cast<usb_dwc_dev_t*>(Detail::kDwc2FsRegBase);
  dev->doeptsiz0_reg.val = 0U;
  if (DmaEnabled())
  {
    dev->doeptsiz0_reg.xfersize = 3U * kSetupPacketBytes;
    dev->doeptsiz0_reg.pktcnt = 1U;
    dev->doeptsiz0_reg.supcnt = 3U;
    ASSERT(Detail::CacheSyncDmaBuffer(setup_packet_, kSetupDmaBufferBytes, false));
    dev->doepdma0_reg.dmaaddr =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(setup_packet_));
    dev->doepctl0_reg.cnak = 1;
    dev->doepctl0_reg.epena = 1;
    if (endpoint_map_.out[0] != nullptr)
    {
      static_cast<ESP32USBEndpoint*>(endpoint_map_.out[0])->ep0_out_phase_ =
          ESP32USBEndpoint::Ep0OutPhase::SETUP;
    }
  }
  else
  {
    dev->doeptsiz0_reg.pktcnt = 1U;
    dev->doeptsiz0_reg.xfersize = 3U * kSetupPacketBytes;
    dev->doeptsiz0_reg.supcnt = 3U;
  }
}

void ESP32USBDevice::ResetControlState() { control_ = {}; }

void ESP32USBDevice::UpdateSetupState(const uint8_t* setup)
{
  control_.setup_direction_out = (setup != nullptr) && ((setup[0] & 0x80U) == 0U);
}

void IRAM_ATTR ESP32USBDevice::HandleInterrupt()
{
  auto* dev = reinterpret_cast<usb_dwc_dev_t*>(Detail::kDwc2FsRegBase);
  for (uint32_t guard = 0; guard < kInterruptDispatchGuard; ++guard)
  {
    usb_dwc_gintsts_reg_t pending = {};
    pending.val = dev->gintsts_reg.val & dev->gintmsk_reg.val;

    if (pending.val == 0U)
    {
      break;
    }

    if (pending.usbrst)
    {
      dev->gintsts_reg.usbrst = 1;
      HandleBusReset(true);
    }

    if (pending.enumdone)
    {
      dev->gintsts_reg.enumdone = 1;
      const uint32_t enum_speed = dev->dsts_reg.enumspd;
      if (enum_speed != Detail::kEnumSpeedFull30To60Mhz &&
          enum_speed != Detail::kEnumSpeedFull48Mhz)
      {
        ASSERT(false);
      }
    }

    if (pending.usbsusp)
    {
      dev->gintsts_reg.usbsusp = 1;
    }

    if (pending.wkupint)
    {
      dev->gintsts_reg.wkupint = 1;
    }

    if (!DmaEnabled() && pending.rxflvl)
    {
      dev->gintmsk_reg.rxflvlmsk = 0;
      while (dev->gintsts_reg.rxflvl)
      {
        HandleRxFifoLevel();
      }
      dev->gintmsk_reg.rxflvlmsk = 1;
    }

    if (pending.oepint)
    {
      HandleEndpointInterrupt(true, false);
    }

    if (pending.iepint)
    {
      HandleEndpointInterrupt(true, true);
    }

    if (pending.otgint)
    {
      const uint32_t otg_status = dev->gotgint_reg.val;
      dev->gotgint_reg.val = otg_status;
    }
  }
}

void ESP32USBDevice::HandleBusReset(bool in_isr)
{
  auto* dev = reinterpret_cast<usb_dwc_dev_t*>(Detail::kDwc2FsRegBase);

  for (uint8_t i = 0; i < kEndpointCount; ++i)
  {
    if (i == 0U)
    {
      Detail::DisableOutEndpointAndWait(dev->doepctl0_reg);
    }
    else
    {
      Detail::DisableOutEndpointAndWait(dev->out_eps[i - 1U].doepctl_reg);
    }
  }

  for (uint8_t i = 0; i < kEndpointCount; ++i)
  {
    if (i == 0U)
    {
      if (dev->diepctl0_reg.epena)
      {
        dev->diepctl0_reg.snak = 1;
        dev->diepctl0_reg.epdis = 1;
      }
    }
    else if (dev->in_eps[i - 1U].diepctl_reg.epena)
    {
      dev->in_eps[i - 1U].diepctl_reg.snak = 1;
      dev->in_eps[i - 1U].diepctl_reg.epdis = 1;
    }
  }

  dev->daintmsk_reg.val = 0U;
  dev->doepmsk_reg.val = 0U;
  dev->diepmsk_reg.val = 0U;
  dev->diepempmsk_reg.val = 0U;
  dev->doepmsk_reg.setupmsk = 1;
  dev->doepmsk_reg.xfercomplmsk = 1;
  dev->doepmsk_reg.epdisbldmsk = 1;
  dev->doepmsk_reg.ahberrmsk = 1;
  dev->doepmsk_reg.outtknepdismsk = 1;
  dev->doepmsk_reg.stsphsercvdmsk = 1;
  dev->doepmsk_reg.outpkterrmsk = 1;
  dev->doepmsk_reg.bnaoutintrmsk = 1;
  dev->doepmsk_reg.bbleerrmsk = 1;
  dev->doepmsk_reg.nakmsk = 1;
  dev->doepmsk_reg.nyetmsk = 1;
  dev->diepmsk_reg.xfercomplmsk = 1;
  dev->diepmsk_reg.timeoutmsk = 1;

  FlushFifos();
  ResetDeviceState();
  ResetEndpointHardwareState();

  dev->dcfg_reg.devaddr = 0U;

  if (runtime_.core_inited)
  {
    Deinit(in_isr);
    Init(in_isr);
  }

  ReloadSetupPacketCount();
}

void ESP32USBDevice::HandleEndpointInterrupt(bool in_isr, bool in_dir)
{
  auto* dev = reinterpret_cast<usb_dwc_dev_t*>(Detail::kDwc2FsRegBase);
  const uint32_t daint = dev->daint_reg.val & dev->daintmsk_reg.val;
  const uint32_t bits = in_dir ? (daint & 0xFFFFU) : ((daint >> 16U) & 0xFFFFU);

  for (uint8_t ep_num = 0; ep_num < kEndpointCount; ++ep_num)
  {
    if ((bits & (1UL << ep_num)) == 0U)
    {
      continue;
    }

    USB::Endpoint* ep = in_dir ? endpoint_map_.in[ep_num] : endpoint_map_.out[ep_num];
    if (ep == nullptr)
    {
      continue;
    }

    auto* esp_ep = static_cast<ESP32USBEndpoint*>(ep);
    if (in_dir)
    {
      esp_ep->HandleInInterrupt(in_isr);
    }
    else
    {
      esp_ep->HandleOutInterrupt(in_isr);
    }
  }
}

void ESP32USBDevice::HandleRxFifoLevel()
{
  auto* dev = reinterpret_cast<usb_dwc_dev_t*>(Detail::kDwc2FsRegBase);
  usb_dwc_grxstsp_reg_t status = {};
  status.val = dev->grxstsp_reg.val;
  const uint8_t ep_num = static_cast<uint8_t>(status.chnum);

  if (DmaEnabled())
  {
    if ((ep_num < kEndpointCount) && (endpoint_map_.out[ep_num] != nullptr))
    {
      static_cast<ESP32USBEndpoint*>(endpoint_map_.out[ep_num])
          ->ObserveDmaRxStatus(status.pktsts, status.bcnt);
    }
    return;
  }

  switch (status.pktsts)
  {
    case Detail::kRxStatusGlobalOutNak:
      break;

    case Detail::kRxStatusSetupData:
      Detail::ReadFifoPacket(Detail::GetEndpointFifo(dev, 0), setup_packet_,
                             sizeof(setup_packet_));
      UpdateSetupState(setup_packet_);
      break;

    case Detail::kRxStatusSetupDone:
      ReloadSetupPacketCount();
      break;

    case Detail::kRxStatusData:
    {
      USB::Endpoint* ep = (ep_num < kEndpointCount) ? endpoint_map_.out[ep_num] : nullptr;
      if (ep != nullptr)
      {
        static_cast<ESP32USBEndpoint*>(ep)->HandleRxData(status.bcnt);
      }
      else
      {
        uint8_t sink[64] = {};
        Detail::ReadFifoPacket(Detail::GetEndpointFifo(dev, 0), sink, status.bcnt);
      }
      break;
    }

    case Detail::kRxStatusTransferComplete:
    default:
      break;
  }
}

bool ESP32USBDevice::AllocateTxFifo(uint8_t ep_num, uint16_t packet_size, bool is_bulk,
                                    uint16_t& fifo_words)
{
  auto* dev = reinterpret_cast<usb_dwc_dev_t*>(Detail::kDwc2FsRegBase);
  const uint8_t max_in_ep_num =
      static_cast<uint8_t>(sizeof(dev->dieptxf_regs) / sizeof(dev->dieptxf_regs[0]));

  (void)is_bulk;

  if (ep_num > max_in_ep_num)
  {
    return false;
  }

  const uint16_t words = Detail::CalcTxFifoWords(packet_size, DmaEnabled());
  if (!fifo_state_.tx_bound[ep_num] && (fifo_state_.allocated_in >= kInEndpointLimit))
  {
    return false;
  }

  for (uint8_t index = 0U; index <= ep_num; ++index)
  {
    const uint16_t requested_words =
        (index == ep_num) ? words : Detail::kEsp32SxFsMinTxFifoWords;
    if (fifo_state_.tx_words[index] != 0U)
    {
      if (fifo_state_.tx_words[index] < requested_words)
      {
        return false;
      }
      continue;
    }

    const uint16_t next_words =
        static_cast<uint16_t>(fifo_state_.tx_next_words + requested_words);
    if (next_words > fifo_state_.depth_words)
    {
      return false;
    }

    const uint32_t tx_fifo_reg =
        Detail::PackTxFifoSizeReg(fifo_state_.tx_next_words, requested_words);
    if (index == 0U)
    {
      dev->gnptxfsiz_reg.val = tx_fifo_reg;
    }
    else
    {
      dev->dieptxf_regs[index - 1U].val = tx_fifo_reg;
    }

    fifo_state_.tx_words[index] = requested_words;
    fifo_state_.tx_next_words = next_words;
  }

  if (!fifo_state_.tx_bound[ep_num])
  {
    fifo_state_.tx_bound[ep_num] = true;
    fifo_state_.allocated_in++;
  }
  fifo_words = words;
  return true;
}

bool ESP32USBDevice::EnsureRxFifo(uint16_t packet_size)
{
  auto* dev = reinterpret_cast<usb_dwc_dev_t*>(Detail::kDwc2FsRegBase);
  const uint16_t needed =
      Detail::CalcConfiguredRxFifoWords(packet_size, kEndpointCount, DmaEnabled());
  if (needed <= fifo_state_.rx_words)
  {
    return true;
  }

  if (fifo_state_.tx_next_words != fifo_state_.rx_words)
  {
    return false;
  }

  if (needed > fifo_state_.depth_words)
  {
    return false;
  }

  fifo_state_.rx_words = needed;
  fifo_state_.tx_next_words = needed;
  dev->grxfsiz_reg.rxfdep = fifo_state_.rx_words;
  return true;
}

}  // namespace LibXR

#endif
