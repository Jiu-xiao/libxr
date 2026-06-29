#pragma once

#include <stdint.h>

#define UNUSED(x) (void)(x)

typedef int32_t hpm_stat_t;

#define status_group_common 0
#define status_group_i2c 6
#define status_group_dma_mgr 11
#define MAKE_STATUS(group, code) \
  ((hpm_stat_t)((((uint32_t)(group)) << 16U) | ((uint32_t)(code))))

enum
{
  status_success = MAKE_STATUS(status_group_common, 0),
  status_fail = MAKE_STATUS(status_group_common, 1),
  status_invalid_argument = MAKE_STATUS(status_group_common, 2),
  status_timeout = MAKE_STATUS(status_group_common, 3),
  status_i2c_bus_busy = MAKE_STATUS(status_group_i2c, 1),
  status_i2c_not_supported = MAKE_STATUS(status_group_i2c, 2),
  status_i2c_no_ack = MAKE_STATUS(status_group_i2c, 3),
  status_i2c_no_addr_hit = MAKE_STATUS(status_group_i2c, 4),
  status_i2c_invalid_data = MAKE_STATUS(status_group_i2c, 5),
  status_i2c_transmit_not_completed = MAKE_STATUS(status_group_i2c, 6),
  status_dma_mgr_no_resource = MAKE_STATUS(status_group_dma_mgr, 1),
};
