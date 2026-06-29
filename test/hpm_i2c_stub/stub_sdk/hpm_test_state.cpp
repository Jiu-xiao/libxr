#include "hpm_test_state.hpp"

#include <cstring>

extern "C" I2C_Type g_hpm_i2c0{};
DMA_Type g_hpm_dma{};

namespace
{

HpmI2cTestState g_state{};

hpm_stat_t DmaOk(dma_resource_t* resource)
{
  if (resource == nullptr)
  {
    return status_invalid_argument;
  }
  if (resource->base == nullptr)
  {
    resource->base = &g_hpm_dma;
    resource->channel = 0U;
    resource->mux = 0;
  }
  return status_success;
}

}  // namespace

const HpmI2cTestState& hpm_test_state() { return g_state; }

void hpm_test_reset_state()
{
  std::memset(&g_state, 0, sizeof(g_state));
  std::memset(&g_hpm_i2c0, 0, sizeof(g_hpm_i2c0));
}

void hpm_test_trigger_dma_tc()
{
  if (g_state.tc_callback != nullptr)
  {
    g_state.tc_callback(&g_hpm_dma, 0U, g_state.tc_cb_data);
  }
}

extern "C" void hpm_test_record_i2c_issue_command(uint32_t ctrl)
{
  g_state.issue_count++;
  if ((ctrl & I2C_CTRL_PHASE_STOP_MASK) != 0U)
  {
    g_state.stop_issue_count++;
    g_state.last_stop_ctrl = ctrl;
  }
}

extern "C" uint64_t hpm_csr_get_core_cycle(void)
{
  static uint64_t cycle = 0U;
  return ++cycle;
}

extern "C" hpm_stat_t i2c_init_master(I2C_Type* ptr, uint32_t src_clk_in_hz,
                                      i2c_config_t* config)
{
  if (ptr == nullptr || src_clk_in_hz == 0U || config == nullptr)
  {
    return status_invalid_argument;
  }
  ptr->SETUP |= I2C_SETUP_IICEN_MASK;
  return status_success;
}

extern "C" void i2c_reset(I2C_Type* ptr)
{
  if (ptr != nullptr)
  {
    ptr->CMD = I2C_CMD_RESET;
  }
  g_state.i2c_reset_count++;
}

extern "C" hpm_stat_t i2c_master_start_dma_write(I2C_Type* ptr,
                                                 uint16_t device_address,
                                                 uint32_t size)
{
  if (ptr == nullptr || size == 0U)
  {
    return status_invalid_argument;
  }
  ptr->ADDR = device_address;
  ptr->DATA_COUNT = size;
  return status_success;
}

extern "C" hpm_stat_t i2c_master_start_dma_read(I2C_Type* ptr,
                                                uint16_t device_address,
                                                uint32_t size)
{
  if (ptr == nullptr || size == 0U)
  {
    return status_invalid_argument;
  }
  ptr->ADDR = device_address;
  ptr->DATA_COUNT = size;
  return status_success;
}

extern "C" hpm_stat_t i2c_master_read(I2C_Type* ptr, uint16_t device_address,
                                      uint8_t* data, uint32_t size)
{
  (void)device_address;
  return (ptr != nullptr && data != nullptr && size > 0U) ? status_success
                                                          : status_invalid_argument;
}

extern "C" hpm_stat_t i2c_master_write(I2C_Type* ptr, uint16_t device_address,
                                       uint8_t* data, uint32_t size)
{
  (void)device_address;
  return (ptr != nullptr && data != nullptr && size > 0U) ? status_success
                                                          : status_invalid_argument;
}

extern "C" hpm_stat_t i2c_master_address_read(I2C_Type* ptr, uint16_t device_address,
                                              uint8_t* addr, uint32_t addr_size,
                                              uint8_t* data, uint32_t size)
{
  (void)device_address;
  return (ptr != nullptr && addr != nullptr && addr_size > 0U && data != nullptr &&
          size > 0U)
             ? status_success
             : status_invalid_argument;
}

extern "C" hpm_stat_t i2c_master_address_write(I2C_Type* ptr, uint16_t device_address,
                                               uint8_t* addr, uint32_t addr_size,
                                               uint8_t* data, uint32_t size)
{
  (void)device_address;
  (void)data;
  return (ptr != nullptr && addr != nullptr && addr_size > 0U) ? status_success
                                                               : status_invalid_argument;
}

extern "C" hpm_stat_t i2c_master_seq_transmit_check_ack(
    I2C_Type* ptr, uint16_t device_address, uint8_t* data, uint32_t size,
    i2c_seq_transfer_opt_t frame, bool check_ack)
{
  (void)device_address;
  (void)frame;
  (void)check_ack;
  return (ptr != nullptr && data != nullptr && size > 0U) ? status_success
                                                          : status_invalid_argument;
}

extern "C" hpm_stat_t i2c_master_seq_receive(I2C_Type* ptr, uint16_t device_address,
                                             uint8_t* data, uint32_t size,
                                             i2c_seq_transfer_opt_t frame)
{
  (void)device_address;
  (void)frame;
  return (ptr != nullptr && data != nullptr && size > 0U) ? status_success
                                                          : status_invalid_argument;
}

