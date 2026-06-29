#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hpm_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DMA_Type {
  uint32_t RESERVED;
} DMA_Type;

typedef struct dma_resource {
  DMA_Type* base;
  uint32_t channel;
  int32_t mux;
} dma_resource_t;

typedef struct dma_mgr_chn_conf {
  uint32_t src_width;
  uint32_t dst_width;
  bool en_dmamux;
  uint8_t dmamux_src;
  uint32_t interrupt_mask;
} dma_mgr_chn_conf_t;

typedef void (*dma_mgr_callback_t)(DMA_Type* base, uint32_t channel, void* cb_data_ptr);

#define DMA_MGR_TRANSFER_WIDTH_BYTE 1U
#define DMA_MGR_HANDSHAKE_MODE_NORMAL 0U
#define DMA_MGR_HANDSHAKE_MODE_HANDSHAKE 1U
#define DMA_MGR_ADDRESS_CONTROL_INCREMENT 0U
#define DMA_MGR_ADDRESS_CONTROL_FIXED 1U
#define DMA_MGR_INTERRUPT_MASK_ALL 0xFFFFFFFFU
#define DMA_MGR_INTERRUPT_MASK_TC (1U << 0)
#define DMA_MGR_INTERRUPT_MASK_ERROR (1U << 1)
#define DMA_MGR_INTERRUPT_MASK_ABORT (1U << 2)

void dma_mgr_init(void);
hpm_stat_t dma_mgr_request_resource(dma_resource_t* resource);
hpm_stat_t dma_mgr_request_specified_resource(dma_resource_t* resource, DMA_Type* base);
hpm_stat_t dma_mgr_release_resource(dma_resource_t* resource);
void dma_mgr_get_default_chn_config(dma_mgr_chn_conf_t* cfg);
hpm_stat_t dma_mgr_setup_channel(dma_resource_t* resource, dma_mgr_chn_conf_t* cfg);
hpm_stat_t dma_mgr_install_chn_tc_callback(dma_resource_t* resource,
                                           dma_mgr_callback_t callback, void* cb_data);
hpm_stat_t dma_mgr_install_chn_error_callback(dma_resource_t* resource,
                                              dma_mgr_callback_t callback, void* cb_data);
hpm_stat_t dma_mgr_install_chn_abort_callback(dma_resource_t* resource,
                                              dma_mgr_callback_t callback, void* cb_data);
hpm_stat_t dma_mgr_enable_chn_irq(dma_resource_t* resource, uint32_t mask);
hpm_stat_t dma_mgr_enable_dma_irq_with_priority(dma_resource_t* resource, uint32_t priority);
hpm_stat_t dma_mgr_disable_channel(dma_resource_t* resource);
hpm_stat_t dma_mgr_enable_channel(dma_resource_t* resource);
hpm_stat_t dma_mgr_set_chn_dst_addr(dma_resource_t* resource, uint32_t addr);
hpm_stat_t dma_mgr_set_chn_dst_work_mode(dma_resource_t* resource, uint32_t mode);
hpm_stat_t dma_mgr_set_chn_dst_addr_ctrl(dma_resource_t* resource, uint32_t ctrl);
hpm_stat_t dma_mgr_set_chn_src_addr(dma_resource_t* resource, uint32_t addr);
hpm_stat_t dma_mgr_set_chn_src_work_mode(dma_resource_t* resource, uint32_t mode);
hpm_stat_t dma_mgr_set_chn_src_addr_ctrl(dma_resource_t* resource, uint32_t ctrl);
hpm_stat_t dma_mgr_set_chn_src_width(dma_resource_t* resource, uint32_t width);
hpm_stat_t dma_mgr_set_chn_dst_width(dma_resource_t* resource, uint32_t width);
hpm_stat_t dma_mgr_set_chn_transize(dma_resource_t* resource, uint32_t size);

static inline void dma_clear_transfer_status(DMA_Type* base, uint32_t channel)
{
  (void)base;
  (void)channel;
}

#ifdef __cplusplus
}
#endif
