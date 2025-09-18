#pragma once

#include "double_buffer.hpp"
#include "main.h"

#undef USB

#include "usb/core/ep_pool.hpp"
#include "usb/device/dev_core.hpp"

#if defined(HAL_PCD_MODULE_ENABLED)

typedef enum : uint8_t
{
#if (defined(USB_BASE))
  STM32_USB_FS_DEV,
#endif
#if (defined(USB_OTG_FS))
  STM32_USB_OTG_FS,
#endif
#if (defined(USB_OTG_HS))
  STM32_USB_OTG_HS,
#endif
  STM32_USB_DEV_ID_NUM
} stm32_usb_dev_id_t;

#endif
