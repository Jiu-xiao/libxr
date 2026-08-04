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
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <system_error>
#include <vector>

#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "semaphore.hpp"
#include "uart.hpp"
#include "uart/uart_rx_config_gate.hpp"

namespace LibXR
{
class LinuxUART;

namespace Detail
{
class LinuxUARTReadPort : public ReadPort
{
 public:
  explicit LinuxUARTReadPort(size_t size, LinuxUART& owner)
      : ReadPort(size), owner_(owner)
  {
  }

  void OnRxDequeue(bool in_isr) override;

  LinuxUARTReadPort& operator=(ReadFun fun)
  {
    ReadPort::operator=(fun);
    return *this;
  }

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
 * RX、TX、运行期配置、close 与重连全部由单一 I/O owner 线程串行化。`SetConfig()`
 * 只接纳一个异步配置事务，在该事务应用完成前返回 `BUSY`。 / RX, TX, runtime
 * configuration, close, and reconnect are serialized by one I/O owner thread.
 * `SetConfig()` admits one asynchronous configuration transaction and returns `BUSY`
 * until that transaction is applied.
 * @note 内部线程不会停止且持有 `this`；实例必须存活到进程结束。 / The internal thread
 * does not stop and retains `this`; the instance must remain alive until process exit.
 */
class LinuxUART : public UART
{
  friend class Detail::LinuxUARTReadPort;

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
        tx_buff_(new uint8_t[buffer_size]),
        buff_size_(buffer_size),
        initial_config_{baudrate, parity, data_bits, stop_bits},
        _read_port(buffer_size, *this),
        _write_port(tx_queue_size, buffer_size)
  {
    ASSERT(buff_size_ > 0);

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
        tx_buff_(new uint8_t[buffer_size]),
        buff_size_(buffer_size),
        initial_config_{baudrate, parity, data_bits, stop_bits},
        _read_port(buffer_size, *this),
        _write_port(tx_queue_size, buffer_size)
  {
    ASSERT(buff_size_ > 0);

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

  /**
   * @brief Submit one serialized runtime configuration transaction
   * @return `OK` on admission, `BUSY` while another configuration is outstanding
   * @note The owner waits for the active record, observes `TIOCOUTQ == 0`, and then
   *       applies the termios change. This is the nonblocking drain boundary exposed by
   *       the Linux tty driver; it is not an independent observation of every USB-UART
   *       bridge FIFO or of the external physical line.
   */
  ErrorCode SetConfig(UART::Configuration config) override
  {
    const ErrorCode validation = ValidateConfig(config);
    if (validation != ErrorCode::OK)
    {
      return validation;
    }

    if (!config_gate_.TryReserveConfig())
    {
      return ErrorCode::BUSY;
    }
    requested_config_ = config;
    config_gate_.PublishConfig();
    NotifyIoOwner();
    return ErrorCode::OK;
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

 private:
  static constexpr int RECONNECT_DELAY_MS = 1000;
  static constexpr int CONFIG_DRAIN_POLL_MS = 10;

  static ErrorCode ValidateConfig(const UART::Configuration& config)
  {
    if (config.baudrate == 0 || (config.stop_bits != 1 && config.stop_bits != 2) ||
        config.data_bits < 5 || config.data_bits > 8)
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

  void StartIoOwner(size_t thread_stack_size)
  {
    REQUIRE(ValidateConfig(initial_config_) == ErrorCode::OK);

    wake_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    REQUIRE(wake_fd_ >= 0);

    _read_port = ReadFun;
    _write_port = WriteFun;

    io_thread_.Create<LinuxUART*>(
        this, [](LinuxUART* self) { self->IoLoop(); }, "io_uart", thread_stack_size,
        Thread::Priority::REALTIME);

    REQUIRE(startup_sem_.Wait() == ErrorCode::OK);
    REQUIRE(startup_result_ == ErrorCode::OK);
  }

  void NotifyIoOwner()
  {
    const uint64_t value = 1U;
    while (true)
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
    while (true)
    {
      const ssize_t bytes = read(wake_fd_, &value, sizeof(value));
      if (bytes == static_cast<ssize_t>(sizeof(value)))
      {
        return;
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

  void SetLowLatency(int fd)
  {
    struct serial_struct serinfo{};
    if (ioctl(fd, TIOCGSERIAL, &serinfo) != 0)
    {
      return;
    }
    serinfo.flags |= ASYNC_LOW_LATENCY;
    (void)ioctl(fd, TIOCSSERIAL, &serinfo);
  }

  ErrorCode ApplyConfig(int fd, const UART::Configuration& config, bool flush)
  {
    const ErrorCode validation = ValidateConfig(config);
    if (validation != ErrorCode::OK)
    {
      return validation;
    }

    struct termios2 tio{};
    if (ioctl(fd, TCGETS2, &tio) != 0)
    {
      return ErrorCode::INIT_ERR;
    }

    // 设置自定义波特率
    tio.c_cflag &= ~CBAUD;
    tio.c_cflag |= BOTHER;
    tio.c_ispeed = config.baudrate;
    tio.c_ospeed = config.baudrate;

    // 输入模式：关闭软件流控、特殊字符处理
    tio.c_iflag &= ~(IXON | IXOFF | IXANY | ISTRIP | IGNCR | INLCR | ICRNL
#ifdef IUCLC
                     | IUCLC
#endif
    );

    // 输出模式：关闭所有加工
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

    // 本地模式：禁用行缓冲、回显、信号中断
    tio.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

    // 控制模式：设置数据位、校验、停止位、流控
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

    // 停止位
    tio.c_cflag &= ~CSTOPB;
    if (config.stop_bits == 2)
    {
      tio.c_cflag |= CSTOPB;
    }

    // 奇偶校验
    switch (config.parity)
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
      default:
        return ErrorCode::ARG_ERR;
    }

    // 禁用硬件流控
    tio.c_cflag &= ~CRTSCTS;

    // 启用本地模式、读功能
    tio.c_cflag |= (CLOCAL | CREAD);

    // 控制字符配置：阻塞直到读到 1 字节
    // for (int i = 0; i < NCCS; ++i) tio.c_cc[i] = 0;
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

  static ErrorCode ReadFun(ReadPort&, bool) { return ErrorCode::PENDING; }

  static ErrorCode WriteFun(WritePort& port, bool)
  {
    auto* uart = LibXR::ContainerOf(&port, &LinuxUART::_write_port);
    uart->NotifyIoOwner();
    return ErrorCode::PENDING;
  }

  int OpenDevice(const UART::Configuration& config, bool reconnect, ErrorCode& result)
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

  void CompleteActiveTx(ErrorCode result)
  {
    REQUIRE(tx_active_);
    WriteInfoBlock info = tx_active_info_;
    tx_active_ = false;
    tx_active_size_ = 0U;
    tx_active_offset_ = 0U;
    write_port_->Finish(false, result, info);
  }

  void Disconnect(int& fd, bool fail_active)
  {
    if (fd >= 0)
    {
      (void)close(fd);
      fd = -1;
    }
    if (fail_active && tx_active_)
    {
      CompleteActiveTx(ErrorCode::FAILED);
    }
  }

  bool LoadActiveTx()
  {
    if (tx_active_)
    {
      return false;
    }

    WriteInfoBlock info{};
    if (write_port_->queue_info_->Peek(info) != ErrorCode::OK ||
        !config_gate_.TryEnterTx())
    {
      return false;
    }

    REQUIRE(info.data.size_ <= buff_size_);
    const ErrorCode data_result =
        write_port_->queue_data_->PopBatch(tx_buff_, info.data.size_);
    REQUIRE(data_result == ErrorCode::OK);
    const ErrorCode info_result = write_port_->queue_info_->Pop(tx_active_info_);
    REQUIRE(info_result == ErrorCode::OK);

    tx_active_size_ = info.data.size_;
    tx_active_offset_ = 0U;
    tx_active_ = true;
    config_gate_.LeaveTx();
    return true;
  }

  bool PumpTx(int& fd)
  {
    while (tx_active_)
    {
      const size_t remaining = tx_active_size_ - tx_active_offset_;
      const ssize_t written = write(fd, tx_buff_ + tx_active_offset_, remaining);
      if (written > 0)
      {
        tx_active_offset_ += static_cast<size_t>(written);
        if (tx_active_offset_ == tx_active_size_)
        {
          CompleteActiveTx(ErrorCode::OK);
        }
        continue;
      }
      if (written < 0 && errno == EINTR)
      {
        continue;
      }
      if (written == 0 || (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)))
      {
        return true;
      }

      XR_LOG_WARN("Cannot write UART device: %s", device_path_.c_str());
      Disconnect(fd, true);
      return false;
    }
    return true;
  }

  bool DrainRx(int& fd, bool terminal = false)
  {
    size_t budget = terminal ? static_cast<size_t>(-1) : buff_size_;
    while (budget > 0U)
    {
      const size_t read_size = std::min(budget, read_port_->EmptySize());
      if (read_size == 0U)
      {
        return true;
      }

      const ssize_t bytes = read(fd, rx_buff_, read_size);
      if (bytes > 0)
      {
        budget -= static_cast<size_t>(bytes);
        const ErrorCode push_result =
            read_port_->queue_data_->PushBatch(rx_buff_, static_cast<size_t>(bytes));
        REQUIRE(push_result == ErrorCode::OK);
        read_port_->ProcessPendingReads(false);
        if (!terminal && config_gate_.ConfigRequested())
        {
          return true;
        }
        continue;
      }
      if (bytes < 0 && errno == EINTR)
      {
        continue;
      }
      if (bytes == 0 || (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)))
      {
        return true;
      }

      XR_LOG_WARN("Cannot read UART device: %s", device_path_.c_str());
      Disconnect(fd, true);
      return false;
    }
    return true;
  }

  bool ServiceConfig(int& fd, UART::Configuration& current_config)
  {
    if (tx_active_ || !config_gate_.TryEnterConfig())
    {
      return false;
    }

    int queued_bytes = 0;
    if (ioctl(fd, TIOCOUTQ, &queued_bytes) != 0)
    {
      XR_LOG_WARN("Cannot inspect UART output queue: %s", device_path_.c_str());
      Disconnect(fd, false);
      return true;
    }
    if (queued_bytes > 0)
    {
      return true;
    }

    const UART::Configuration requested = requested_config_;
    const ErrorCode result = ApplyConfig(fd, requested, false);
    if (result != ErrorCode::OK)
    {
      XR_LOG_WARN("Cannot apply UART configuration: %s", device_path_.c_str());
      Disconnect(fd, false);
      return true;
    }

    current_config = requested;
    config_gate_.LeaveConfig();
    return true;
  }

  void WaitForReconnectWake()
  {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(RECONNECT_DELAY_MS);
    pollfd wake_poll{};
    wake_poll.fd = wake_fd_;
    wake_poll.events = POLLIN;

    while (true)
    {
      const auto now = std::chrono::steady_clock::now();
      if (now >= deadline)
      {
        return;
      }

      const auto remaining =
          std::chrono::ceil<std::chrono::milliseconds>(deadline - now).count();
      wake_poll.revents = 0;
      const int result = poll(&wake_poll, 1U, static_cast<int>(remaining));
      if (result < 0 && errno == EINTR)
      {
        continue;
      }

      REQUIRE(result >= 0);
      if (result == 0)
      {
        return;
      }
      REQUIRE((wake_poll.revents & (POLLERR | POLLHUP | POLLNVAL)) == 0);
      if ((wake_poll.revents & POLLIN) != 0)
      {
        DrainWakeFd();
      }
    }
  }

  void IoLoop()
  {
    UART::Configuration current_config = initial_config_;
    ErrorCode open_result = ErrorCode::INIT_ERR;
    int fd = OpenDevice(current_config, false, open_result);

    startup_result_ = open_result;
    startup_sem_.Post();

    while (true)
    {
      if (fd < 0)
      {
        const bool config_active = config_gate_.TryEnterConfig();
        const UART::Configuration target_config =
            config_active ? requested_config_ : current_config;
        fd = OpenDevice(target_config, true, open_result);
        if (fd >= 0)
        {
          current_config = target_config;
          if (config_active)
          {
            config_gate_.LeaveConfig();
          }
          continue;
        }
        WaitForReconnectWake();
        continue;
      }

      const bool config_progress = ServiceConfig(fd, current_config);
      if (fd < 0)
      {
        continue;
      }
      if (!config_gate_.ConfigRequested() && !tx_active_)
      {
        (void)LoadActiveTx();
      }

      std::array<pollfd, 2> poll_fds{};
      poll_fds[0].fd = fd;
      bool rx_space_armed = false;
      if (read_port_->EmptySize() == 0U)
      {
        // Arm before re-checking so a dequeue cannot be lost before poll starts.
        rx_space_waiting_.exchange(true, std::memory_order_acq_rel);
        if (read_port_->EmptySize() == 0U)
        {
          rx_space_armed = true;
        }
        else
        {
          rx_space_waiting_.exchange(false, std::memory_order_acq_rel);
          poll_fds[0].events |= POLLIN;
        }
      }
      else
      {
        poll_fds[0].events |= POLLIN;
      }
      if (tx_active_)
      {
        poll_fds[0].events |= POLLOUT;
      }
      poll_fds[1].fd = wake_fd_;
      poll_fds[1].events = POLLIN;

      const int timeout = config_progress && config_gate_.ConfigRequested() && !tx_active_
                              ? CONFIG_DRAIN_POLL_MS
                              : -1;
      int poll_result = 0;
      do
      {
        poll_result = poll(poll_fds.data(), poll_fds.size(), timeout);
      } while (poll_result < 0 && errno == EINTR);

      if (rx_space_armed)
      {
        rx_space_waiting_.exchange(false, std::memory_order_acq_rel);
      }
      if (poll_result < 0)
      {
        XR_LOG_WARN("Cannot poll UART device: %s", device_path_.c_str());
        Disconnect(fd, true);
        continue;
      }
      if (poll_result == 0)
      {
        continue;
      }

      REQUIRE((poll_fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) == 0);
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
        Disconnect(fd, true);
        continue;
      }
      if (fd >= 0 && (poll_fds[0].revents & POLLOUT) != 0 && tx_active_)
      {
        (void)PumpTx(fd);
      }
    }
  }

  std::string device_path_;
  Thread io_thread_;
  uint8_t* rx_buff_ = nullptr;
  uint8_t* tx_buff_ = nullptr;
  size_t buff_size_ = 0;
  Configuration initial_config_{};
  Configuration requested_config_{};
  UartRxConfigGate config_gate_;
  int wake_fd_ = -1;
  Semaphore startup_sem_;
  ErrorCode startup_result_ = ErrorCode::INIT_ERR;
  WriteInfoBlock tx_active_info_{};
  size_t tx_active_size_ = 0U;
  size_t tx_active_offset_ = 0U;
  bool tx_active_ = false;
  std::atomic<bool> rx_space_waiting_{false};

  Detail::LinuxUARTReadPort _read_port;  // NOLINT
  WritePort _write_port;                 // NOLINT
};

inline void Detail::LinuxUARTReadPort::OnRxDequeue(bool)
{
  if (owner_.rx_space_waiting_.exchange(false, std::memory_order_acq_rel))
  {
    owner_.NotifyIoOwner();
  }
}

}  // namespace LibXR
