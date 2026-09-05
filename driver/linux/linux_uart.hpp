#pragma once

#define termios asmtermios
#include <asm/termbits.h>
#undef termios
#include <fcntl.h>
#include <libudev.h>
#include <linux/serial.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "semaphore.hpp"
#include "uart.hpp"

namespace LibXR
{
class LinuxUART;

namespace Detail
{
class LinuxUARTReadPort : public ReadPort
{
 public:
  LinuxUARTReadPort(size_t size, LinuxUART& owner) : ReadPort(size), owner_(owner) {}

  void OnReadQueueSpaceAvailable(bool) override;

 private:
  LinuxUART& owner_;
};
}  // namespace Detail

/**
 * @brief Linux UART 串口驱动实现 / Linux UART driver implementation
 *
 * 支持按设备路径或 USB VID/PID(/control interface name[/serial]) 自动发现并创建 UART
 * 通道。
 * Supports UART creation by device path or USB VID/PID(/control interface
 * name[/serial]) auto discovery.
 *
 * RX, TX, configuration, and reconnect are serialized by one I/O owner thread.
 * The device is opened in nonblocking mode; port callbacks only wake that owner.
 */
class LinuxUART : public UART
{
  friend class Detail::LinuxUARTReadPort;

  enum class ConfigState : uint8_t
  {
    EMPTY,
    RESERVED,
    PUBLISHED,
  };

 public:
  /**
   * @brief 通过设备路径构造 UART / Construct UART by device path
   */
  LinuxUART(const char* dev_path, unsigned int baudrate = 115200,
            Parity parity = Parity::NO_PARITY, uint8_t data_bits = 8,
            uint8_t stop_bits = 1, uint32_t tx_queue_size = 5, size_t buffer_size = 512,
            size_t thread_stack_size = 65536)
      : UART(&_read_port, &_write_port),
        rx_buff_(new uint8_t[buffer_size]),
        buff_size_(buffer_size),
        config_{baudrate, parity, data_bits, stop_bits},
        _read_port(buffer_size, *this),
        _write_port(tx_queue_size, buffer_size)
  {
    ASSERT(buff_size_ > 0);
    REQUIRE(tx_queue_size > 0);

    while (!std::filesystem::exists(dev_path))
    {
      XR_LOG_WARN("Cannot find UART device: %s, retrying...", dev_path);
      Thread::Sleep(100);
    }

    device_path_ = GetByPathForTTY(dev_path);

    StartIoOwner(thread_stack_size);
  }

  /**
   * @brief 通过 USB VID/PID 构造 UART / Construct UART by USB VID/PID
   */
  LinuxUART(const std::string& vid, const std::string& pid,
            unsigned int baudrate = 115200, Parity parity = Parity::NO_PARITY,
            uint8_t data_bits = 8, uint8_t stop_bits = 1, uint32_t tx_queue_size = 5,
            size_t buffer_size = 512, size_t thread_stack_size = 65536)
      : LinuxUART(vid, pid, "", "", baudrate, parity, data_bits, stop_bits, tx_queue_size,
                  buffer_size, thread_stack_size)
  {
  }

  /**
   * @brief 通过 USB VID/PID/control interface name 构造 UART
   *        Construct UART by USB VID/PID/control interface name
   *
   * @note CDC ACM 有 control/data 两个 interface；这里匹配 Linux ttyACM 父级
   *       usb_interface 的 control interface 名称，不匹配 data interface 名称。
   *       CDC ACM has control/data interfaces; this selector matches the Linux
   *       ttyACM parent usb_interface control interface name, not the data
   *       interface name.
   */
  LinuxUART(const std::string& vid, const std::string& pid,
            const std::string& control_interface_name, unsigned int baudrate = 115200,
            Parity parity = Parity::NO_PARITY, uint8_t data_bits = 8,
            uint8_t stop_bits = 1, uint32_t tx_queue_size = 5, size_t buffer_size = 512,
            size_t thread_stack_size = 65536)
      : LinuxUART(vid, pid, control_interface_name, "", baudrate, parity, data_bits,
                  stop_bits, tx_queue_size, buffer_size, thread_stack_size)
  {
  }

