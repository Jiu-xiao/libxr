#pragma once

#include "libxr_def.hpp"
#include DEF2STR(LIBXR_CH32_CONFIG_FILE)

namespace LibXR
{

// 设备ID枚举（对标STM32的 stm32_usb_dev_id_t）
typedef enum : uint8_t
{
#if defined(USBFSD)
  CH32_USB_OTG_FS,
#endif
#if defined(USBHSD)
  CH32_USB_OTG_HS,
#endif
  // CH32_USB_HS_DEV, // 如需支持HS/OTG可补充
  CH32_USB_DEV_ID_NUM
} ch32_usb_dev_id_t;

}  // namespace LibXR
