#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "driver/gpio.h"
#include "hal/i2c_hal.h"
#include "hal/i2c_types.h"
#include "i2c.hpp"
#include "soc/i2c_periph.h"
#include "soc/soc_caps.h"

namespace LibXR
{

class ESP32I2C : public I2C
{
 public:
  static constexpr int PIN_NO_CHANGE = -1;

  ESP32I2C(i2c_port_t port_num, int scl_pin, int sda_pin,
           uint32_t clock_speed = 400000U,
           bool enable_internal_pullup = true,
           uint32_t timeout_ms = 100U,
           uint32_t dma_enable_min_size = 3U);

  ~ESP32I2C();

  ErrorCode Read(uint16_t slave_addr, RawData read_data, ReadOperation& op,
                 bool in_isr = false) override;

  ErrorCode Write(uint16_t slave_addr, ConstRawData write_data, WriteOperation& op,
                  bool in_isr = false) override;

  ErrorCode SetConfig(Configuration config) override;

  ErrorCode MemRead(uint16_t slave_addr, uint16_t mem_addr, RawData read_data,
                    ReadOperation& op,
                    MemAddrLength mem_addr_size = MemAddrLength::BYTE_8,
                    bool in_isr = false) override;

  ErrorCode MemWrite(uint16_t slave_addr, uint16_t mem_addr,
                     ConstRawData write_data, WriteOperation& op,
                     MemAddrLength mem_addr_size = MemAddrLength::BYTE_8,
                     bool in_isr = false) override;

  i2c_port_t Port() const { return port_num_; }

 private:
  static constexpr size_t kFifoLen = SOC_I2C_FIFO_LEN;
  static constexpr size_t kMaxWritePayload = (kFifoLen > 4U) ? (kFifoLen - 4U) : 0U;
  static constexpr size_t kMaxWriteReadPrefix =
      (kFifoLen > 5U) ? (kFifoLen - 5U) : 0U;
  static constexpr size_t kMaxReadPayload = (kFifoLen > 4U) ? (kFifoLen - 4U) : kFifoLen;

  bool Acquire();
  void Release();

  ErrorCode InitHardware();
  void DeinitHardware();
  ErrorCode ConfigurePins();
  ErrorCode ApplyConfig();
  ErrorCode ResolveClockSource(uint32_t& source_hz);
  ErrorCode RecoverController();
  ErrorCode ExecuteTransaction(uint16_t slave_addr, const uint8_t* write_payload,
                               size_t write_size, uint8_t* read_payload,
                               size_t read_size);
  static bool IsValid7BitAddr(uint16_t addr);

  i2c_port_t port_num_;
  int scl_pin_;
  int sda_pin_;
  bool enable_internal_pullup_;
  uint32_t timeout_ms_;
  uint32_t dma_enable_min_size_ = 3U;
  bool initialized_ = false;
  Configuration config_{};
  i2c_hal_context_t hal_ = {};
  uint32_t source_clock_hz_ = 0U;
  std::atomic<bool> busy_{false};
};

}  // namespace LibXR