  /**
   * @brief 通过 USB VID/PID/control interface name/serial 构造 UART
   *        Construct UART by USB VID/PID/control interface name/serial
   *
   * @note CDC ACM 有 control/data 两个 interface；这里匹配 Linux ttyACM 父级
   *       usb_interface 的 control interface 名称，不匹配 data interface 名称。
   *       CDC ACM has control/data interfaces; this selector matches the Linux
   *       ttyACM parent usb_interface control interface name, not the data
   *       interface name.
   */
  LinuxUART(const std::string& vid, const std::string& pid,
            const std::string& control_interface_name, const std::string& serial,
            unsigned int baudrate = 115200, Parity parity = Parity::NO_PARITY,
            uint8_t data_bits = 8, uint8_t stop_bits = 1, uint32_t tx_queue_size = 5,
            size_t buffer_size = 512, size_t thread_stack_size = 65536)
      : UART(&_read_port, &_write_port),
        rx_buff_(new uint8_t[buffer_size]),
        buff_size_(buffer_size),
        config_{baudrate, parity, data_bits, stop_bits},
        _read_port(buffer_size, *this),
        _write_port(tx_queue_size, buffer_size)
  {
    REQUIRE(tx_queue_size > 0);
    while (!FindUSBTTYByVidPid(vid, pid, control_interface_name, serial, device_path_))
    {
      XR_LOG_WARN(
          "Cannot find USB TTY device with VID=%s PID=%s SERIAL=%s CONTROL_INTERFACE=%s, "
          "retrying...",
          vid.c_str(), pid.c_str(), serial.empty() ? "*" : serial.c_str(),
          control_interface_name.empty() ? "*" : control_interface_name.c_str());
      Thread::Sleep(100);
    }

    XR_LOG_PASS("Found USB TTY: %s", device_path_.c_str());

    if (std::filesystem::exists(device_path_) == false)
    {
      XR_LOG_ERROR("Cannot find UART device: %s", device_path_.c_str());
      ASSERT(false);
      return;
    }

    device_path_ = GetByPathForTTY(device_path_);

    StartIoOwner(thread_stack_size);
  }

  std::string GetByPathForTTY(const std::string& tty_name)
  {
    const std::string BASE = "/dev/serial/by-path";
    if (strncmp(tty_name.c_str(), BASE.c_str(), BASE.length()) == 0 ||
        !std::filesystem::exists(BASE))
    {
      return tty_name;
    }
    for (const auto& entry : std::filesystem::directory_iterator(BASE))
    {
      std::error_code ec;
      const auto full = std::filesystem::canonical(entry.path(), ec);
      if (ec)
      {
        continue;
      }
      if (full == tty_name)
      {
        return entry.path().string();  // 返回符号链接路径
      }
    }
    return tty_name;  // 未命中 by-path 时保留原始 tty 路径
  }

  static bool FindUSBTTYByVidPid(const std::string& target_vid,
                                 const std::string& target_pid, std::string& tty_path)
  {
    return FindUSBTTYByVidPid(target_vid, target_pid, "", "", tty_path);
  }

  static bool FindUSBTTYByVidPid(const std::string& target_vid,
                                 const std::string& target_pid,
                                 const std::string& target_control_interface_name,
                                 std::string& tty_path)
  {
    return FindUSBTTYByVidPid(target_vid, target_pid, target_control_interface_name, "",
                              tty_path);
  }

