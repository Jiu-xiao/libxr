#pragma once

#include "ch32_usb.hpp"
#include "ch32h41x_usb_endpoint_otghs.hpp"
#include "libxr_def.hpp"
#include "libxr_type.hpp"
#include "usb/core/ep_pool.hpp"
#include "usb/device/dev_core.hpp"

#if defined(USBHSD) && defined(__CH32H417_H)

#define LIBXR_CH32_USB_OTGHS_DEVICE_SPECIALIZED 1

namespace LibXR
{

class CH32H41xUSBOtgHS : public USB::EndpointPool, public USB::DeviceCore
{
 public:
  struct EPConfig
  {
    RawData buffer_tx;
    RawData buffer_rx;
    bool double_buffer;
    bool is_in;

    EPConfig(RawData buffer)
        : buffer_tx(buffer), buffer_rx(buffer), double_buffer(false), is_in(false)
    {
    }

    EPConfig(RawData buffer, bool is_in)
        : buffer_tx(buffer), buffer_rx(buffer), double_buffer(true), is_in(is_in)
    {
    }

    EPConfig(RawData buffer_tx, RawData buffer_rx)
        : buffer_tx(buffer_tx), buffer_rx(buffer_rx), double_buffer(false), is_in(false)
    {
    }
  };

  CH32H41xUSBOtgHS(
      const std::initializer_list<EPConfig> EP_CFGS, uint16_t vid, uint16_t pid,
      uint16_t bcd,
      const std::initializer_list<const USB::DescriptorStrings::LanguagePack*> LANG_LIST,
      const std::initializer_list<const std::initializer_list<USB::ConfigDescriptorItem*>>
          CONFIGS,
      ConstRawData uid = {nullptr, 0});

  ErrorCode SetAddress(uint8_t address, USB::DeviceCore::Context context) override;

  void Start(bool in_isr) override;
  void Stop(bool in_isr) override;

  static inline CH32H41xUSBOtgHS* self_ = nullptr;
};

using CH32USBOtgHS = CH32H41xUSBOtgHS;

}  // namespace LibXR

#endif
