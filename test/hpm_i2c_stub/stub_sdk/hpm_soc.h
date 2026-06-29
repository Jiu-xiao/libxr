#pragma once

#include <stdint.h>

#include "hpm_clock_drv.h"
#include "hpm_common.h"
#include "hpm_i2c_drv.h"

#define HPM_CORE0 0

#ifdef __cplusplus
extern "C" {
#endif

extern I2C_Type g_hpm_i2c0;
uint64_t hpm_csr_get_core_cycle(void);

#ifdef __cplusplus
}
#endif

#define HPM_I2C0 (&g_hpm_i2c0)
#define IRQn_I2C0 10
#define HPM_DMA_SRC_I2C0 4U

static inline uint32_t core_local_mem_to_sys_address(uint32_t core, uint32_t addr)
{
  (void)core;
  return addr;
}
