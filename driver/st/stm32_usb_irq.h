#pragma once

#include "main.h"

#if defined(HAL_PCD_MODULE_ENABLED)

#ifdef __cplusplus
extern "C"
{
#endif

  /**
   * @brief 在对应 LibXR USB device owner 内处理 HAL PCD IRQ
   *
   * CubeMX 生成的 C ISR 必须调用本入口，不能直接调用 HAL_PCD_IRQHandler。
   *
   * @param hpcd 产生当前 IRQ 的 HAL PCD handle
   */
  void STM32USBDeviceIrqHandler(PCD_HandleTypeDef* hpcd);

#ifdef __cplusplus
}
#endif

#endif
