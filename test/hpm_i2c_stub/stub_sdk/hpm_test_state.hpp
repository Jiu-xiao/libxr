#pragma once

#include <cstdint>

#include "hpm_dma_mgr.h"
#include "hpm_i2c_drv.h"

struct HpmI2cTestState
{
  uint32_t issue_count;
  uint32_t stop_issue_count;
  uint32_t last_stop_ctrl;
  uint32_t dma_enable_count;
  uint32_t dma_disable_count;
  uint32_t i2c_reset_count;
  dma_mgr_callback_t tc_callback;
  void* tc_cb_data;
};

const HpmI2cTestState& hpm_test_state();
void hpm_test_reset_state();
void hpm_test_trigger_dma_tc();