  static bool FindUSBTTYByVidPid(const std::string& target_vid,
                                 const std::string& target_pid,
                                 const std::string& target_control_interface_name,
                                 const std::string& target_serial, std::string& tty_path)
  {
    struct udev* udev = udev_new();
    if (!udev)
    {
      XR_LOG_ERROR("Cannot create udev context");
      return false;
    }

    struct udev_enumerate* enumerate = udev_enumerate_new(udev);
    udev_enumerate_add_match_subsystem(enumerate, "tty");
    udev_enumerate_scan_devices(enumerate);

    struct udev_list_entry* devices = udev_enumerate_get_list_entry(enumerate);
    struct udev_list_entry* entry = nullptr;
    std::vector<std::string> matches;

    udev_list_entry_foreach(entry, devices)
    {
      const char* path = udev_list_entry_get_name(entry);
      struct udev_device* tty_dev = udev_device_new_from_syspath(udev, path);
      if (!tty_dev)
      {
        continue;
      }

      struct udev_device* usb_dev =
          udev_device_get_parent_with_subsystem_devtype(tty_dev, "usb", "usb_device");
      struct udev_device* usb_interface =
          udev_device_get_parent_with_subsystem_devtype(tty_dev, "usb", "usb_interface");

      if (usb_dev)
      {
        const char* vid = udev_device_get_sysattr_value(usb_dev, "idVendor");
        const char* pid = udev_device_get_sysattr_value(usb_dev, "idProduct");
        const char* serial = udev_device_get_sysattr_value(usb_dev, "serial");
        const char* control_interface_name = nullptr;
        if (usb_interface)
        {
          // For Linux cdc_acm tty nodes this parent is the CDC control interface
          // (for example if00 / if02), not the CDC data interface.
          control_interface_name =
              udev_device_get_sysattr_value(usb_interface, "interface");
        }

        if (vid && pid && target_vid == vid && target_pid == pid &&
            (target_serial.empty() || (serial && target_serial == serial)) &&
            (target_control_interface_name.empty() ||
             (control_interface_name &&
              target_control_interface_name == control_interface_name)))
        {
          const char* devnode = udev_device_get_devnode(tty_dev);
          if (devnode)
          {
            matches.emplace_back(devnode);
          }
        }
      }

      udev_device_unref(tty_dev);
    }

    udev_enumerate_unref(enumerate);
    udev_unref(udev);
    if (matches.empty())
    {
      return false;
    }

    std::sort(matches.begin(), matches.end());
    tty_path = matches.front();

    if (matches.size() > 1)
    {
      XR_LOG_WARN(
          "Multiple USB TTY devices found with VID=%s PID=%s SERIAL=%s "
          "CONTROL_INTERFACE=%s, using %s. Specify serial or control interface name to "
          "disambiguate.",
          target_vid.c_str(), target_pid.c_str(),
          target_serial.empty() ? "*" : target_serial.c_str(),
          target_control_interface_name.empty() ? "*"
                                                : target_control_interface_name.c_str(),
          tty_path.c_str());
    }

    return true;
  }

  void SetLowLatency(int fd)
  {
    struct serial_struct serinfo;
    ioctl(fd, TIOCGSERIAL, &serinfo);
    serinfo.flags |= ASYNC_LOW_LATENCY;
    ioctl(fd, TIOCSSERIAL, &serinfo);
  }

  ErrorCode SetConfig(UART::Configuration config, bool = false) override
  {
    if (ValidateConfig(config) != ErrorCode::OK)
    {
      return ErrorCode::ARG_ERR;
    }

    uint8_t expected = static_cast<uint8_t>(ConfigState::EMPTY);
    if (!config_state_.compare_exchange_strong(
            expected, static_cast<uint8_t>(ConfigState::RESERVED),
            std::memory_order_acq_rel, std::memory_order_acquire))
    {
      return ErrorCode::BUSY;
    }

    requested_config_ = config;
    config_state_.store(static_cast<uint8_t>(ConfigState::PUBLISHED),
                        std::memory_order_release);
    NotifyIoOwner();
    return ErrorCode::OK;
  }

 private:
  static ErrorCode ValidateConfig(const UART::Configuration& config)
  {
    if (config.baudrate == 0U || (config.stop_bits != 1U && config.stop_bits != 2U) ||
        config.data_bits < 5U || config.data_bits > 8U)
    {
      return ErrorCode::ARG_ERR;
    }

    switch (config.parity)
    {
      case UART::Parity::NO_PARITY:
      case UART::Parity::EVEN:
      case UART::Parity::ODD:
        return ErrorCode::OK;
      default:
        return ErrorCode::ARG_ERR;
    }
  }

