#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hpm_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct I2C_Type {
  volatile uint32_t STATUS;
  volatile uint32_t CTRL;
  volatile uint32_t CMD;
  volatile uint32_t SETUP;
  volatile uint32_t DATA;
  volatile uint32_t ADDR;
  volatile uint32_t INTEN;
  volatile uint32_t DATA_COUNT;
} I2C_Type;

#define I2C_SOC_TRANSFER_COUNT_MAX 255U

#define I2C_STATUS_CMPL_MASK (1UL << 0)
#define I2C_STATUS_ADDRHIT_MASK (1UL << 1)
#define I2C_STATUS_BUSBUSY_MASK (1UL << 2)
#define I2C_STATUS_BYTETRANS_MASK (1UL << 3)
#define I2C_STATUS_FIFOEMPTY_MASK (1UL << 4)
#define I2C_STATUS_FIFOFULL_MASK (1UL << 5)
#define I2C_STATUS_ARBLOSE_MASK (1UL << 6)
#define I2C_STATUS_ACK_MASK (1UL << 7)
#define I2C_STATUS_ACK_GET(status) (((status) & I2C_STATUS_ACK_MASK) != 0U)

#define I2C_CTRL_PHASE_START_MASK (1UL << 0)
#define I2C_CTRL_PHASE_ADDR_MASK (1UL << 1)
#define I2C_CTRL_PHASE_DATA_MASK (1UL << 2)
#define I2C_CTRL_PHASE_STOP_MASK (1UL << 3)
#define I2C_CTRL_DIR_MASK (1UL << 4)
#define I2C_CTRL_DIR_SET(x) ((x) ? I2C_CTRL_DIR_MASK : 0U)

#define I2C_CMD_RESET (1UL << 0)
#define I2C_CMD_CLEAR_FIFO (1UL << 1)
#define I2C_CMD_ISSUE_DATA_TRANSMISSION (1UL << 2)

#define I2C_SETUP_DMAEN_MASK (1UL << 0)
#define I2C_SETUP_IICEN_MASK (1UL << 1)

#define I2C_EVENT_TRANSACTION_COMPLETE (1UL << 0)
#define I2C_EVENT_LOSS_ARBITRATION (1UL << 1)
#define I2C_EVENT_BYTE_RECEIVED (1UL << 2)

#define I2C_RD (1U << 0)
#define I2C_ADDR_10BIT (1U << 1)
#define I2C_NO_START (1U << 2)
#define I2C_NO_ADDRESS (1U << 3)
#define I2C_NO_READ_ACK (1U << 4)
#define I2C_NO_STOP (1U << 5)
#define I2C_WRITE_CHECK_ACK (1U << 6)

#define I2C_DIR_MASTER_WRITE false
#define I2C_DIR_MASTER_READ true

typedef enum i2c_mode {
  i2c_mode_normal = 0,
  i2c_mode_fast,
  i2c_mode_fast_plus,
} i2c_mode_t;

typedef struct i2c_config {
  i2c_mode_t i2c_mode;
  bool is_10bit_addressing;
} i2c_config_t;

typedef enum i2c_seq_transfer_opt {
  i2c_frist_frame = 0,
  i2c_next_frame,
  i2c_last_frame,
} i2c_seq_transfer_opt_t;

void hpm_test_record_i2c_issue_command(uint32_t ctrl);

static inline uint32_t i2c_get_status(I2C_Type* ptr) { return ptr->STATUS; }

static inline void i2c_clear_status(I2C_Type* ptr, uint32_t mask)
{
  ptr->STATUS &= ~mask;
}

static inline void i2c_clear_fifo(I2C_Type* ptr) { ptr->CMD = I2C_CMD_CLEAR_FIFO; }

static inline void i2c_dma_enable(I2C_Type* ptr) { ptr->SETUP |= I2C_SETUP_DMAEN_MASK; }

static inline void i2c_dma_disable(I2C_Type* ptr) { ptr->SETUP &= ~I2C_SETUP_DMAEN_MASK; }

static inline void i2c_master_set_slave_address(I2C_Type* ptr, uint16_t address)
{
  ptr->ADDR = address;
}

static inline void i2c_set_direction(I2C_Type* ptr, bool direction)
{
  ptr->CTRL = (ptr->CTRL & ~I2C_CTRL_DIR_MASK) | I2C_CTRL_DIR_SET(direction);
}

static inline void i2c_master_enable_start_phase(I2C_Type* ptr)
{
  ptr->CTRL |= I2C_CTRL_PHASE_START_MASK;
}

static inline void i2c_master_disable_start_phase(I2C_Type* ptr)
{
  ptr->CTRL &= ~I2C_CTRL_PHASE_START_MASK;
}

