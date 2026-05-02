#include "linux_rs485.hpp"

#include <fcntl.h>
#include <linux/serial.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <limits>
#include <thread>
#include <utility>

namespace
{

using SteadyClock = std::chrono::steady_clock;

constexpr int LEGACY_SYSFS_RETRY_WAIT_MS = 100;

bool PathExists(const std::string& path)
{
  struct stat st = {};
  return ::stat(path.c_str(), &st) == 0;
}

bool WaitForPathState(const std::string& path, bool exists, int timeout_ms)
{
  const auto deadline = SteadyClock::now() + std::chrono::milliseconds(timeout_ms);
  while (SteadyClock::now() < deadline)
  {
    if (PathExists(path) == exists)
    {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return PathExists(path) == exists;
}

bool TimedOut(const SteadyClock::time_point& deadline, uint32_t timeout_ms)
{
  return timeout_ms != UINT32_MAX && SteadyClock::now() >= deadline;
}

SteadyClock::time_point MakeDeadline(uint32_t timeout_ms)
{
  if (timeout_ms == UINT32_MAX)
  {
    return SteadyClock::time_point::max();
  }
  return SteadyClock::now() + std::chrono::milliseconds(timeout_ms);
}

}  // namespace

namespace LibXR
{

LinuxRS485::LinuxRS485() : LinuxRS485(PlatformConfig(), Configuration()) {}

LinuxRS485::LinuxRS485(PlatformConfig platform_config, const Configuration& config)
    : platform_config_(std::move(platform_config)), config_(config)
{
}

LinuxRS485::~LinuxRS485() { Close(); }

ErrorCode LinuxRS485::Init()
{
  if (platform_config_.has_rx_enable)
  {
    ErrorCode ec = ConfigureGpio(rx_enable_, platform_config_.rx_enable_line);
    if (ec != ErrorCode::OK)
    {
      return ec;
    }
  }

  if (platform_config_.has_tx_enable)
  {
    ErrorCode ec = ConfigureGpio(tx_enable_, platform_config_.tx_enable_line);
    if (ec != ErrorCode::OK)
    {
      return ec;
    }
  }

  ErrorCode ec = OpenSerial();
  if (ec != ErrorCode::OK)
  {
    return ec;
  }

  ec = ConfigureSerial();
  if (ec != ErrorCode::OK)
  {
    return ec;
  }

  return SetReceiveDirection();
}

void LinuxRS485::Close()
{
  if (fd_ >= 0)
  {
    (void)SetReceiveDirection();
    ::close(fd_);
    fd_ = -1;
  }
}

ErrorCode LinuxRS485::SetConfig(const Configuration& config)
{
  if (config.data_bits < 5 || config.data_bits > 8)
  {
    return ErrorCode::NOT_SUPPORT;
  }

  const Configuration old_config = config_;
  config_ = config;
  if (fd_ < 0)
  {
    return ErrorCode::OK;
  }

  ErrorCode ec = ConfigureSerial();
  if (ec != ErrorCode::OK)
  {
    config_ = old_config;
    (void)ConfigureSerial();
  }
  return ec;
}

ErrorCode LinuxRS485::SetPlatformConfig(const PlatformConfig& config)
{
  Close();
  platform_config_ = config;
  rx_enable_.reset();
  tx_enable_.reset();
  return ErrorCode::OK;
}

ErrorCode LinuxRS485::ConfigureGpio(std::unique_ptr<LinuxGPIO>& gpio, unsigned int line)
{
  gpio = std::make_unique<LinuxGPIO>(platform_config_.gpio_chip, line);
  const GPIO::Configuration output_cfg = {GPIO::Direction::OUTPUT_PUSH_PULL,
                                          GPIO::Pull::NONE};
  ErrorCode ec = gpio->SetConfig(output_cfg);
  if (ec == ErrorCode::OK || !platform_config_.release_legacy_sysfs)
  {
    return ec;
  }

  if (!ReleaseLegacySysfsLine(line))
  {
    return ec;
  }

  return gpio->SetConfig(output_cfg);
}

ErrorCode LinuxRS485::OpenSerial()
{
  if (fd_ >= 0)
  {
    return ErrorCode::OK;
  }

  int flags = O_RDWR | O_NOCTTY | O_CLOEXEC;
  if (platform_config_.open_nonblock)
  {
    flags |= O_NONBLOCK;
  }

  fd_ = ::open(platform_config_.uart_path.c_str(), flags);
  return fd_ >= 0 ? ErrorCode::OK : ErrorCode::INIT_ERR;
}

ErrorCode LinuxRS485::ConfigureSerial()
{
  if (fd_ < 0)
  {
    return ErrorCode::STATE_ERR;
  }

  struct termios tio = {};
  if (::tcgetattr(fd_, &tio) != 0)
  {
    return ErrorCode::INIT_ERR;
  }

  const speed_t speed = ToBaudrate(config_.baudrate);
  if (speed == 0)
  {
    return ErrorCode::ARG_ERR;
  }

  ::cfmakeraw(&tio);
  tio.c_cflag |= CLOCAL | CREAD;
#ifdef CRTSCTS
  tio.c_cflag &= ~CRTSCTS;
#endif
  tio.c_cflag &= ~CSIZE;

  switch (config_.data_bits)
  {
    case 5:
      tio.c_cflag |= CS5;
      break;
    case 6:
      tio.c_cflag |= CS6;
      break;
    case 7:
      tio.c_cflag |= CS7;
      break;
    case 8:
      tio.c_cflag |= CS8;
      break;
    default:
      return ErrorCode::ARG_ERR;
  }

  if (config_.stop_bits == 1)
  {
    tio.c_cflag &= ~CSTOPB;
  }
  else if (config_.stop_bits == 2)
  {
    tio.c_cflag |= CSTOPB;
  }
  else
  {
    return ErrorCode::ARG_ERR;
  }

  switch (config_.parity)
  {
    case UART::Parity::NO_PARITY:
      tio.c_cflag &= ~PARENB;
      break;
    case UART::Parity::EVEN:
      tio.c_cflag |= PARENB;
      tio.c_cflag &= ~PARODD;
      break;
    case UART::Parity::ODD:
      tio.c_cflag |= PARENB;
      tio.c_cflag |= PARODD;
      break;
  }

  tio.c_cc[VMIN] = 0;
  tio.c_cc[VTIME] = 0;

  if (::cfsetispeed(&tio, speed) != 0 || ::cfsetospeed(&tio, speed) != 0)
  {
    return ErrorCode::INIT_ERR;
  }
  if (::tcsetattr(fd_, TCSANOW, &tio) != 0)
  {
    return ErrorCode::INIT_ERR;
  }

  struct serial_struct serinfo = {};
  if (::ioctl(fd_, TIOCGSERIAL, &serinfo) == 0)
  {
    serinfo.flags |= ASYNC_LOW_LATENCY;
    (void)::ioctl(fd_, TIOCSSERIAL, &serinfo);
  }

  return FlushInput();
}

ErrorCode LinuxRS485::SetTransmitDirection()
{
  if (tx_enable_)
  {
    tx_enable_->Write(config_.tx_active_level);
  }
  if (rx_enable_)
  {
    rx_enable_->Write(!platform_config_.rx_active_level);
  }
  if (config_.assert_time_us != 0)
  {
    DelayMicroseconds(config_.assert_time_us);
  }
  return ErrorCode::OK;
}

ErrorCode LinuxRS485::SetReceiveDirection()
{
  if (rx_enable_)
  {
    rx_enable_->Write(platform_config_.rx_active_level);
  }
  if (tx_enable_)
  {
    tx_enable_->Write(!config_.tx_active_level);
  }
  return ErrorCode::OK;
}

ErrorCode LinuxRS485::FlushInput()
{
  if (fd_ < 0)
  {
    return ErrorCode::STATE_ERR;
  }
  return ::tcflush(fd_, TCIFLUSH) == 0 ? ErrorCode::OK : ErrorCode::FAILED;
}

ErrorCode LinuxRS485::Write(ConstRawData frame, WriteOperation& op, bool in_isr)
{
  if (in_isr)
  {
    return ErrorCode::NOT_SUPPORT;
  }

  if (frame.size_ == 0)
  {
    if (op.type != WriteOperation::OperationType::BLOCK)
    {
      op.UpdateStatus(false, ErrorCode::OK);
    }
    return ErrorCode::OK;
  }

  if (op.type == WriteOperation::OperationType::POLLING)
  {
    op.MarkAsRunning();
  }

  const uint32_t timeout_ms = (op.type == WriteOperation::OperationType::BLOCK)
                                  ? op.data.sem_info.timeout
                                  : platform_config_.default_write_timeout_ms;
  const ErrorCode ec = WriteBlocking(frame, timeout_ms);

  if (op.type != WriteOperation::OperationType::BLOCK)
  {
    op.UpdateStatus(false, ec);
  }
  return ec;
}

ErrorCode LinuxRS485::Write(ConstRawData frame, uint32_t timeout_ms)
{
  return WriteBlocking(frame, timeout_ms);
}

void LinuxRS485::Reset()
{
  if (fd_ >= 0)
  {
    (void)::tcflush(fd_, TCIOFLUSH);
  }
  (void)SetReceiveDirection();
}

ErrorCode LinuxRS485::WriteBlocking(ConstRawData frame, uint32_t timeout_ms)
{
  if (frame.size_ == 0)
  {
    return ErrorCode::OK;
  }
  if (frame.addr_ == nullptr ||
      frame.size_ > static_cast<size_t>(std::numeric_limits<ssize_t>::max()))
  {
    return ErrorCode::ARG_ERR;
  }

  ErrorCode ec = FlushInput();
  if (ec != ErrorCode::OK)
  {
    return ec;
  }

  ec = SetTransmitDirection();
  if (ec != ErrorCode::OK)
  {
    return ec;
  }

  ec = WriteAll(static_cast<const uint8_t*>(frame.addr_), frame.size_, timeout_ms);
  if (ec != ErrorCode::OK)
  {
    (void)SetReceiveDirection();
    return ec;
  }

  if (platform_config_.drain_after_write && ::tcdrain(fd_) != 0)
  {
    (void)SetReceiveDirection();
    return ErrorCode::FAILED;
  }

  if (config_.deassert_time_us != 0)
  {
    DelayMicroseconds(config_.deassert_time_us);
  }

  return SetReceiveDirection();
}

ErrorCode LinuxRS485::WriteAll(const uint8_t* data, size_t size, uint32_t timeout_ms)
{
  if (fd_ < 0)
  {
    return ErrorCode::STATE_ERR;
  }

  size_t written_total = 0;
  const auto deadline = MakeDeadline(timeout_ms);
  while (written_total < size)
  {
    if (TimedOut(deadline, timeout_ms))
    {
      return ErrorCode::TIMEOUT;
    }

    const ssize_t written = ::write(fd_, data + written_total, size - written_total);
    if (written > 0)
    {
      written_total += static_cast<size_t>(written);
      continue;
    }
    if (written < 0 && (errno == EINTR || errno == EAGAIN))
    {
      SleepWait();
      continue;
    }
    return ErrorCode::FAILED;
  }
  return ErrorCode::OK;
}

ErrorCode LinuxRS485::ReadExact(RawData data, uint32_t timeout_ms)
{
  if (data.size_ == 0)
  {
    return ErrorCode::OK;
  }
  if (fd_ < 0 || data.addr_ == nullptr)
  {
    return ErrorCode::STATE_ERR;
  }

  auto* out = static_cast<uint8_t*>(data.addr_);
  size_t received = 0;
  const auto deadline = MakeDeadline(timeout_ms);

  while (received < data.size_)
  {
    if (TimedOut(deadline, timeout_ms))
    {
      return ErrorCode::TIMEOUT;
    }

    int available = 0;
    if (::ioctl(fd_, TIOCINQ, &available) != 0)
    {
      available = 0;
    }

    if (available <= 0)
    {
      SleepWait();
      continue;
    }

    const size_t want = std::min(data.size_ - received, static_cast<size_t>(available));
    const ssize_t got = ::read(fd_, out + received, want);
    if (got > 0)
    {
      received += static_cast<size_t>(got);
      continue;
    }
    if (got < 0 && (errno == EINTR || errno == EAGAIN))
    {
      SleepWait();
      continue;
    }
    if (got == 0)
    {
      SleepWait();
      continue;
    }
    return ErrorCode::FAILED;
  }

  return ErrorCode::OK;
}

void LinuxRS485::DelayMicroseconds(uint32_t delay_us)
{
  if (delay_us == 0)
  {
    return;
  }

  const auto deadline = SteadyClock::now() + std::chrono::microseconds(delay_us);
  if (delay_us > 2000)
  {
    const uint32_t sleep_us = delay_us - 1000;
    std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
  }
  while (SteadyClock::now() < deadline)
  {
  }
}

void LinuxRS485::SleepWait() const
{
  if (platform_config_.wait_sleep_us == 0)
  {
    std::this_thread::yield();
    return;
  }
  std::this_thread::sleep_for(std::chrono::microseconds(platform_config_.wait_sleep_us));
}

bool LinuxRS485::ReleaseLegacySysfsLine(unsigned int line)
{
  const std::string gpio_path = "/sys/class/gpio/gpio" + std::to_string(line);
  if (!PathExists(gpio_path))
  {
    return true;
  }

  const int fd = ::open("/sys/class/gpio/unexport", O_WRONLY | O_CLOEXEC);
  if (fd < 0)
  {
    return false;
  }

  const std::string line_text = std::to_string(line);
  const ssize_t written = ::write(fd, line_text.data(), line_text.size());
  const int saved_errno = errno;
  ::close(fd);

  if (written < 0 && saved_errno != EINVAL)
  {
    return false;
  }

  return WaitForPathState(gpio_path, false, LEGACY_SYSFS_RETRY_WAIT_MS);
}

speed_t LinuxRS485::ToBaudrate(uint32_t baudrate)
{
  switch (baudrate)
  {
    case 9600:
      return B9600;
    case 19200:
      return B19200;
    case 38400:
      return B38400;
    case 57600:
      return B57600;
    case 115200:
      return B115200;
    case 230400:
      return B230400;
    case 460800:
      return B460800;
    case 921600:
      return B921600;
#ifdef B1000000
    case 1000000:
      return B1000000;
#endif
#ifdef B2000000
    case 2000000:
      return B2000000;
#endif
    default:
      return 0;
  }
}

}  // namespace LibXR
