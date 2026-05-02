#pragma once

#include <termios.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "linux_gpio.hpp"
#include "rs485.hpp"

namespace LibXR
{

class LinuxRS485 : public RS485
{
 public:
  struct PlatformConfig
  {
    std::string uart_path = "/dev/ttyTHS1";
    std::string gpio_chip = "/dev/gpiochip0";
    bool has_rx_enable = true;
    unsigned int rx_enable_line = 50;
    bool rx_active_level = true;
    bool has_tx_enable = true;
    unsigned int tx_enable_line = 14;
    bool open_nonblock = false;
    bool drain_after_write = true;
    bool release_legacy_sysfs = true;
    uint32_t wait_sleep_us = 50;
    uint32_t default_write_timeout_ms = 1000;
  };

  LinuxRS485();
  explicit LinuxRS485(PlatformConfig platform_config,
                      const Configuration& config = Configuration());
  ~LinuxRS485() override;

  LinuxRS485(const LinuxRS485&) = delete;
  LinuxRS485& operator=(const LinuxRS485&) = delete;

  ErrorCode Init();
  void Close();

  ErrorCode SetConfig(const Configuration& config) override;
  ErrorCode SetPlatformConfig(const PlatformConfig& config);
  ErrorCode Write(ConstRawData frame, WriteOperation& op, bool in_isr = false) override;
  ErrorCode Write(ConstRawData frame, uint32_t timeout_ms);
  void Reset() override;

  ErrorCode ReadExact(RawData data, uint32_t timeout_ms);
  ErrorCode FlushInput();
  ErrorCode SetTransmitDirection();
  ErrorCode SetReceiveDirection();
  ErrorCode SetTransmitMode() { return SetTransmitDirection(); }
  ErrorCode SetReceiveMode() { return SetReceiveDirection(); }

  [[nodiscard]] int RawFd() const { return fd_; }
  [[nodiscard]] const Configuration& config() const { return config_; }
  [[nodiscard]] const PlatformConfig& platform_config() const { return platform_config_; }

 private:
  ErrorCode OpenSerial();
  ErrorCode ConfigureSerial();
  ErrorCode ConfigureGpio(std::unique_ptr<LinuxGPIO>& gpio, unsigned int line);
  ErrorCode WriteBlocking(ConstRawData frame, uint32_t timeout_ms);
  ErrorCode WriteAll(const uint8_t* data, size_t size, uint32_t timeout_ms);
  void SleepWait() const;

  static void DelayMicroseconds(uint32_t delay_us);
  static bool ReleaseLegacySysfsLine(unsigned int line);
  static speed_t ToBaudrate(uint32_t baudrate);

  PlatformConfig platform_config_;
  Configuration config_;
  int fd_ = -1;
  std::unique_ptr<LinuxGPIO> rx_enable_;
  std::unique_ptr<LinuxGPIO> tx_enable_;
};

}  // namespace LibXR
