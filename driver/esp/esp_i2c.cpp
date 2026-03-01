#include "esp_i2c.hpp"

#include <algorithm>
#include <array>

#include "esp_clk_tree.h"
#include "esp_private/periph_ctrl.h"
#include "esp_rom_gpio.h"
#include "esp_timer.h"
#include "libxr_def.hpp"

namespace LibXR
{
namespace
{

constexpr uint8_t kAckValue = I2C_MASTER_ACK;
constexpr uint8_t kNackValue = I2C_MASTER_NACK;
constexpr uint8_t kCheckAck = 1U;
constexpr uint8_t kNoCheckAck = 0U;

uint64_t ToTimeoutUs(uint32_t timeout_ms)
{
  return (timeout_ms == UINT32_MAX)
             ? UINT64_MAX
             : static_cast<uint64_t>(timeout_ms) * 1000ULL;
}

void WriteCommand(i2c_dev_t* dev, int cmd_idx, uint8_t op_code, uint8_t ack_val,
                  uint8_t ack_exp, uint8_t ack_en, uint8_t byte_num)
{
  i2c_ll_hw_cmd_t cmd = {};
  cmd.op_code = op_code;
  cmd.ack_val = ack_val;
  cmd.ack_exp = ack_exp;
  cmd.ack_en = ack_en;
  cmd.byte_num = byte_num;
  i2c_ll_master_write_cmd_reg(dev, cmd, cmd_idx);
}

ErrorCode WaitSegmentDone(i2c_hal_context_t& hal, int done_cmd_idx, uint64_t timeout_us)
{
  const int64_t start_us = esp_timer_get_time();
  while (true)
  {
    const uint32_t intr = hal.dev->int_raw.val;

    if ((intr & I2C_LL_INTR_NACK) != 0U)
    {
      i2c_ll_clear_intr_mask(hal.dev, I2C_LL_INTR_MASK);
      i2c_hal_master_fsm_rst(&hal);
      i2c_ll_update(hal.dev);
      return ErrorCode::NO_RESPONSE;
    }
    if ((intr & I2C_LL_INTR_TIMEOUT) != 0U)
    {
      i2c_ll_clear_intr_mask(hal.dev, I2C_LL_INTR_MASK);
      i2c_hal_master_fsm_rst(&hal);
      i2c_ll_update(hal.dev);
      return ErrorCode::TIMEOUT;
    }
    if ((intr & I2C_LL_INTR_ARBITRATION) != 0U)
    {
      i2c_ll_clear_intr_mask(hal.dev, I2C_LL_INTR_MASK);
      i2c_hal_master_fsm_rst(&hal);
      i2c_ll_update(hal.dev);
      return ErrorCode::FAILED;
    }

    if (i2c_ll_master_is_cmd_done(hal.dev, done_cmd_idx) ||
        ((intr & (I2C_LL_INTR_MST_COMPLETE | I2C_LL_INTR_END_DETECT)) != 0U))
    {
      return ErrorCode::OK;
    }

    if ((timeout_us != UINT64_MAX) &&
        (static_cast<uint64_t>(esp_timer_get_time() - start_us) > timeout_us))
    {
      i2c_ll_clear_intr_mask(hal.dev, I2C_LL_INTR_MASK);
      i2c_hal_master_fsm_rst(&hal);
      i2c_ll_update(hal.dev);
      return ErrorCode::TIMEOUT;
    }
  }
}

ErrorCode StartAndWaitSegment(i2c_hal_context_t& hal, int done_cmd_idx, uint64_t timeout_us)
{
  i2c_ll_clear_intr_mask(hal.dev, I2C_LL_INTR_MASK);
  i2c_hal_master_trans_start(&hal);
  return WaitSegmentDone(hal, done_cmd_idx, timeout_us);
}

template <typename OperationType>
ErrorCode Complete(OperationType& op, bool in_isr, ErrorCode result)
{
  op.UpdateStatus(in_isr, result);

  if (op.type != OperationType::OperationType::BLOCK)
  {
    return result;
  }

  if (in_isr)
  {
    // ISR context must not block on semaphore wait.
    return result;
  }

  return op.data.sem_info.sem->Wait(op.data.sem_info.timeout);
}

}  // namespace

ESP32I2C::ESP32I2C(i2c_port_t port_num, int scl_pin, int sda_pin,
                   uint32_t clock_speed, bool enable_internal_pullup,
                   uint32_t timeout_ms)
    : port_num_(port_num),
      scl_pin_(scl_pin),
      sda_pin_(sda_pin),
      enable_internal_pullup_(enable_internal_pullup),
      timeout_ms_(timeout_ms),
      config_{clock_speed}
{
  ASSERT(port_num_ >= 0);
  ASSERT(port_num_ < SOC_I2C_NUM);
  ASSERT(GPIO_IS_VALID_OUTPUT_GPIO(static_cast<gpio_num_t>(scl_pin_)));
  ASSERT(GPIO_IS_VALID_OUTPUT_GPIO(static_cast<gpio_num_t>(sda_pin_)));
  ASSERT(config_.clock_speed > 0U);
  ASSERT(kFifoLen > 2U);

  if (InitHardware() != ErrorCode::OK)
  {
    ASSERT(false);
  }
}

ESP32I2C::~ESP32I2C() { DeinitHardware(); }

bool ESP32I2C::Acquire()
{
  bool expected = false;
  return busy_.compare_exchange_strong(expected, true, std::memory_order_acq_rel,
                                       std::memory_order_acquire);
}

void ESP32I2C::Release() { busy_.store(false, std::memory_order_release); }

bool ESP32I2C::IsValid7BitAddr(uint16_t addr) { return addr <= 0x7FU; }

ErrorCode ESP32I2C::ResolveClockSource(uint32_t& source_hz)
{
  source_hz = 0U;
  const esp_err_t err = esp_clk_tree_src_get_freq_hz(
      static_cast<soc_module_clk_t>(I2C_CLK_SRC_DEFAULT),
      ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED, &source_hz);
  if ((err != ESP_OK) || (source_hz == 0U))
  {
    return ErrorCode::INIT_ERR;
  }
  return ErrorCode::OK;
}

ErrorCode ESP32I2C::ApplyConfig()
{
  if ((hal_.dev == nullptr) || (config_.clock_speed == 0U))
  {
    return ErrorCode::ARG_ERR;
  }

  i2c_ll_set_source_clk(hal_.dev, I2C_CLK_SRC_DEFAULT);
  if (ResolveClockSource(source_clock_hz_) != ErrorCode::OK)
  {
    return ErrorCode::INIT_ERR;
  }

  if (config_.clock_speed > (source_clock_hz_ / 20U))
  {
    return ErrorCode::ARG_ERR;
  }

  _i2c_hal_set_bus_timing(&hal_, static_cast<int>(config_.clock_speed),
                          I2C_CLK_SRC_DEFAULT,
                          static_cast<int>(source_clock_hz_));
  i2c_ll_master_set_filter(hal_.dev, 7U);
  i2c_ll_update(hal_.dev);

  return ErrorCode::OK;
}

ErrorCode ESP32I2C::InitHardware()
{
  if (initialized_)
  {
    return ErrorCode::OK;
  }

  if ((port_num_ < 0) || (port_num_ >= SOC_I2C_NUM) ||
      (static_cast<size_t>(port_num_) >= SOC_I2C_NUM))
  {
    return ErrorCode::OUT_OF_RANGE;
  }

  if (!GPIO_IS_VALID_OUTPUT_GPIO(static_cast<gpio_num_t>(scl_pin_)) ||
      !GPIO_IS_VALID_OUTPUT_GPIO(static_cast<gpio_num_t>(sda_pin_)))
  {
    return ErrorCode::ARG_ERR;
  }

  PERIPH_RCC_ATOMIC()
  {
    i2c_ll_enable_bus_clock(port_num_, true);
    i2c_ll_reset_register(port_num_);
  }

  _i2c_hal_init(&hal_, static_cast<int>(port_num_));
  if (hal_.dev == nullptr)
  {
    return ErrorCode::INIT_ERR;
  }

  i2c_hal_master_init(&hal_);
  i2c_ll_disable_intr_mask(hal_.dev, I2C_LL_MASTER_EVENT_INTR);
  i2c_ll_clear_intr_mask(hal_.dev, I2C_LL_MASTER_EVENT_INTR);

  ErrorCode err = ConfigurePins();
  if (err != ErrorCode::OK)
  {
    DeinitHardware();
    return err;
  }

  err = ApplyConfig();
  if (err != ErrorCode::OK)
  {
    DeinitHardware();
    return err;
  }

  initialized_ = true;
  return ErrorCode::OK;
}

void ESP32I2C::DeinitHardware()
{
  if (!initialized_)
  {
    return;
  }

  if (hal_.dev != nullptr)
  {
    i2c_ll_disable_intr_mask(hal_.dev, I2C_LL_MASTER_EVENT_INTR);
    i2c_ll_clear_intr_mask(hal_.dev, I2C_LL_MASTER_EVENT_INTR);
  }
  _i2c_hal_deinit(&hal_);
  PERIPH_RCC_ATOMIC() { i2c_ll_enable_bus_clock(port_num_, false); }

  // Release pin matrix ownership so later GPIO/UART tests can remux these pins.
  if (gpio_num_t sda_gpio = static_cast<gpio_num_t>(sda_pin_);
      GPIO_IS_VALID_GPIO(sda_gpio))
  {
    gpio_reset_pin(sda_gpio);
  }
  if (gpio_num_t scl_gpio = static_cast<gpio_num_t>(scl_pin_);
      GPIO_IS_VALID_GPIO(scl_gpio))
  {
    gpio_reset_pin(scl_gpio);
  }

  hal_.dev = nullptr;
  source_clock_hz_ = 0U;
  initialized_ = false;
}

ErrorCode ESP32I2C::ConfigurePins()
{
  if (hal_.dev == nullptr)
  {
    return ErrorCode::STATE_ERR;
  }

  const auto& sig = i2c_periph_signal[port_num_];
  const gpio_num_t sda_gpio = static_cast<gpio_num_t>(sda_pin_);
  const gpio_num_t scl_gpio = static_cast<gpio_num_t>(scl_pin_);

  gpio_set_level(sda_gpio, 1);
  esp_rom_gpio_pad_select_gpio(static_cast<uint32_t>(sda_pin_));
  gpio_set_direction(sda_gpio, GPIO_MODE_INPUT_OUTPUT_OD);
  gpio_set_pull_mode(sda_gpio, enable_internal_pullup_ ? GPIO_PULLUP_ONLY : GPIO_FLOATING);
  esp_rom_gpio_connect_out_signal(sda_pin_, sig.sda_out_sig, false, false);
  esp_rom_gpio_connect_in_signal(sda_pin_, sig.sda_in_sig, false);

  gpio_set_level(scl_gpio, 1);
  esp_rom_gpio_pad_select_gpio(static_cast<uint32_t>(scl_pin_));
  gpio_set_direction(scl_gpio, GPIO_MODE_INPUT_OUTPUT_OD);
  gpio_set_pull_mode(scl_gpio, enable_internal_pullup_ ? GPIO_PULLUP_ONLY : GPIO_FLOATING);
  esp_rom_gpio_connect_out_signal(scl_pin_, sig.scl_out_sig, false, false);
  esp_rom_gpio_connect_in_signal(scl_pin_, sig.scl_in_sig, false);

  return ErrorCode::OK;
}

ErrorCode ESP32I2C::RecoverController()
{
  if (hal_.dev == nullptr)
  {
    return ErrorCode::INIT_ERR;
  }

#if SOC_I2C_SUPPORT_HW_FSM_RST
  i2c_hal_master_fsm_rst(&hal_);
  i2c_ll_update(hal_.dev);
  return ErrorCode::OK;
#else
  // ESP32-class targets without HW FSM reset require full register reset.
  PERIPH_RCC_ATOMIC() { i2c_ll_reset_register(port_num_); }
  i2c_hal_master_init(&hal_);
  i2c_ll_disable_intr_mask(hal_.dev, I2C_LL_MASTER_EVENT_INTR);
  i2c_ll_clear_intr_mask(hal_.dev, I2C_LL_INTR_MASK);

  const ErrorCode pin_ec = ConfigurePins();
  if (pin_ec != ErrorCode::OK)
  {
    return pin_ec;
  }
  return ApplyConfig();
#endif
}

ErrorCode ESP32I2C::ExecuteTransaction(uint16_t slave_addr, const uint8_t* write_payload,
                                       size_t write_size, uint8_t* read_payload,
                                       size_t read_size)
{
  if (!initialized_ || (hal_.dev == nullptr))
  {
    return ErrorCode::INIT_ERR;
  }
  if (!IsValid7BitAddr(slave_addr))
  {
    return ErrorCode::ARG_ERR;
  }
  if ((write_size > 0U) && (write_payload == nullptr))
  {
    return ErrorCode::PTR_NULL;
  }
  if ((read_size > 0U) && (read_payload == nullptr))
  {
    return ErrorCode::PTR_NULL;
  }

  if (i2c_ll_is_bus_busy(hal_.dev))
  {
    const ErrorCode ec = RecoverController();
    if (ec != ErrorCode::OK)
    {
      return ec;
    }
  }

  i2c_ll_txfifo_rst(hal_.dev);
  i2c_ll_rxfifo_rst(hal_.dev);
  i2c_ll_clear_intr_mask(hal_.dev, I2C_LL_INTR_MASK);

  const uint64_t timeout_us = ToTimeoutUs(timeout_ms_);
  const uint8_t write_addr = static_cast<uint8_t>((slave_addr << 1U) | I2C_MASTER_WRITE);
  const uint8_t read_addr = static_cast<uint8_t>((slave_addr << 1U) | I2C_MASTER_READ);
  const size_t fifo_len = kFifoLen;
  const size_t write_chunk_cap = (fifo_len > 1U) ? (fifo_len - 1U) : 0U;
  ASSERT(write_chunk_cap > 0U);

  int cmd_idx = 0;

  if ((write_size > 0U) || (read_size == 0U))
  {
    WriteCommand(hal_.dev, cmd_idx++, I2C_LL_CMD_RESTART, kAckValue, kAckValue,
                 kNoCheckAck, 0U);

    i2c_ll_write_txfifo(hal_.dev, &write_addr, 1U);
    WriteCommand(hal_.dev, cmd_idx++, I2C_LL_CMD_WRITE, kAckValue, kAckValue,
                 kCheckAck, 1U);

    size_t write_offset = 0U;
    while (write_offset < write_size)
    {
      const size_t chunk = std::min(write_size - write_offset, write_chunk_cap);
      i2c_ll_write_txfifo(hal_.dev,
                          static_cast<const uint8_t*>(write_payload) + write_offset,
                          static_cast<uint8_t>(chunk));
      write_offset += chunk;

      WriteCommand(hal_.dev, cmd_idx++, I2C_LL_CMD_WRITE, kAckValue, kAckValue,
                   kCheckAck, static_cast<uint8_t>(chunk));
      WriteCommand(hal_.dev, cmd_idx++, I2C_LL_CMD_END, kAckValue, kAckValue,
                   kNoCheckAck, 0U);

      const ErrorCode ec = StartAndWaitSegment(hal_, cmd_idx - 1, timeout_us);
      if (ec != ErrorCode::OK)
      {
        (void)RecoverController();
        return ec;
      }
      cmd_idx = 0;
    }

    if (write_size == 0U)
    {
      if (read_size == 0U)
      {
        WriteCommand(hal_.dev, cmd_idx++, I2C_LL_CMD_STOP, kAckValue, kAckValue,
                     kNoCheckAck, 0U);
#if SOC_I2C_STOP_INDEPENDENT
        WriteCommand(hal_.dev, cmd_idx++, I2C_LL_CMD_END, kAckValue, kAckValue,
                     kNoCheckAck, 0U);
#endif
      }
      const ErrorCode ec = StartAndWaitSegment(hal_, cmd_idx - 1, timeout_us);
      if (ec != ErrorCode::OK)
      {
        (void)RecoverController();
        return ec;
      }
      cmd_idx = 0;
    }
    else if (read_size == 0U)
    {
      WriteCommand(hal_.dev, cmd_idx++, I2C_LL_CMD_STOP, kAckValue, kAckValue,
                   kNoCheckAck, 0U);
#if SOC_I2C_STOP_INDEPENDENT
      WriteCommand(hal_.dev, cmd_idx++, I2C_LL_CMD_END, kAckValue, kAckValue,
                   kNoCheckAck, 0U);
#endif
      const ErrorCode ec = StartAndWaitSegment(hal_, cmd_idx - 1, timeout_us);
      if (ec != ErrorCode::OK)
      {
        (void)RecoverController();
        return ec;
      }
      cmd_idx = 0;
    }
  }

  if (read_size > 0U)
  {
    WriteCommand(hal_.dev, cmd_idx++, I2C_LL_CMD_RESTART, kAckValue, kAckValue,
                 kNoCheckAck, 0U);
    i2c_ll_write_txfifo(hal_.dev, &read_addr, 1U);
    WriteCommand(hal_.dev, cmd_idx++, I2C_LL_CMD_WRITE, kAckValue, kAckValue,
                 kCheckAck, 1U);

    size_t read_offset = 0U;
    while (read_offset < read_size)
    {
      const size_t chunk = std::min(read_size - read_offset, fifo_len);
      const bool is_last = (read_offset + chunk) >= read_size;

      if (!is_last)
      {
        WriteCommand(hal_.dev, cmd_idx++, I2C_LL_CMD_READ, kAckValue, kAckValue,
                     kNoCheckAck, static_cast<uint8_t>(chunk));
        WriteCommand(hal_.dev, cmd_idx++, I2C_LL_CMD_END, kAckValue, kAckValue,
                     kNoCheckAck, 0U);
      }
      else if (chunk == 1U)
      {
        WriteCommand(hal_.dev, cmd_idx++, I2C_LL_CMD_READ, kNackValue, kAckValue,
                     kNoCheckAck, 1U);
      }
      else
      {
        WriteCommand(hal_.dev, cmd_idx++, I2C_LL_CMD_READ, kAckValue, kAckValue,
                     kNoCheckAck, static_cast<uint8_t>(chunk - 1U));
        WriteCommand(hal_.dev, cmd_idx++, I2C_LL_CMD_READ, kNackValue, kAckValue,
                     kNoCheckAck, 1U);
      }

      if (is_last)
      {
        WriteCommand(hal_.dev, cmd_idx++, I2C_LL_CMD_STOP, kAckValue, kAckValue,
                     kNoCheckAck, 0U);
#if SOC_I2C_STOP_INDEPENDENT
        WriteCommand(hal_.dev, cmd_idx++, I2C_LL_CMD_END, kAckValue, kAckValue,
                     kNoCheckAck, 0U);
#endif
      }

      const ErrorCode ec = StartAndWaitSegment(hal_, cmd_idx - 1, timeout_us);
      if (ec != ErrorCode::OK)
      {
        (void)RecoverController();
        return ec;
      }

      i2c_ll_read_rxfifo(hal_.dev, read_payload + read_offset,
                         static_cast<uint8_t>(chunk));
      read_offset += chunk;
      cmd_idx = 0;
    }
  }

  i2c_ll_clear_intr_mask(hal_.dev, I2C_LL_INTR_MASK);
  return ErrorCode::OK;
}

ErrorCode ESP32I2C::SetConfig(Configuration config)
{
  if (config.clock_speed == 0U)
  {
    return ErrorCode::ARG_ERR;
  }

  if (!initialized_)
  {
    const ErrorCode init_err = InitHardware();
    if (init_err != ErrorCode::OK)
    {
      return init_err;
    }
  }

  if (!Acquire())
  {
    return ErrorCode::BUSY;
  }

  config_ = config;
  const ErrorCode ans = ApplyConfig();
  Release();
  return ans;
}

ErrorCode ESP32I2C::Write(uint16_t slave_addr, ConstRawData write_data,
                          WriteOperation& op, bool in_isr)
{
  if (!initialized_)
  {
    if (in_isr)
    {
      return Complete(op, in_isr, ErrorCode::INIT_ERR);
    }

    const ErrorCode init_err = InitHardware();
    if (init_err != ErrorCode::OK)
    {
      return Complete(op, in_isr, init_err);
    }
  }

  if (!IsValid7BitAddr(slave_addr))
  {
    return Complete(op, in_isr, ErrorCode::ARG_ERR);
  }

  if ((write_data.size_ > 0U) && (write_data.addr_ == nullptr))
  {
    return Complete(op, in_isr, ErrorCode::PTR_NULL);
  }

  if (!Acquire())
  {
    return Complete(op, in_isr, ErrorCode::BUSY);
  }

  ErrorCode ans = ErrorCode::OK;
  ans = ExecuteTransaction(slave_addr,
                           static_cast<const uint8_t*>(write_data.addr_),
                           write_data.size_, nullptr, 0U);

  Release();
  return Complete(op, in_isr, ans);
}

ErrorCode ESP32I2C::Read(uint16_t slave_addr, RawData read_data, ReadOperation& op,
                         bool in_isr)
{
  if (!initialized_)
  {
    if (in_isr)
    {
      return Complete(op, in_isr, ErrorCode::INIT_ERR);
    }

    const ErrorCode init_err = InitHardware();
    if (init_err != ErrorCode::OK)
    {
      return Complete(op, in_isr, init_err);
    }
  }

  if (!IsValid7BitAddr(slave_addr))
  {
    return Complete(op, in_isr, ErrorCode::ARG_ERR);
  }

  if ((read_data.size_ > 0U) && (read_data.addr_ == nullptr))
  {
    return Complete(op, in_isr, ErrorCode::PTR_NULL);
  }

  if (!Acquire())
  {
    return Complete(op, in_isr, ErrorCode::BUSY);
  }

  ErrorCode ans = ErrorCode::OK;
  ans = ExecuteTransaction(slave_addr, nullptr, 0U,
                           static_cast<uint8_t*>(read_data.addr_),
                           read_data.size_);

  Release();
  return Complete(op, in_isr, ans);
}

ErrorCode ESP32I2C::MemWrite(uint16_t slave_addr, uint16_t mem_addr,
                             ConstRawData write_data, WriteOperation& op,
                             MemAddrLength mem_addr_size, bool in_isr)
{
  if (!initialized_)
  {
    if (in_isr)
    {
      return Complete(op, in_isr, ErrorCode::INIT_ERR);
    }

    const ErrorCode init_err = InitHardware();
    if (init_err != ErrorCode::OK)
    {
      return Complete(op, in_isr, init_err);
    }
  }

  if (!IsValid7BitAddr(slave_addr))
  {
    return Complete(op, in_isr, ErrorCode::ARG_ERR);
  }

  if ((write_data.size_ > 0U) && (write_data.addr_ == nullptr))
  {
    return Complete(op, in_isr, ErrorCode::PTR_NULL);
  }

  const size_t mem_len = (mem_addr_size == MemAddrLength::BYTE_16) ? 2U : 1U;
  if (mem_len > kMaxWritePayload)
  {
    return Complete(op, in_isr, ErrorCode::SIZE_ERR);
  }

  if (!Acquire())
  {
    return Complete(op, in_isr, ErrorCode::BUSY);
  }

  std::array<uint8_t, kFifoLen> staging = {};
  const size_t max_chunk = kMaxWritePayload - mem_len;
  auto* src = static_cast<const uint8_t*>(write_data.addr_);
  size_t offset = 0U;
  ErrorCode ans = ErrorCode::OK;

  if (write_data.size_ == 0U)
  {
    if (mem_len == 2U)
    {
      staging[0] = static_cast<uint8_t>((mem_addr >> 8) & 0xFFU);
      staging[1] = static_cast<uint8_t>(mem_addr & 0xFFU);
    }
    else
    {
      staging[0] = static_cast<uint8_t>(mem_addr & 0xFFU);
    }
    ans = ExecuteTransaction(slave_addr, staging.data(), mem_len, nullptr, 0U);
  }
  else
  {
    while (offset < write_data.size_)
    {
      const size_t chunk = std::min(write_data.size_ - offset, max_chunk);
      const uint16_t cur_mem = static_cast<uint16_t>(mem_addr + offset);
      if (mem_len == 2U)
      {
        staging[0] = static_cast<uint8_t>((cur_mem >> 8) & 0xFFU);
        staging[1] = static_cast<uint8_t>(cur_mem & 0xFFU);
      }
      else
      {
        staging[0] = static_cast<uint8_t>(cur_mem & 0xFFU);
      }
      Memory::FastCopy(staging.data() + mem_len, src + offset, chunk);
      ans = ExecuteTransaction(slave_addr, staging.data(), mem_len + chunk, nullptr, 0U);
      if (ans != ErrorCode::OK)
      {
        break;
      }
      offset += chunk;
    }
  }

  Release();
  return Complete(op, in_isr, ans);
}

ErrorCode ESP32I2C::MemRead(uint16_t slave_addr, uint16_t mem_addr, RawData read_data,
                            ReadOperation& op, MemAddrLength mem_addr_size,
                            bool in_isr)
{
  if (!initialized_)
  {
    if (in_isr)
    {
      return Complete(op, in_isr, ErrorCode::INIT_ERR);
    }

    const ErrorCode init_err = InitHardware();
    if (init_err != ErrorCode::OK)
    {
      return Complete(op, in_isr, init_err);
    }
  }

  if (!IsValid7BitAddr(slave_addr))
  {
    return Complete(op, in_isr, ErrorCode::ARG_ERR);
  }

  if ((read_data.size_ > 0U) && (read_data.addr_ == nullptr))
  {
    return Complete(op, in_isr, ErrorCode::PTR_NULL);
  }

  const size_t mem_len = (mem_addr_size == MemAddrLength::BYTE_16) ? 2U : 1U;
  if (mem_len > kMaxWriteReadPrefix)
  {
    return Complete(op, in_isr, ErrorCode::SIZE_ERR);
  }

  if (!Acquire())
  {
    return Complete(op, in_isr, ErrorCode::BUSY);
  }

  std::array<uint8_t, 2> mem_raw = {};
  auto* dst = static_cast<uint8_t*>(read_data.addr_);
  size_t offset = 0U;
  ErrorCode ans = ErrorCode::OK;

  while (offset < read_data.size_)
  {
    const size_t chunk = std::min(read_data.size_ - offset, kMaxReadPayload);
    const uint16_t cur_mem = static_cast<uint16_t>(mem_addr + offset);
    if (mem_len == 2U)
    {
      mem_raw[0] = static_cast<uint8_t>((cur_mem >> 8) & 0xFFU);
      mem_raw[1] = static_cast<uint8_t>(cur_mem & 0xFFU);
    }
    else
    {
      mem_raw[0] = static_cast<uint8_t>(cur_mem & 0xFFU);
    }

    ans = ExecuteTransaction(slave_addr, mem_raw.data(), mem_len, dst + offset, chunk);
    if (ans != ErrorCode::OK)
    {
      break;
    }
    offset += chunk;
  }

  Release();
  return Complete(op, in_isr, ans);
}

}  // namespace LibXR