  ErrorCode ApplyConfig(int fd, const UART::Configuration& config, bool flush)
  {
    struct termios2 tio{};
    if (ioctl(fd, TCGETS2, &tio) != 0)
    {
      return ErrorCode::INIT_ERR;
    }

    tio.c_cflag &= ~CBAUD;
    tio.c_cflag |= BOTHER;
    tio.c_ispeed = config.baudrate;
    tio.c_ospeed = config.baudrate;

    tio.c_iflag &= ~(IXON | IXOFF | IXANY | ISTRIP | IGNCR | INLCR | ICRNL
#ifdef IUCLC
                     | IUCLC
#endif
    );

    tio.c_oflag &= ~(OPOST
#ifdef ONLCR
                     | ONLCR
#endif
#ifdef OCRNL
                     | OCRNL
#endif
#ifdef ONOCR
                     | ONOCR
#endif
#ifdef ONLRET
                     | ONLRET
#endif
    );

    tio.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

    tio.c_cflag &= ~CSIZE;
    switch (config.data_bits)
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

    tio.c_cflag &= ~CSTOPB;
    if (config.stop_bits == 2U)
    {
      tio.c_cflag |= CSTOPB;
    }

    tio.c_cflag &= ~(PARENB | PARODD);
    switch (config.parity)
    {
      case UART::Parity::NO_PARITY:
        break;
      case UART::Parity::EVEN:
        tio.c_cflag |= PARENB;
        break;
      case UART::Parity::ODD:
        tio.c_cflag |= PARENB | PARODD;
        break;
    }

    tio.c_cflag &= ~CRTSCTS;
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cc[VTIME] = 0;
    tio.c_cc[VMIN] = 1;

    if (ioctl(fd, TCSETS2, &tio) != 0)
    {
      return ErrorCode::INIT_ERR;
    }

    SetLowLatency(fd);
    if (flush && tcflush(fd, TCIOFLUSH) != 0)
    {
      return ErrorCode::INIT_ERR;
    }
    return ErrorCode::OK;
  }

  static void WriteFun(WritePort& port, bool)
  {
    auto* uart = LibXR::ContainerOf(&port, &LinuxUART::_write_port);
    uart->NotifyIoOwner();
  }

  static constexpr int RECONNECT_DELAY_MS = 1000;
  static constexpr int CONFIG_DRAIN_POLL_MS = 10;

  void StartIoOwner(size_t thread_stack_size)
  {
    REQUIRE(ValidateConfig(config_) == ErrorCode::OK);

    wake_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    REQUIRE(wake_fd_ >= 0);

    _write_port = WriteFun;
    io_thread_.Create<LinuxUART*>(
        this, [](LinuxUART* self) { self->IoLoop(); }, "io_uart", thread_stack_size,
        Thread::Priority::REALTIME);

    ErrorCode wait_result = ErrorCode::TIMEOUT;
    do
    {
      wait_result = startup_sem_.Wait();
    } while (wait_result == ErrorCode::TIMEOUT);
    REQUIRE(wait_result == ErrorCode::OK);
  }

  void NotifyIoOwner()
  {
    REQUIRE(wake_fd_ >= 0);
    const uint64_t value = 1U;
    for (;;)
    {
      const ssize_t written = write(wake_fd_, &value, sizeof(value));
      if (written == static_cast<ssize_t>(sizeof(value)))
      {
        return;
      }
      if (written < 0 && errno == EINTR)
      {
        continue;
      }
      if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
      {
        return;
      }
      REQUIRE(false);
    }
  }

  void DrainWakeFd()
  {
    uint64_t value = 0U;
    for (;;)
    {
      const ssize_t bytes = read(wake_fd_, &value, sizeof(value));
      if (bytes == static_cast<ssize_t>(sizeof(value)))
      {
        continue;
      }
      if (bytes < 0 && errno == EINTR)
      {
        continue;
      }
      if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
      {
        return;
      }
      REQUIRE(false);
    }
  }

