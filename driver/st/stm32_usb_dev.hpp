#pragma once

#include "double_buffer.hpp"
#include "main.h"
#include "stm32_usb.hpp"
#include "stm32_usb_ep.hpp"
#include "usb/core/ep_pool.hpp"
#include "usb/device/dev_core.hpp"

#if defined(HAL_PCD_MODULE_ENABLED)

// NOLINTNEXTLINE
stm32_usb_dev_id_t STM32USBDeviceGetID(PCD_HandleTypeDef* hpcd);

namespace LibXR
{

class STM32USBDevice : public LibXR::USB::EndpointPool, public LibXR::USB::DeviceCore
{
 public:
  STM32USBDevice(
      PCD_HandleTypeDef* hpcd, stm32_usb_dev_id_t id, size_t max_ep_num,
      USB::DeviceDescriptor::PacketSize0 packet_size, uint16_t vid, uint16_t pid,
      uint16_t bcd,
      const std::initializer_list<const USB::DescriptorStrings::LanguagePack*> LANG_LIST,
      const std::initializer_list<const std::initializer_list<USB::ConfigDescriptorItem*>>
          CONFIGS)
      : LibXR::USB::EndpointPool(max_ep_num),
        LibXR::USB::DeviceCore(*this, USB::USBSpec::USB_2_0, USB::Speed::FULL,
                               packet_size, vid, pid, bcd, LANG_LIST, CONFIGS),
        hpcd_(hpcd),
        id_(id)
  {
    map_[id] = this;
  }

  void Init() override { LibXR::USB::DeviceCore::Init(); }

  void Start() { HAL_PCD_Start(hpcd_); }
  void Stop() { HAL_PCD_Stop(hpcd_); }

  ErrorCode SetAddress(uint8_t address, USB::DeviceCore::Context context) override
  {
    HAL_StatusTypeDef ans = HAL_OK;
#if defined(USB_OTG_FS) || defined(USB_OTG_HS)
    // OTG (F4/F7/H7): 地址设置立即生效，直接在 SETUP 阶段调用
    if (context == USB::DeviceCore::Context::SETUP)
#else
    // devfs (F1): 必须等状态阶段完成后再调用（IN完成回调）
    if (context == USB::DeviceCore::Context::DATA_IN)
#endif
    {
      ans = HAL_PCD_SetAddress(hpcd_, address);
    }
    return (ans == HAL_OK) ? ErrorCode::OK : ErrorCode::FAILED;
  }

  PCD_HandleTypeDef* hpcd_;
  stm32_usb_dev_id_t id_;
  static inline STM32USBDevice* map_[STM32_USB_DEV_ID_NUM] = {};
};

class STM32USBDeviceOtgFS : public STM32USBDevice
{
 public:
  struct EPInConfig
  {
    LibXR::RawData buffer;
    size_t fifo_size;
  };

  STM32USBDeviceOtgFS(
      PCD_HandleTypeDef* hpcd, size_t rx_fifo_size,
      const std::initializer_list<LibXR::RawData> RX_EP_CFGS,
      const std::initializer_list<EPInConfig> TX_EP_CFGS,
      USB::DeviceDescriptor::PacketSize0 packet_size, uint16_t vid, uint16_t pid,
      uint16_t bcd,
      const std::initializer_list<const USB::DescriptorStrings::LanguagePack*> LANG_LIST,
      const std::initializer_list<const std::initializer_list<USB::ConfigDescriptorItem*>>
          CONFIGS);

  ErrorCode SetAddress(uint8_t address, USB::DeviceCore::Context context) override;
};

}  // namespace LibXR
#endif
