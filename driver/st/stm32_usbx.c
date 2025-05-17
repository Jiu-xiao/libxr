#include "main.h"
#if defined(HAL_PCD_MODULE_ENABLED) && defined(LIBXR_SYSTEM_ThreadX)

#include "tx_api.h"
#include "ux_api.h"
#include "ux_dcd_stm32.h"

/**
 * @brief ux_dcd_stm32_initialize的封装 wrapping of ux_dcd_stm32_initialize
 * 
 * @param dcd_io
 * @param parameter 
 */
void usbx_dcd_stm32_initialize(ULONG dcd_io, ULONG parameter)
{
  ux_dcd_stm32_initialize(dcd_io, parameter);
}

#endif