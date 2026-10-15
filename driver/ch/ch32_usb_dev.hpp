#pragma once

#include "ch32_usb.hpp"
#include "ch32_usb_endpoint.hpp"
#include "ch32_usb_fsdev_regs.hpp"
#include "ch32_usbcan_shared.hpp"
#include "libxr_def.hpp"
#include "libxr_type.hpp"
#include "usb/core/ep_pool.hpp"
#include "usb/device/dev_core.hpp"

namespace LibXR
{

class CH32USBDevice : public USB::EndpointPool,
                      public USB::DeviceCore<USB::FullSpeedCapabilities>
{
 public:
  /**
   * @brief 构造 CH32 USB 设备核心对象 / Construct CH32 USB device core
   */
  CH32USBDevice(
      ch32_usb_dev_id_t id, size_t max_ep_num,
      USB::DeviceDescriptor::PacketSize0 packet_size, uint16_t vid, uint16_t pid,
      uint16_t bcd,
      const std::initializer_list<const USB::DescriptorStrings::LanguagePack*> LANG_LIST,
      const std::initializer_list<const std::initializer_list<USB::ConfigDescriptorItem*>>
          CONFIGS,
      USB::Speed speed = USB::Speed::FULL, USB::USBSpec spec = USB::USBSpec::USB_2_1)
      : USB::EndpointPool(max_ep_num),
        USB::DeviceCore<USB::FullSpeedCapabilities>(*this, spec, speed, packet_size, vid,
                                                    pid, bcd, LANG_LIST, CONFIGS),
        id_(id)
  {
  }

  void Init(bool in_isr) override
  {
    USB::DeviceCore<USB::FullSpeedCapabilities>::Init(in_isr);
  }
  void Deinit(bool in_isr) override
  {
    USB::DeviceCore<USB::FullSpeedCapabilities>::Deinit(in_isr);
  }

  uint8_t id_;
};

#if defined(RCC_APB1Periph_USB) || defined(__DOXYGEN__)

/**
 * @brief CH32 FSDEV 设备驱动 / CH32 FSDEV device driver
 *
 * PMA 布局构造期固定，之后不能再增加 CAN 的占用；PMA 可用量取构造时刻的
 * `usb_pma_limit_bytes()`。寄存器运行期与 STM32 FSDEV 共用（usb/backend/fsdev.hpp），
 * 中断经 `ch32_usbcan_shared` 注册到 USB/CAN 共享向量。
 *
 * The PMA layout is fixed at construction and CAN usage cannot grow afterwards; the
 * usable PMA is `usb_pma_limit_bytes()` at construction time. The register runtime is
 * shared with STM32 FSDEV (usb/backend/fsdev.hpp); the interrupt is registered through
 * `ch32_usbcan_shared` on the shared USB/CAN vectors.
 */
class CH32USBDeviceFS : public USB::FSDevDevice<Ch32DevFsHw>
{
 public:
  using EPConfig = typename USB::FSDevDevice<Ch32DevFsHw>::EPConfig;

  /**
   * @brief 构造 FSDEV 设备对象 / Construct FSDEV device object
   */
  CH32USBDeviceFS(
      const std::initializer_list<EPConfig>& rx_ep_cfgs,
      const std::initializer_list<EPConfig>& tx_ep_cfgs,
      USB::DeviceDescriptor::PacketSize0 packet_size, uint16_t vid, uint16_t pid,
      uint16_t bcd,
      const std::initializer_list<const USB::DescriptorStrings::LanguagePack*> LANG_LIST,
      const std::initializer_list<const std::initializer_list<USB::ConfigDescriptorItem*>>
          CONFIGS,
      ConstRawData uid = {nullptr, 0})
      : USB::FSDevDevice<Ch32DevFsHw>(rx_ep_cfgs, tx_ep_cfgs, packet_size, vid, pid, bcd,
                                      LANG_LIST, CONFIGS, uid,
                                      CH32UsbCanShared::usb_pma_limit_bytes())
  {
    CH32UsbCanShared::usb_pma_configured.store(true, std::memory_order_release);
  }