extern "C" hpm_stat_t i2c_master_transfer(I2C_Type* ptr, uint16_t device_address,
                                          uint8_t* data, uint32_t size, uint16_t flags)
{
  (void)device_address;
  (void)flags;
  return (ptr != nullptr && data != nullptr && size > 0U) ? status_success
                                                          : status_invalid_argument;
}

extern "C" void dma_mgr_init(void) {}

extern "C" hpm_stat_t dma_mgr_request_resource(dma_resource_t* resource)
{
  return DmaOk(resource);
}

extern "C" hpm_stat_t dma_mgr_request_specified_resource(dma_resource_t* resource,
                                                         DMA_Type* base)
{
  if (resource == nullptr)
  {
    return status_invalid_argument;
  }
  resource->base = base != nullptr ? base : &g_hpm_dma;
  resource->channel = 0U;
  resource->mux = 0;
  return status_success;
}

extern "C" hpm_stat_t dma_mgr_release_resource(dma_resource_t* resource)
{
  if (resource != nullptr)
  {
    resource->base = nullptr;
  }
  return status_success;
}

extern "C" void dma_mgr_get_default_chn_config(dma_mgr_chn_conf_t* cfg)
{
  if (cfg != nullptr)
  {
    *cfg = {};
  }
}

extern "C" hpm_stat_t dma_mgr_setup_channel(dma_resource_t* resource,
                                            dma_mgr_chn_conf_t* cfg)
{
  (void)cfg;
  return DmaOk(resource);
}

extern "C" hpm_stat_t dma_mgr_install_chn_tc_callback(dma_resource_t* resource,
                                                      dma_mgr_callback_t callback,
                                                      void* cb_data)
{
  hpm_stat_t ans = DmaOk(resource);
  if (ans == status_success)
  {
    g_state.tc_callback = callback;
    g_state.tc_cb_data = cb_data;
  }
  return ans;
}

extern "C" hpm_stat_t dma_mgr_install_chn_error_callback(dma_resource_t* resource,
                                                         dma_mgr_callback_t callback,
                                                         void* cb_data)
{
  (void)callback;
  (void)cb_data;
  return DmaOk(resource);
}

extern "C" hpm_stat_t dma_mgr_install_chn_abort_callback(dma_resource_t* resource,
                                                         dma_mgr_callback_t callback,
                                                         void* cb_data)
{
  (void)callback;
  (void)cb_data;
  return DmaOk(resource);
}

extern "C" hpm_stat_t dma_mgr_enable_chn_irq(dma_resource_t* resource, uint32_t mask)
{
  (void)mask;
  return DmaOk(resource);
}

extern "C" hpm_stat_t dma_mgr_enable_dma_irq_with_priority(dma_resource_t* resource,
                                                           uint32_t priority)
{
  (void)priority;
  return DmaOk(resource);
}

extern "C" hpm_stat_t dma_mgr_disable_channel(dma_resource_t* resource)
{
  g_state.dma_disable_count++;
  return DmaOk(resource);
}

extern "C" hpm_stat_t dma_mgr_enable_channel(dma_resource_t* resource)
{
  hpm_stat_t ans = DmaOk(resource);
  if (ans == status_success)
  {
    g_state.dma_enable_count++;
  }
  return ans;
}

extern "C" hpm_stat_t dma_mgr_set_chn_dst_addr(dma_resource_t* resource, uint32_t addr)
{
  (void)addr;
  return DmaOk(resource);
}

extern "C" hpm_stat_t dma_mgr_set_chn_dst_work_mode(dma_resource_t* resource,
                                                    uint32_t mode)
{
  (void)mode;
  return DmaOk(resource);
}

extern "C" hpm_stat_t dma_mgr_set_chn_dst_addr_ctrl(dma_resource_t* resource,
                                                    uint32_t ctrl)
{
  (void)ctrl;
  return DmaOk(resource);
}

extern "C" hpm_stat_t dma_mgr_set_chn_src_addr(dma_resource_t* resource, uint32_t addr)
{
  (void)addr;
  return DmaOk(resource);
}

extern "C" hpm_stat_t dma_mgr_set_chn_src_work_mode(dma_resource_t* resource,
                                                    uint32_t mode)
{
  (void)mode;
  return DmaOk(resource);
}

extern "C" hpm_stat_t dma_mgr_set_chn_src_addr_ctrl(dma_resource_t* resource,
                                                    uint32_t ctrl)
{
  (void)ctrl;
  return DmaOk(resource);
}

extern "C" hpm_stat_t dma_mgr_set_chn_src_width(dma_resource_t* resource,
                                                uint32_t width)
{
  (void)width;
  return DmaOk(resource);
}

extern "C" hpm_stat_t dma_mgr_set_chn_dst_width(dma_resource_t* resource,
                                                uint32_t width)
{
  (void)width;
  return DmaOk(resource);
}

extern "C" hpm_stat_t dma_mgr_set_chn_transize(dma_resource_t* resource,
                                               uint32_t size)
{
  (void)size;
  return DmaOk(resource);
}
