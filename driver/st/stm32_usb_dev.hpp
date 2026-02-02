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
      USB::USBSpec spec = USB::USBSpec::USB_2_1)
      : LibXR::USB::EndpointPool(max_ep_num),
        LibXR::USB::DeviceCore(*this, spec, speed, packet_size, vid, pid, bcd, LANG_LIST,
                               CONFIGS, uid),
        hpcd_(hpcd),
        id_(id)
  {
    map_[id] = this;
  }

  void Init(bool in_isr) override { LibXR::USB::DeviceCore::Init(in_isr); }

  void Deinit(bool in_isr) override { LibXR::USB::DeviceCore::Deinit(in_isr); }

  void Start(bool) override { HAL_PCD_Start(hpcd_); }
  void Stop(bool) override { HAL_PCD_Stop(hpcd_); }

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

#if defined(PMA_END_ADDR)
#define LIBXR_STM32_USB_PMA_SIZE PMA_END_ADDR
// --- F0: USB FS Device, PMA = 1024B (dedicated) -------------------------
#elif defined(STM32F0)
#define LIBXR_STM32_USB_PMA_SIZE 1024u

// --- F1: F102/F103 USB FS Device, PMA = 512B，与 CAN 共用，不能并行使用 ---
#elif defined(STM32F102xB) || defined(STM32F102xC) || defined(STM32F103x6) || \
    defined(STM32F103xB) || defined(STM32F103xE) || defined(STM32F103xG)
#define LIBXR_STM32_USB_PMA_SIZE 512u

#if defined(HAL_CAN_MODULE_ENABLED)
#error "STM32F102/F103 cannot use CAN at the same time with USB."
#endif

// --- F3 小容量带 USB ：PMA = 512B，专用给 USB --------------------
#elif defined(STM32F302xB) || defined(STM32F302xC) || defined(STM32F303xB) || \
    defined(STM32F303xC) || defined(STM32F373xC)
#define LIBXR_STM32_USB_PMA_SIZE 512u

// --- F3 大容量（和 CAN 共享 1KB）的几种：F302x6/8,D/E & F303xD/E -------
// - 总共 1KB USB+CAN SRAM
// - 若 CAN 在用：USB 只能用前 768B，最后 256B 给 CAN
#elif defined(STM32F302x6) || defined(STM32F302x8) || defined(STM32F302xD) || \
    defined(STM32F302xE) || defined(STM32F303xD) || defined(STM32F303xE)

#if defined(HAL_CAN_MODULE_ENABLED)
// 给 USB 768B
#define LIBXR_STM32_USB_PMA_SIZE 768u
#else
// 整 1KB 给 USB
#define LIBXR_STM32_USB_PMA_SIZE 1024u
#endif

// --- L0: USB FS Device, PMA = 1024B (dedicated) -------------------------
#elif defined(STM32L0)
#define LIBXR_STM32_USB_PMA_SIZE 1024u

// --- L1: USB FS Device, PMA = 512B (dedicated) --------------------------
#elif defined(STM32L1)
#define LIBXR_STM32_USB_PMA_SIZE 512u

// --- G4: USB FS Device (V1), PMA = 1024B (dedicated) --------------------
#elif defined(STM32G4)
#define LIBXR_STM32_USB_PMA_SIZE 1024u

// --- G0: USB_DRD_FS (V2), PMA = 2048B (dedicated) -----------------------
#elif defined(STM32G0)
#define LIBXR_STM32_USB_PMA_SIZE 2048u

// --- C0: USB_DRD_FS (V2), PMA = 2048B (dedicated) -----------------------
#elif defined(STM32C0)
#define LIBXR_STM32_USB_PMA_SIZE 2048u

// --- H5（H503/563/573/562 DRD_FS），PMA = 2048B -------------------------
#elif defined(STM32H503xx) || defined(STM32H563xx) || defined(STM32H573xx) || \
    defined(STM32H562xx)
#define LIBXR_STM32_USB_PMA_SIZE 2048u

// --- WB55/35: USB FS Device, PMA = 1024B (专门给 USB) ------------------
#elif defined(STM32WB)
#define LIBXR_STM32_USB_PMA_SIZE 1024u

// --- L4: USB FS Device (非 OTG)，PMA = 1024B ----------------------------
#elif defined(STM32L4)
#define LIBXR_STM32_USB_PMA_SIZE 1024u

// --- L5: USB FS Device/DRD，PMA = 1024B -------------------------------
#elif defined(STM32L5)
#define LIBXR_STM32_USB_PMA_SIZE 1024u

// --- U5: USB_DRD_FS (V2)，PMA = 2048B -----------------------------------
#elif defined(STM32U5)
#define LIBXR_STM32_USB_PMA_SIZE 2048u

// --- U0: USB_DRD_FS (V2，1KB 专用) --------------------------------------
#elif defined(STM32U0)
#define LIBXR_STM32_USB_PMA_SIZE 1024u

// --- U3: USB_DRD_FS (V2，2KB 专用) --------------------------------------
#elif defined(STM32U3)
#define LIBXR_STM32_USB_PMA_SIZE 2048u

#else
#error \
    "Unknown STM32 USB FS/DRD with PMA. Please define LIBXR_STM32_USB_PMA_SIZE manually."
#endif

class STM32USBDeviceDevFs : public STM32USBDevice
{
 public:
  struct EPConfig
  {
    LibXR::RawData buffer1, buffer2;
    size_t hw_buffer_size1, hw_buffer_size2;
    bool double_buffer_is_in = false;

    EPConfig(LibXR::RawData buffer, size_t hw_buffer_size, bool is_in)
        : buffer1(buffer),
          buffer2(nullptr, 0),
          hw_buffer_size1(hw_buffer_size),
          hw_buffer_size2(0),
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
