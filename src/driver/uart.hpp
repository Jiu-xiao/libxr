#pragma once

#include "libxr.hpp"
#include "libxr_rw.hpp"

namespace LibXR
{

/**
 * @class UART
 * @brief 通用异步收发传输（UART）基类 / Abstract base class for Universal Asynchronous
 * Receiver-Transmitter (UART)
 *
 * 该类定义了 UART 设备的基本接口，包括配置和数据传输端口。
 * This class defines the basic interface for a UART device, including configuration and
 * data transmission ports.
 */
class UART
{
 public:
  /**
   * @enum Parity
   * @brief 奇偶校验模式 / Parity mode
   *
   * 指定 UART 传输时的奇偶校验模式。
   * Specifies the parity mode used in UART transmission.
   */
  enum class Parity : uint8_t
  {
    NO_PARITY = 0,  ///< 无校验 / No parity
    EVEN = 1,       ///< 偶校验 / Even parity
    ODD = 2         ///< 奇校验 / Odd parity
  };

  /**
   * @struct Configuration
   * @brief UART 配置结构体 / UART configuration structure
   *
   * 该结构体包含 UART 端口的基本配置参数，如波特率、数据位、停止位等。
   * This structure contains basic configuration parameters for the UART port, such as
   * baud rate, data bits, and stop bits.
   */
  struct Configuration
  {
    uint32_t baudrate;  ///< 波特率 / Baud rate
    Parity parity;      ///< 校验模式 / Parity mode
    uint8_t data_bits;  ///< 数据位长度 / Number of data bits
    uint8_t stop_bits;  ///< 停止位长度 / Number of stop bits
  };

  ReadPort read_port_;    ///< 读取端口 / Read port
  WritePort write_port_;  ///< 写入端口 / Write port

  /**
   * @brief UART 构造函数 / UART constructor
   * @param rx_queue_size 接收队列大小 / Receive queue size
   * @param rx_buffer_size 接收缓冲区大小 / Receive buffer size
   * @param tx_queue_size 发送队列大小 / Transmit queue size
   * @param tx_buffer_size 发送缓冲区大小 / Transmit buffer size
   *
   * 该构造函数初始化 UART 的读取和写入端口。
   * This constructor initializes the read and write ports of the UART.
   */
  UART(size_t rx_queue_size, size_t rx_buffer_size, size_t tx_queue_size,
       size_t tx_buffer_size)
      : read_port_(rx_queue_size, rx_buffer_size),
        write_port_(tx_queue_size, tx_buffer_size)
  {
  }

  /**
   * @brief 设置 UART 配置 / Sets the UART configuration
   * @param config UART 配置信息 / UART configuration settings
   * @return 返回操作状态，成功时返回 `ErrorCode::OK`，否则返回相应错误码 / Returns the
   * operation status, `ErrorCode::OK` if successful, otherwise an error code
   *
   * 该方法为纯虚函数，子类必须实现具体的 UART 配置逻辑。
   * This is a pure virtual function. Subclasses must implement the specific UART
   * configuration logic.
   */
  virtual ErrorCode SetConfig(Configuration config) = 0;
};

}  // namespace LibXR
