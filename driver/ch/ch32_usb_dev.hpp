#pragma once

#include "ch32_usb.hpp"
#include "ch32_usb_endpoint.hpp"
#include "libxr_def.hpp"
#include "usb/core/ep_pool.hpp"
#include "usb/device/dev_core.hpp"

namespace LibXR
{

class CH32USBDevice : public USB::EndpointPool, public USB::DeviceCore
{
 public:
  CH32USBDevice(
      ch32_usb_dev_id_t id, size_t max_ep_num,
      USB::DeviceDescriptor::PacketSize0 packet_size, uint16_t vid, uint16_t pid,
      uint16_t bcd,
      const std::initializer_list<const USB::DescriptorStrings::LanguagePack *> LANG_LIST,
      const std::initializer_list<
          const std::initializer_list<USB::ConfigDescriptorItem *>>
          CONFIGS,
      USB::Speed speed = USB::Speed::FULL, USB::USBSpec spec = USB::USBSpec::USB_2_0)
      : USB::EndpointPool(max_ep_num),
        USB::DeviceCore(*this, spec, speed, packet_size, vid, pid, bcd, LANG_LIST,
                        CONFIGS),
        id_(id)
  {
  }

  void Init() override { USB::DeviceCore::Init(); }
  void Deinit() override { USB::DeviceCore::Deinit(); }

  uint8_t id_;
};

class CH32USBDeviceFS : public USB::EndpointPool, public USB::DeviceCore
{
 public:
  CH32USBDeviceFS(
      const std::initializer_list<LibXR::RawData> EP_CFGS,
      USB::DeviceDescriptor::PacketSize0 packet_size, uint16_t vid, uint16_t pid,
      uint16_t bcd,
      const std::initializer_list<const USB::DescriptorStrings::LanguagePack *> LANG_LIST,
      const std::initializer_list<
          const std::initializer_list<USB::ConfigDescriptorItem *>>
          CONFIGS);

  ErrorCode SetAddress(uint8_t address, USB::DeviceCore::Context context) override;

  void Start() override;

  void Stop() override;

  static inline CH32USBDeviceFS *self_ = nullptr;
};
}  // namespace LibXR
