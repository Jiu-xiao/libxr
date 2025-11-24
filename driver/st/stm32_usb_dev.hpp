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
          CONFIGS,
      ConstRawData uid = {nullptr, 0}, USB::Speed speed = USB::Speed::FULL,
      USB::USBSpec spec = USB::USBSpec::USB_2_0)
      : LibXR::USB::EndpointPool(max_ep_num),
        LibXR::USB::DeviceCore(*this, spec, speed, packet_size, vid, pid, bcd, LANG_LIST,
                               CONFIGS, uid),
        hpcd_(hpcd),
        id_(id)
  {
    map_[id] = this;
  }

  void Init() override { LibXR::USB::DeviceCore::Init(); }

  void Deinit() override { LibXR::USB::DeviceCore::Deinit(); }

  void Start() override { HAL_PCD_Start(hpcd_); }
  void Stop() override { HAL_PCD_Stop(hpcd_); }

  PCD_HandleTypeDef* hpcd_;
  stm32_usb_dev_id_t id_;
  static inline STM32USBDevice* map_[STM32_USB_DEV_ID_NUM] = {};
};

#if defined(USB_OTG_FS)
#if !defined(USB_OTG_FS_TOTAL_FIFO_SIZE)
#if defined(STM32H7) || defined(STM32N6)
#define USB_OTG_FS_TOTAL_FIFO_SIZE 4096
#else
#define USB_OTG_FS_TOTAL_FIFO_SIZE 1280
#endif
#endif
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
          CONFIGS,
      ConstRawData uid = {nullptr, 0});

  ErrorCode SetAddress(uint8_t address, USB::DeviceCore::Context context) override;
};

#endif

#if defined(USB_OTG_HS)
#if !defined(USB_OTG_HS_TOTAL_FIFO_SIZE)
#define USB_OTG_HS_TOTAL_FIFO_SIZE 4096
#endif
class STM32USBDeviceOtgHS : public STM32USBDevice
{
 public:
  struct EPInConfig
  {
    LibXR::RawData buffer;
    size_t fifo_size;
  };

  STM32USBDeviceOtgHS(
      PCD_HandleTypeDef* hpcd, size_t rx_fifo_size,
      const std::initializer_list<LibXR::RawData> RX_EP_CFGS,
      const std::initializer_list<EPInConfig> TX_EP_CFGS,
      USB::DeviceDescriptor::PacketSize0 packet_size, uint16_t vid, uint16_t pid,
      uint16_t bcd,
      const std::initializer_list<const USB::DescriptorStrings::LanguagePack*> LANG_LIST,
      const std::initializer_list<const std::initializer_list<USB::ConfigDescriptorItem*>>
          CONFIGS,
      ConstRawData uid = {nullptr, 0});

  ErrorCode SetAddress(uint8_t address, USB::DeviceCore::Context context) override;
};
#endif

#if defined(USB_BASE)
class STM32USBDeviceDevFs : public STM32USBDevice
{
 public:
  struct EPConfig
  {
    LibXR::RawData buffer1, buffer2;
    size_t hw_buffer_size1, hw_buffer_size2;
    bool double_buffer = false;
    bool double_buffer_is_in = false;

    EPConfig(LibXR::RawData buffer, size_t hw_buffer_size, bool is_in)
        : buffer1(buffer),
          buffer2(nullptr, 0),
          hw_buffer_size1(hw_buffer_size),
          hw_buffer_size2(0),
          double_buffer(true),
          double_buffer_is_in(is_in)
    {
    }

    EPConfig(LibXR::RawData buffer_in, LibXR::RawData buffer_out,
             size_t hw_buffer_size_in, size_t hw_buffer_size_out)
        : buffer1(buffer_in),
          buffer2(buffer_out),
          hw_buffer_size1(hw_buffer_size_in),
          hw_buffer_size2(hw_buffer_size_out)
    {
    }

    EPConfig() = delete;
  };

  STM32USBDeviceDevFs(
      PCD_HandleTypeDef* hpcd, const std::initializer_list<EPConfig> EP_CFGS,
      USB::DeviceDescriptor::PacketSize0 packet_size, uint16_t vid, uint16_t pid,
      uint16_t bcd,
      const std::initializer_list<const USB::DescriptorStrings::LanguagePack*> LANG_LIST,
      const std::initializer_list<const std::initializer_list<USB::ConfigDescriptorItem*>>
          CONFIGS,
      ConstRawData uid = {nullptr, 0});

  ErrorCode SetAddress(uint8_t address, USB::DeviceCore::Context context) override;
};
#endif

}  // namespace LibXR
#endif