  void Start(bool in_isr) override;
  void Stop(bool in_isr) override;
};

#endif  // defined(RCC_APB1Periph_USB) || defined(__DOXYGEN__)

// The OTG FS/HS device classes still use the pre-slot-model backend interface and are
// migrated in step 5; until then they need LIBXR_CH32_USB_OTG_LEGACY. See
// ch32_usb_endpoint.hpp.
#if (defined(USBFSD) && defined(LIBXR_CH32_USB_OTG_LEGACY)) || defined(__DOXYGEN__)

/**
 * @brief CH32 OTG FS 设备驱动 / CH32 OTG FS device driver
 */
class CH32USBOtgFS : public USB::EndpointPool,
                     public USB::DeviceCore<USB::FullSpeedCapabilities>
{
 public:
  /**
   * @brief OTG FS 端点配置 / OTG FS endpoint configuration
   */
  struct EPConfig
  {
    LibXR::RawData buffer;
    int8_t is_in;

    EPConfig(LibXR::RawData buffer) : buffer(buffer), is_in(-1) {}
    EPConfig(LibXR::RawData buffer, bool is_in) : buffer(buffer), is_in(is_in ? 1 : 0) {}
  };

  /**
   * @brief 构造 OTG FS 设备对象 / Construct OTG FS device object
   */
  CH32USBOtgFS(
      const std::initializer_list<EPConfig> EP_CFGS,
      USB::DeviceDescriptor::PacketSize0 packet_size, uint16_t vid, uint16_t pid,
      uint16_t bcd,
      const std::initializer_list<const USB::DescriptorStrings::LanguagePack*> LANG_LIST,
      const std::initializer_list<const std::initializer_list<USB::ConfigDescriptorItem*>>
          CONFIGS,
      ConstRawData uid = {nullptr, 0});

  ErrorCode SetAddress(uint8_t address, USB::ControlContext context) override;

  void OnControlReady() override;

  void Start(bool in_isr) override;
  void Stop(bool in_isr) override;

  static inline CH32USBOtgFS* self_ = nullptr;
};

#endif  // defined(USBFSD) || defined(__DOXYGEN__)

#if (defined(USBHSD) && defined(LIBXR_CH32_USB_OTG_LEGACY)) || defined(__DOXYGEN__)

/**
 * @brief CH32 OTG HS 设备驱动 / CH32 OTG HS device driver
 */
class CH32USBOtgHS : public USB::EndpointPool,
                     public USB::DeviceCore<USB::HighSpeedCapabilities>
{
 public:
  /**
   * @brief OTG HS 端点配置 / OTG HS endpoint configuration
   */
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

  /**
   * @brief 构造 OTG HS 设备对象 / Construct OTG HS device object
   */
  CH32USBOtgHS(
      const std::initializer_list<EPConfig> EP_CFGS, uint16_t vid, uint16_t pid,
      uint16_t bcd,
      const std::initializer_list<const USB::DescriptorStrings::LanguagePack*> LANG_LIST,
      const std::initializer_list<const std::initializer_list<USB::ConfigDescriptorItem*>>
          CONFIGS,
      ConstRawData uid = {nullptr, 0});

  ErrorCode SetAddress(uint8_t address, USB::ControlContext context) override;

  USB::Speed ReadBusSpeed() const override
  {
    return (USBHSD->SPEED_TYPE & USBHS_USB_SPEED_TYPE) == USBHS_USB_SPEED_HIGH
               ? USB::Speed::HIGH
               : USB::Speed::FULL;
  }

  void OnControlReady() override;

  void Start(bool in_isr) override;
  void Stop(bool in_isr) override;

  static inline CH32USBOtgHS* self_ = nullptr;
};

#endif  // defined(USBHSD) || defined(__DOXYGEN__)

}  // namespace LibXR
