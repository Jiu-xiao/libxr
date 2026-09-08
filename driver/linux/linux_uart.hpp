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
/**
 * @brief 通知串口线程恢复接收的读端口 / Read port that wakes UART reception.
 */
class LinuxUARTReadPort : public ReadPort
{
 public:
  /// 构造接收端口并关联串口 / Construct the RX port associated with its UART.
  LinuxUARTReadPort(size_t size, LinuxUART& owner) : ReadPort(size), owner_(owner) {}

  /// 释放空间后唤醒串口线程 / Wake the UART thread after space is freed.
  void OnReadQueueSpaceAvailable(bool) override;

 private:
  LinuxUART& owner_;  ///< 所属串口 / Associated UART.
};
}  // namespace Detail

/**
 * @brief 使用单个 I/O 线程的 Linux 串口 / Linux UART with one I/O thread.
 *
 * 支持设备路径和 USB VID/PID、控制接口名称、序列号匹配。线程统一负责打开、关闭、
 * 接收、发送、配置和重连，读写端口的通知只唤醒该线程。
 * Supports device paths or USB VID/PID, control-interface name, and serial matching.
 * One thread handles open, close, RX, TX, configuration, and reconnect. Port callbacks
 * only wake that thread.
 *
 * @note 写完成表示数据已被 Linux 接收。短写保留剩余部分；部分发送后断线会以 FAILED
 *       结束该请求，尚未发送的后续请求保留到重连。接收队列满时暂停读取设备。
 *       Writes complete on kernel acceptance. Short writes retain the suffix; a
 *       disconnect after partial transmission fails that request and retains later
 *       requests for reconnect. Full RX queues pause device reads.
 * @pre 读写操作遵守端口约定。对象用于长期运行，线程结束前须保持对象和端口有效。
 *      Follow the port contracts. Keep this startup-lifetime object and its ports
 *      valid for the lifetime of the I/O thread.
 */
class LinuxUART : public UART
{
  friend class Detail::LinuxUARTReadPort;

  /// 单个待应用配置的发布状态 / Publication state of the pending configuration.
  enum class ConfigState : uint8_t
  {
    EMPTY,      ///< 可接受配置 / Available for a configuration.
    RESERVED,   ///< 调用者正在写入 / Caller writing the configuration.
    PUBLISHED,  ///< 等待线程应用 / Awaiting application by the I/O thread.
  };

