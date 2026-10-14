#pragma once

#include "double_buffer.hpp"
#include "main.h"

#undef USB

#include "usb/core/ep_pool.hpp"
#include "usb/device/dev_core.hpp"

#if defined(HAL_PCD_MODULE_ENABLED) && !(defined(USB_OTG_FS) || defined(USB_OTG_HS))

/**
 * @brief STM32 USB 设备编号 / STM32 USB device identifier
 */
typedef enum : uint8_t
{
  STM32_USB_FS_DEV,
  STM32_USB_DEV_ID_NUM
} stm32_usb_dev_id_t;

static inline bool STM32USBUsesDma(PCD_HandleTypeDef* hpcd)
{
  UNUSED(hpcd);
  return false;
}

#endif