  int OpenDevice(const Configuration& config, bool reconnect, ErrorCode& result)
  {
    const int fd = open(device_path_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
    {
      result = ErrorCode::INIT_ERR;
      XR_LOG_WARN("Cannot open UART device: %s", device_path_.c_str());
      return -1;
    }

    result = ApplyConfig(fd, config, true);
    if (result != ErrorCode::OK)
    {
      XR_LOG_WARN("Cannot configure UART device: %s", device_path_.c_str());
      (void)close(fd);
      return -1;
    }

    XR_LOG_PASS("%s UART device: %s", reconnect ? "Reopen" : "Open",
                device_path_.c_str());
    return fd;
  }

  void Disconnect(int& fd, bool fail_front)
  {
    if (fd >= 0)
    {
      (void)close(fd);
      fd = -1;
    }

    if (fail_front)
    {
      auto queue = _write_port.GetWriteQueue(false);
      if (!queue.Empty())
      {
        queue.FailFront(ErrorCode::FAILED);
      }
      tx_front_unreplayable_ = false;
    }
  }

  size_t WriteSpans(int fd, const uint8_t* first, size_t first_size,
                    const uint8_t* second, size_t second_size, bool& fatal_error)
  {
    std::array<iovec, 2> spans{};
    spans[0] = iovec{const_cast<uint8_t*>(first), first_size};
    spans[1] = iovec{const_cast<uint8_t*>(second), second_size};
    const int span_count = second_size == 0U ? 1 : 2;

    for (;;)
    {
      const ssize_t written = writev(fd, spans.data(), span_count);
      if (written > 0)
      {
        const size_t accepted = static_cast<size_t>(written);
        REQUIRE(accepted <= first_size + second_size);
        return accepted;
      }
      if (written == 0)
      {
        fatal_error = true;
        return 0U;
      }
      if (errno == EINTR)
      {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK)
      {
        return 0U;
      }

      fatal_error = true;
      return 0U;
    }
  }

  bool HasPendingTx() { return _write_port.Size() != 0U; }

  bool PumpTx(int& fd)
  {
    bool fatal_error = false;
    size_t offered = 0U;
    size_t accepted = 0U;
    {
      auto queue = _write_port.GetWriteQueue(false);
      offered = queue.AvailableSize();
      if (offered != 0U)
      {
        accepted = queue.PopWithWriter(
            offered,
            [this, fd, &fatal_error](const uint8_t* first, size_t first_size,
                                     const uint8_t* second, size_t second_size) -> size_t
            {
              return WriteSpans(fd, first, first_size, second, second_size, fatal_error);
            });
      }
    }

    if (offered == 0U)
    {
      return false;
    }

    if (fatal_error)
    {
      XR_LOG_WARN("Cannot write UART device: %s", device_path_.c_str());
      Disconnect(fd, true);
      return false;
    }

    if (accepted == 0U)
    {
      // Preserve an earlier accepted prefix while the same front waits for
      // the next writable turn.
      return true;
    }
    tx_front_unreplayable_ = accepted < offered;
    return accepted < offered || HasPendingTx();
  }

  bool DrainRx(int& fd, bool terminal = false)
  {
    size_t budget = terminal ? static_cast<size_t>(-1) : buff_size_;
    while (budget > 0U)
    {
      const size_t empty_size = _read_port.EmptySize();
      if (empty_size == 0U)
      {
        return true;
      }

      const size_t read_size = std::min(budget, empty_size);
      auto queue = _read_port.GetReadQueue(false);
      const ssize_t bytes = read(fd, rx_buff_, read_size);
      if (bytes > 0)
      {
        REQUIRE(queue.PushBatch(rx_buff_, static_cast<size_t>(bytes)) == ErrorCode::OK);
        queue.Publish();
        budget -= static_cast<size_t>(bytes);
        continue;
      }

      queue.Publish();
      if (bytes < 0 && errno == EINTR)
      {
        continue;
      }
      if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
      {
        return true;
      }
      if (bytes == 0)
      {
        XR_LOG_WARN("UART device closed: %s", device_path_.c_str());
      }
      else
      {
        XR_LOG_WARN("Cannot read UART device: %s", device_path_.c_str());
      }
      Disconnect(fd, tx_front_unreplayable_);
      return false;
    }
    return true;
  }

  bool ConfigRequested() const
  {
    return config_state_.load(std::memory_order_acquire) ==
           static_cast<uint8_t>(ConfigState::PUBLISHED);
  }

  void ServiceConfig(int& fd, Configuration& current_config)
  {
    if (!ConfigRequested() || HasPendingTx())
    {
      return;
    }

    int queued_bytes = 0;
    if (ioctl(fd, TIOCOUTQ, &queued_bytes) != 0)
    {
      XR_LOG_WARN("Cannot inspect UART output queue: %s", device_path_.c_str());
      Disconnect(fd, false);
      return;
    }
    if (queued_bytes > 0)
    {
      return;
    }

    const Configuration requested = requested_config_;
    const ErrorCode result = ApplyConfig(fd, requested, false);
    if (result != ErrorCode::OK)
    {
      XR_LOG_WARN("Cannot apply UART configuration: %s", device_path_.c_str());
      Disconnect(fd, false);
      return;
    }

    current_config = requested;
    config_state_.store(static_cast<uint8_t>(ConfigState::EMPTY),
                        std::memory_order_release);
  }

  void WaitForReconnectWake()
  {
    pollfd wake_poll{};
    wake_poll.fd = wake_fd_;
    wake_poll.events = POLLIN;

    int result = 0;
    do
    {
      result = poll(&wake_poll, 1U, RECONNECT_DELAY_MS);
    } while (result < 0 && errno == EINTR);

    REQUIRE(result >= 0);
    if (result > 0)
    {
      DrainWakeFd();
    }
  }

  void IoLoop()
  {
    Configuration current_config = config_;
    ErrorCode open_result = ErrorCode::INIT_ERR;
    int fd = OpenDevice(current_config, false, open_result);

    startup_sem_.Post();

    while (true)
    {
      if (fd < 0)
      {
        const bool config_pending = ConfigRequested();
        const Configuration target_config =
            config_pending ? requested_config_ : current_config;
        fd = OpenDevice(target_config, true, open_result);
        if (fd >= 0)
        {
          current_config = target_config;
          if (config_pending)
          {
            config_state_.store(static_cast<uint8_t>(ConfigState::EMPTY),
                                std::memory_order_release);
          }
          continue;
        }

        WaitForReconnectWake();
        continue;
      }

      const bool tx_waiting = PumpTx(fd);
      if (fd < 0)
      {
        continue;
      }

      ServiceConfig(fd, current_config);
      if (fd < 0)
      {
        continue;
      }

      std::array<pollfd, 2> poll_fds{};
      poll_fds[0].fd = fd;
      bool rx_space_armed = false;
      if (_read_port.EmptySize() == 0U)
      {
        rx_space_waiting_.store(true, std::memory_order_release);
        if (_read_port.EmptySize() == 0U)
        {
          rx_space_armed = true;
        }
        else
        {
          rx_space_waiting_.store(false, std::memory_order_release);
          poll_fds[0].events |= POLLIN;
        }
      }
      else
      {
        poll_fds[0].events |= POLLIN;
      }
      if (tx_waiting)
      {
        poll_fds[0].events |= POLLOUT;
      }

      poll_fds[1].fd = wake_fd_;
      poll_fds[1].events = POLLIN;
      const int timeout = ConfigRequested() && !tx_waiting ? CONFIG_DRAIN_POLL_MS : -1;

      int poll_result = 0;
      do
      {
        poll_result = poll(poll_fds.data(), poll_fds.size(), timeout);
      } while (poll_result < 0 && errno == EINTR);

      if (rx_space_armed)
      {
        rx_space_waiting_.store(false, std::memory_order_release);
      }
      if (poll_result < 0)
      {
        XR_LOG_WARN("Cannot poll UART device: %s", device_path_.c_str());
        Disconnect(fd, tx_front_unreplayable_);
        continue;
      }
      if (poll_result == 0)
      {
        continue;
      }

      if ((poll_fds[1].revents & POLLIN) != 0)
      {
        DrainWakeFd();
      }

      const bool hung_up = (poll_fds[0].revents & POLLHUP) != 0;
      if (((poll_fds[0].revents & POLLIN) != 0 || hung_up) && !DrainRx(fd, hung_up))
      {
        continue;
      }
      if (fd < 0)
      {
        continue;
      }
      if ((poll_fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
      {
        XR_LOG_WARN("UART device disconnected: %s", device_path_.c_str());
        Disconnect(fd, tx_front_unreplayable_);
        continue;
      }
    }
  }

  std::string device_path_;
  Thread io_thread_;
  uint8_t* rx_buff_ = nullptr;
  size_t buff_size_ = 0;
  Configuration config_{};
  Configuration requested_config_{};
  int wake_fd_ = -1;
  Semaphore startup_sem_;
  std::atomic<uint8_t> config_state_{static_cast<uint8_t>(ConfigState::EMPTY)};
  std::atomic<bool> rx_space_waiting_{false};
  bool tx_front_unreplayable_ = false;

  Detail::LinuxUARTReadPort _read_port;  // NOLINT
  WritePort _write_port;                 // NOLINT
};

inline void Detail::LinuxUARTReadPort::OnReadQueueSpaceAvailable(bool)
{
  if (owner_.rx_space_waiting_.exchange(false, std::memory_order_acq_rel))
  {
    owner_.NotifyIoOwner();
  }
}

}  // namespace LibXR