 public:
  /**
   * @brief 通过设备路径构造串口 / Construct a UART by device path.
   * @param dev_path 设备路径 / Device path.
   * @param baudrate 初始波特率，必须大于零 / Positive initial baud rate.
   * @param parity 奇偶校验方式 / Parity mode.
   * @param data_bits 数据位数，范围 5 至 8 / Data bits, from 5 to 8.
   * @param stop_bits 停止位数，1 或 2 / Stop bits, 1 or 2.
   * @param tx_queue_size 请求队列容量，必须大于零 / Positive TX request capacity.
   * @param buffer_size 每个方向的字节队列容量，必须大于零 / Positive per-direction
   * capacity.
   * @param thread_stack_size I/O 线程栈大小，单位为字节 / I/O thread stack size in bytes.
   * @note 路径不存在时等待；路径存在后等待线程首次尝试打开设备，失败由线程继续重连。
   *       Waits for the path, then for the thread's first open attempt. Failure is
   *       recovered by the thread's reconnect loop.
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
   * @brief 通过 USB VID/PID 构造串口 / Construct a UART by USB VID/PID.
   * @param vid USB 厂商 ID 字符串 / USB vendor ID string.
   * @param pid USB 产品 ID 字符串 / USB product ID string.
   * @param baudrate 初始波特率，必须大于零 / Positive initial baud rate.
   * @param parity 奇偶校验方式 / Parity mode.
   * @param data_bits 数据位数，范围 5 至 8 / Data bits, from 5 to 8.
   * @param stop_bits 停止位数，1 或 2 / Stop bits, 1 or 2.
   * @param tx_queue_size 请求队列容量，必须大于零 / Positive TX request capacity.
   * @param buffer_size 每个方向的字节队列容量，必须大于零 / Positive per-direction
   * capacity.
   * @param thread_stack_size I/O 线程栈大小，单位为字节 / I/O thread stack size in bytes.
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
   * @brief 按 USB 控制接口名称构造串口 / Construct a UART by USB control interface.
   * @param vid USB 厂商 ID 字符串 / USB vendor ID string.
   * @param pid USB 产品 ID 字符串 / USB product ID string.
   * @param control_interface_name 控制接口名称，空值不限制 / Control interface, empty for
   * any.
   * @param baudrate 初始波特率，必须大于零 / Positive initial baud rate.
   * @param parity 奇偶校验方式 / Parity mode.
   * @param data_bits 数据位数，范围 5 至 8 / Data bits, from 5 to 8.
   * @param stop_bits 停止位数，1 或 2 / Stop bits, 1 or 2.
   * @param tx_queue_size 请求队列容量，必须大于零 / Positive TX request capacity.
   * @param buffer_size 每个方向的字节队列容量，必须大于零 / Positive per-direction
   * capacity.
   * @param thread_stack_size I/O 线程栈大小，单位为字节 / I/O thread stack size in bytes.
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
   * @brief 按 USB 接口和序列号构造串口 / Construct a UART by USB interface and serial.
   * @param vid USB 厂商 ID 字符串 / USB vendor ID string.
   * @param pid USB 产品 ID 字符串 / USB product ID string.
   * @param control_interface_name 控制接口名称，空值不限制 / Control interface, empty for
   * any.
   * @param serial 设备序列号，空值不限制 / Device serial number, empty for any.
   * @param baudrate 初始波特率，必须大于零 / Positive initial baud rate.
   * @param parity 奇偶校验方式 / Parity mode.
   * @param data_bits 数据位数，范围 5 至 8 / Data bits, from 5 to 8.
   * @param stop_bits 停止位数，1 或 2 / Stop bits, 1 or 2.
   * @param tx_queue_size 请求队列容量，必须大于零 / Positive TX request capacity.
   * @param buffer_size 每个方向的字节队列容量，必须大于零 / Positive per-direction
   * capacity.
   * @param thread_stack_size I/O 线程栈大小，单位为字节 / I/O thread stack size in bytes.
   * @note 等待匹配设备出现；多个匹配时按路径排序选择首项。CDC ACM 匹配控制接口名称。
   *       Waits for a match; selects the first sorted path on ambiguity. CDC ACM
   *       matching uses the control-interface name.
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
    REQUIRE(buff_size_ > 0);
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
   * @brief 查找设备的稳定路径 / Find a stable path for a TTY.
   * @param tty_name 原始设备路径 / Original device path.
   * @return 匹配的 by-path 链接，未找到时返回原路径 / Matching by-path link, or original
   * path.
   */
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
        return entry.path().string();  // 返回稳定链接 / Return the stable link.
      }
    }
    return tty_name;  // 保留原路径 / Keep the original path.
  }

  /**
   * @brief 查找匹配的 USB 串口设备 / Find a matching USB TTY device.
   * @return 找到匹配路径时返回 true / True when a matching path is found.
   */
  static bool FindUSBTTYByVidPid(const std::string& target_vid,
                                 const std::string& target_pid, std::string& tty_path)
  {
    return FindUSBTTYByVidPid(target_vid, target_pid, "", "", tty_path);
  }

  /**
   * @brief 查找匹配的 USB 串口设备 / Find a matching USB TTY device.
   * @return 找到匹配路径时返回 true / True when a matching path is found.
   */
  static bool FindUSBTTYByVidPid(const std::string& target_vid,
                                 const std::string& target_pid,
                                 const std::string& target_control_interface_name,
                                 std::string& tty_path)
  {
    return FindUSBTTYByVidPid(target_vid, target_pid, target_control_interface_name, "",
                              tty_path);
  }

  /**
   * @brief 查找匹配的 USB 串口设备 / Find a matching USB TTY device.
   * @return 找到匹配路径时返回 true / True when a matching path is found.
   */
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
          // Linux cdc_acm 的 tty 父节点对应 CDC 控制接口。
          // The parent of a Linux cdc_acm TTY is the CDC control interface.
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

  /**
   * @brief 尝试启用设备低延迟选项 / Try to enable the device low-latency option.
   * @param fd 已打开的设备描述符 / Open device descriptor.
   * @note 仅在线程处理配置时调用，不支持该选项的设备直接跳过。
   *       Called during owner-side configuration; skips unsupported devices.
   */
  void SetLowLatency(int fd)
  {
    struct serial_struct serinfo{};
    if (ioctl(fd, TIOCGSERIAL, &serinfo) != 0)
    {
      return;
    }
    serinfo.flags |= ASYNC_LOW_LATENCY;
    ioctl(fd, TIOCSSERIAL, &serinfo);
  }

  /**
   * @brief 提交串口配置请求 / Submit a UART configuration request.
   * @param config 目标配置 / Requested configuration.
   * @return 接纳返回 OK，已有待处理配置返回 BUSY，参数无效返回 ARG_ERR。
   *         OK on admission, BUSY for an occupied slot, ARG_ERR for invalid parameters.
   * @note OK 不表示已经生效。线程等待当前部分写入结束及内核输出队列排空后应用。
   *       断线时保留请求，重连时使用新配置；应用失败继续保留请求。
   *       OK does not mean applied. The thread finishes the partial front and drains
   *       kernel output before applying. Pending configuration survives disconnect or
   *       application failure and is used on reopen.
   */
  ErrorCode SetConfig(UART::Configuration config) override
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
  /// 检查支持的配置范围 / Validate the supported configuration range.
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

  /// 在线程中设置参数，仅打开设备时清空内核缓冲 / Apply settings; flush only on open.
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

  /// 唤醒串口线程处理已提交写入 / Wake the I/O thread for submitted writes.
  static void WriteFun(WritePort& port, bool)
  {
    auto* uart = LibXR::ContainerOf(&port, &LinuxUART::_write_port);
    uart->NotifyIoOwner();
  }

  static constexpr int RECONNECT_DELAY_MS = 1000;
  static constexpr int CONFIG_DRAIN_POLL_MS = 10;

  /// 创建唤醒描述符并等线程完成首次打开尝试 / Start the thread and await its first open
  /// attempt.
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

  /// 发布唤醒通知，已有未处理通知时允许合并 / Signal progress, coalescing pending wakes.
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

  /// 取走当前唤醒通知，随后重新检查工作状态 / Drain wakes before rechecking work.
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

  /// 非阻塞打开并配置设备，失败交给重连处理 / Open nonblocking; retry failures on
  /// reconnect.
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

  /// 关闭设备，按当前请求是否可继续发送决定失败通知 / Close and optionally fail the
  /// front.
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

  /// 按顺序发送最多两段数据，返回内核接收长度 / Write up to two spans and return accepted
  /// bytes.
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

  /// 只检查已发布请求，不把 Stream 未提交数据计入 / Check released requests only.
  bool HasPendingTx()
  {
    auto queue = _write_port.GetWriteQueue(false);
    return !queue.Empty();
  }

  /// 推进当前队头，保留短写和暂时无法发送的数据 / Progress the front; retain short-write
  /// suffixes.
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
      // 暂时无法发送时，保留此前部分发送的记录。
      // Preserve prior partial progress while waiting for writability.
      return true;
    }
    tx_front_unreplayable_ = accepted < offered;
    return accepted < offered || HasPendingTx();
  }

  /// 按接收队列空闲空间读取并发布数据 / Read within RX free space and publish bytes.
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

  /// 检查调用者是否已完整发布配置 / Check for a fully published configuration.
  bool ConfigRequested() const
  {
    return config_state_.load(std::memory_order_acquire) ==
           static_cast<uint8_t>(ConfigState::PUBLISHED);
  }

  /// 等当前部分写入和内核输出完成后应用配置 / Apply configuration at the TX drain
  /// boundary.
  void ServiceConfig(int& fd, Configuration& current_config)
  {
    if (!ConfigRequested() || tx_front_unreplayable_)
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

  /// 等待重连时限或新的工作通知 / Wait for the reconnect interval or new work.
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

  /// 串行处理设备访问、配置及重连 / Serialize device I/O, configuration, and reconnect.
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

      ServiceConfig(fd, current_config);
      if (fd < 0)
      {
        continue;
      }

      const bool tx_waiting =
          (!ConfigRequested() || tx_front_unreplayable_) && PumpTx(fd);
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
      if (_read_port.EmptySize() != 0U)
      {
        poll_fds[0].events |= POLLIN;
      }
      if (tx_waiting)
      {
        poll_fds[0].events |= POLLOUT;
      }
      // 无收发需求时只等唤醒，避免满接收队列遇到 HUP 后空转。
      // With no I/O demand, wait only for wakes; a full RX queue must not spin on HUP.
      if (poll_fds[0].events == 0)
      {
        poll_fds[0].fd = -1;
      }

      poll_fds[1].fd = wake_fd_;
      poll_fds[1].events = POLLIN;
      const int timeout = ConfigRequested() && !tx_waiting ? CONFIG_DRAIN_POLL_MS : -1;

      int poll_result = 0;
      do
      {
        poll_result = poll(poll_fds.data(), poll_fds.size(), timeout);
      } while (poll_result < 0 && errno == EINTR);

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

  /// 设备稳定路径 / Stable device path.
  std::string device_path_;
  /// 唯一设备访问线程 / Sole device I/O thread.
  Thread io_thread_;
  /// 接收临时缓冲 / RX staging buffer.
  uint8_t* rx_buff_ = nullptr;
  /// 接收缓冲容量 / RX buffer capacity.
  size_t buff_size_ = 0;
  /// 构造时的初始配置 / Initial construction configuration.
  Configuration config_{};
  /// 发布后由线程读取的配置 / Configuration published to the I/O thread.
  Configuration requested_config_{};
  /// 共用的 eventfd 唤醒描述符 / Shared eventfd wake descriptor.
  int wake_fd_ = -1;
  /// 首次打开尝试结束通知 / Initial open-attempt completion.
  Semaphore startup_sem_;
  /// 配置槽的发布状态 / Configuration slot publication state.
  std::atomic<uint8_t> config_state_{static_cast<uint8_t>(ConfigState::EMPTY)};
  /// 当前队头已有外部发送进展，仅 I/O 线程访问 / Owner-only partial TX progress.
  bool tx_front_unreplayable_ = false;

  Detail::LinuxUARTReadPort _read_port;  // NOLINT
  WritePort _write_port;                 // NOLINT
};

inline void Detail::LinuxUARTReadPort::OnReadQueueSpaceAvailable(bool)
{
  owner_.NotifyIoOwner();
}

}  // namespace LibXR