static inline void i2c_master_enable_addr_phase(I2C_Type* ptr)
{
  ptr->CTRL |= I2C_CTRL_PHASE_ADDR_MASK;
}

static inline void i2c_master_disable_addr_phase(I2C_Type* ptr)
{
  ptr->CTRL &= ~I2C_CTRL_PHASE_ADDR_MASK;
}

static inline void i2c_master_enable_data_phase(I2C_Type* ptr)
{
  ptr->CTRL |= I2C_CTRL_PHASE_DATA_MASK;
}

static inline void i2c_master_disable_data_phase(I2C_Type* ptr)
{
  ptr->CTRL &= ~I2C_CTRL_PHASE_DATA_MASK;
}

static inline void i2c_master_enable_stop_phase(I2C_Type* ptr)
{
  ptr->CTRL |= I2C_CTRL_PHASE_STOP_MASK;
}

static inline void i2c_master_disable_stop_phase(I2C_Type* ptr)
{
  ptr->CTRL &= ~I2C_CTRL_PHASE_STOP_MASK;
}

static inline hpm_stat_t i2c_set_data_count(I2C_Type* ptr, uint32_t size)
{
  if (size > I2C_SOC_TRANSFER_COUNT_MAX) {
    return status_invalid_argument;
  }
  ptr->DATA_COUNT = size;
  return status_success;
}

static inline uint32_t i2c_get_data_count(I2C_Type* ptr) { return ptr->DATA_COUNT; }

static inline void i2c_master_issue_data_transmission(I2C_Type* ptr)
{
  hpm_test_record_i2c_issue_command(ptr->CTRL);
  ptr->CMD = I2C_CMD_ISSUE_DATA_TRANSMISSION;
  if ((ptr->CTRL & I2C_CTRL_PHASE_STOP_MASK) != 0U) {
    ptr->STATUS |= I2C_STATUS_CMPL_MASK;
    ptr->DATA_COUNT = 0U;
  }
}

static inline void i2c_write_byte(I2C_Type* ptr, uint8_t data) { ptr->DATA = data; }

static inline uint8_t i2c_read_byte(I2C_Type* ptr) { return (uint8_t)ptr->DATA; }

static inline void i2c_respond_Nack(I2C_Type* ptr) { ptr->STATUS &= ~I2C_STATUS_ACK_MASK; }

static inline void i2c_respond_ack(I2C_Type* ptr) { ptr->STATUS |= I2C_STATUS_ACK_MASK; }

static inline void i2c_enable_10bit_address_mode(I2C_Type* ptr, bool enable)
{
  (void)ptr;
  (void)enable;
}

static inline bool i2c_get_line_scl_status(I2C_Type* ptr)
{
  (void)ptr;
  return true;
}

static inline bool i2c_get_line_sda_status(I2C_Type* ptr)
{
  (void)ptr;
  return true;
}

static inline void i2c_enable_irq(I2C_Type* ptr, uint32_t mask) { ptr->INTEN |= mask; }

static inline void i2c_disable_irq(I2C_Type* ptr, uint32_t mask) { ptr->INTEN &= ~mask; }

hpm_stat_t i2c_init_master(I2C_Type* ptr, uint32_t src_clk_in_hz, i2c_config_t* config);
void i2c_reset(I2C_Type* ptr);
hpm_stat_t i2c_master_start_dma_write(I2C_Type* ptr, uint16_t device_address,
                                      uint32_t size);
hpm_stat_t i2c_master_start_dma_read(I2C_Type* ptr, uint16_t device_address,
                                     uint32_t size);
hpm_stat_t i2c_master_read(I2C_Type* ptr, uint16_t device_address, uint8_t* data,
                           uint32_t size);
hpm_stat_t i2c_master_write(I2C_Type* ptr, uint16_t device_address, uint8_t* data,
                            uint32_t size);
hpm_stat_t i2c_master_address_read(I2C_Type* ptr, uint16_t device_address, uint8_t* addr,
                                   uint32_t addr_size, uint8_t* data, uint32_t size);
hpm_stat_t i2c_master_address_write(I2C_Type* ptr, uint16_t device_address, uint8_t* addr,
                                    uint32_t addr_size, uint8_t* data, uint32_t size);
hpm_stat_t i2c_master_seq_transmit_check_ack(I2C_Type* ptr, uint16_t device_address,
                                             uint8_t* data, uint32_t size,
                                             i2c_seq_transfer_opt_t frame,
                                             bool check_ack);
hpm_stat_t i2c_master_seq_receive(I2C_Type* ptr, uint16_t device_address, uint8_t* data,
                                  uint32_t size, i2c_seq_transfer_opt_t frame);
hpm_stat_t i2c_master_transfer(I2C_Type* ptr, uint16_t device_address, uint8_t* data,
                               uint32_t size, uint16_t flags);

#ifdef __cplusplus
}
#endif
