#include "main.h"

#if defined(HAL_PCD_MODULE_ENABLED) && (defined(USB_OTG_FS) || defined(USB_OTG_HS))

// CubeMX keeps its default code: MX_USB_OTG_*_PCD_Init() calls HAL_PCD_Init() and the
// generated OTG_*_IRQHandler() calls HAL_PCD_IRQHandler(). The link wraps both (see
// CMakeLists.txt). This file holds only the wrappers, so it is linked whenever CubeMX
// references them; the register runtime in stm32_usb_otg.cpp is linked only when a device
// object is constructed, and it is reached through a weak reference. Without it the
// controller stays with HAL, for a project that drives USB through ST's middleware.

extern "C" void libxr_stm32_otg_dispatch(PCD_HandleTypeDef* hpcd) __attribute__((weak));

extern "C" HAL_StatusTypeDef __real_HAL_PCD_Init(PCD_HandleTypeDef* hpcd);
extern "C" void __real_HAL_PCD_IRQHandler(PCD_HandleTypeDef* hpcd);

extern "C" HAL_StatusTypeDef __wrap_HAL_PCD_Init(PCD_HandleTypeDef* hpcd)
{
  // The driver initializes the core when it is constructed.
  if (libxr_stm32_otg_dispatch != nullptr) return HAL_OK;
  return __real_HAL_PCD_Init(hpcd);
}

extern "C" void __wrap_HAL_PCD_IRQHandler(PCD_HandleTypeDef* hpcd)
{
  if (libxr_stm32_otg_dispatch != nullptr)
  {
    libxr_stm32_otg_dispatch(hpcd);
    return;
  }
  __real_HAL_PCD_IRQHandler(hpcd);
}

#endif
