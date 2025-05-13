#pragma once

#define termios asmtermios
#include <asm/termbits.h>
#undef termios
#include <fcntl.h>
#include <linux/serial.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>

#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "semaphore.hpp"
#include "uart.hpp"

namespace LibXR
{

class LinuxUART : public UART
{
 public:
  LinuxUART(const char *dev_path, unsigned int baudrate = 115200,
            Parity parity = Parity::NO_PARITY, uint8_t data_bits = 8,
            uint8_t stop_bits = 1, uint32_t rx_queue_size = 5, uint32_t tx_queue_size = 5,
            size_t buffer_size = 512)
      : UART(rx_queue_size, buffer_size, tx_queue_size, buffer_size),
        write_sem_(0),
        rx_buff_(new uint8_t[buffer_size]),
        tx_buff_(new uint8_t[buffer_size]),
        buff_size_(buffer_size)
  {
    if (std::filesystem::exists(dev_path) == false)
    {
      XR_LOG_ERROR("Cannot find UART device: %s", dev_path);
      ASSERT(false);
    }

    device_path_ = GetByPathForTTY(dev_path);

    fd_ = open(device_path_.c_str(), O_RDWR | O_NOCTTY);
    if (fd_ < 0)
    {
      XR_LOG_ERROR("Cannot open UART device: %s", device_path_.c_str());
      ASSERT(false);
    }
    else
    {
      XR_LOG_PASS("Open UART device: %s", device_path_.c_str());
    }

    config_ = {.baudrate = baudrate,
               .parity = parity,
               .data_bits = data_bits,
               .stop_bits = stop_bits};

    SetConfig(config_);

    read_port_ = ReadFun;
    write_port_ = WriteFun;

    rx_thread_.Create<LinuxUART *>(
        this, [](LinuxUART *self) { self->RxLoop(); }, "rx_uart", 8192,
        Thread::Priority::REALTIME);

    tx_thread_.Create<LinuxUART *>(
        this, [](LinuxUART *self) { self->TxLoop(); }, "tx_uart", 8192,
        Thread::Priority::REALTIME);
  }

  std::string GetByPathForTTY(const std::string &tty_name)
  {
    const std::string BASE = "/dev/serial/by-path";
    if (strncmp(tty_name.c_str(), BASE.c_str(), BASE.length()) == 0 ||
        !std::filesystem::exists(BASE))
    {
      return tty_name;
    }
    for (const auto &entry : std::filesystem::directory_iterator(BASE))
    {
      std::string full = std::filesystem::canonical(entry.path());
      if (full == tty_name)
      {
        return entry.path();  // 返回符号链接路径
      }
    }
    return "";  // 没找到
  }

  void SetLowLatency(int fd)
  {
    struct serial_struct serinfo;
    ioctl(fd, TIOCGSERIAL, &serinfo);
    serinfo.flags |= ASYNC_LOW_LATENCY;
    ioctl(fd, TIOCSSERIAL, &serinfo);
  }

  ErrorCode SetConfig(UART::Configuration config) override
  {
    if (&config != &config_)
    {
      config_ = config;
    }

    struct termios2 tio{};
    if (ioctl(fd_, TCGETS2, &tio) != 0)
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
    }

    // 禁用硬件流控
    tio.c_cflag &= ~CRTSCTS;

    // 启用本地模式、读功能
    tio.c_cflag |= (CLOCAL | CREAD);

    // 控制字符配置：阻塞直到读到 1 字节
    // for (int i = 0; i < NCCS; ++i) tio.c_cc[i] = 0;
    tio.c_cc[VTIME] = 0;
    tio.c_cc[VMIN] = 1;

    if (ioctl(fd_, TCSETS2, &tio) != 0)
    {
      return ErrorCode::INIT_ERR;
    }

    SetLowLatency(fd_);

    tcflush(fd_, TCIOFLUSH);

    return ErrorCode::OK;
  }

  static ErrorCode ReadFun(ReadPort &port)
  {
    auto uart = CONTAINER_OF(&port, LinuxUART, read_port_);
    Mutex::LockGuard guard(uart->read_mutex_);
    port.ProcessPendingReads();
    return ErrorCode::OK;
  }

  static ErrorCode WriteFun(WritePort &port)
  {
    auto uart = CONTAINER_OF(&port, LinuxUART, write_port_);
    WritePort::WriteInfo info;
    if (port.queue_info_->Peek(info) != ErrorCode::OK)
    {
      return ErrorCode::EMPTY;
    }
    port.UpdateStatus(info.op);
    uart->write_sem_.Post();
    return ErrorCode::OK;
  }

 private:
  void RxLoop()
  {
    while (true)
    {
      if (!connected_)
      {
        close(fd_);
        fd_ = open(device_path_.c_str(), O_RDWR | O_NOCTTY);

        if (fd_ < 0)
        {
          XR_LOG_WARN("Cannot open UART device: %s", device_path_.c_str());
          Thread::Sleep(1000);
        }
        else
        {
          SetConfig(config_);
          XR_LOG_PASS("Reopen UART device: %s", device_path_.c_str());
          connected_ = true;
        }
      }
      auto n = read(fd_, rx_buff_, buff_size_);
      if (n > 0)
      {
        read_port_.queue_data_->PushBatch(rx_buff_, n);
        Mutex::LockGuard guard(read_mutex_);
        read_port_.ProcessPendingReads();
      }
      else
      {
        XR_LOG_WARN("Cannot read UART device: %s", device_path_.c_str());
        connected_ = false;
      }
    }
  }

  void TxLoop()
  {
    WritePort::WriteInfo info;
    while (true)
    {
      if (!connected_)
      {
        Thread::Sleep(1);
        continue;
      }

      if (write_sem_.Wait() != ErrorCode::OK)
      {
        continue;
      }
      
      if (write_port_.queue_info_->Pop(info) == ErrorCode::OK)
      {
        if (write_port_.queue_data_->PopBatch(tx_buff_, info.size) == ErrorCode::OK)
        {
          auto written = write(fd_, tx_buff_, info.size);
          if (written < 0)
          {
            XR_LOG_WARN("Cannot write UART device: %s", device_path_.c_str());
            connected_ = false;
          }
          info.op.UpdateStatus(false, (written == static_cast<int>(info.size))
                                          ? ErrorCode::OK
                                          : ErrorCode::FAILED);
        }
        else
        {
          ASSERT(false);
          info.op.UpdateStatus(false, ErrorCode::FAILED);
        }
      }
    }
  }

  int fd_ = -1;
  bool connected_ = true;
  Configuration config_;
  std::string device_path_;
  Thread rx_thread_;
  Thread tx_thread_;
  uint8_t *rx_buff_ = nullptr;
  uint8_t *tx_buff_ = nullptr;
  size_t buff_size_ = 0;
  Semaphore write_sem_;
  Mutex read_mutex_;
};

}  // namespace LibXR
