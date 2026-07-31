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
    // TODO: Mark, Space
    Parity parity;      ///< 校验模式 / Parity mode
    uint8_t data_bits;  ///< 数据位长度 / Number of data bits
    // TODO: 0.5 1.5
    uint8_t stop_bits;  ///< 停止位长度 / Number of stop bits
  };

  ReadPort* read_port_;    ///< 读取端口 / Read port
  WritePort* write_port_;  ///< 写入端口 / Write port

  /**
   * @brief UART 构造函数 / UART constructor
   * @tparam ReadPortType 读取端口类型 / Read-port type
   * @tparam WritePortType 写入端口类型 / Write-port type
   * @param read_port 非 owning 读取端口；UART 可调用期间必须保持有效 / Non-owning read
   * port that must remain valid while the UART is callable
   * @param write_port 非 owning 写入端口；UART 可调用期间必须保持有效 / Non-owning write
   * port that must remain valid while the UART is callable
   *
   * 该构造函数只绑定读取和写入端口，不接管其所有权。 / This constructor only binds
   * the read and write ports and does not take ownership.
   */
  template <typename ReadPortType = ReadPort, typename WritePortType = WritePort>
  UART(ReadPortType* read_port, WritePortType* write_port)
      : read_port_(read_port), write_port_(write_port)
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
   *
   * @warning 每个 UART 实例最多接受一个尚未完成的配置。调用可以来自线程或 ISR；并发
   * 或重入请求返回 `ErrorCode::BUSY`，且不会覆盖已接受的 payload。`ErrorCode::OK`
   * 只确认请求已接纳，硬件静止、应用配置和重启可以稍后完成。 / One UART instance
   * accepts at most one outstanding configuration. Calls may originate in thread or ISR
   * context; a concurrent or reentrant request returns `ErrorCode::BUSY` without
   * replacing the accepted payload. `ErrorCode::OK` acknowledges admission, while
   * quiescence, apply, and restart may finish later.
   */
  virtual ErrorCode SetConfig(Configuration config) = 0;

  /**
   * @brief 提交一条 UART 写操作 / Submit one UART write operation
   * @tparam OperationType `WriteOperation` 的派生类型 / Type derived from
   * `WriteOperation`
   * @param data 待发送数据 / Data to transmit
   * @param op 完成方式和回调信息 / Completion mode and callback information
   * @param in_isr 是否从 ISR 上下文调用 / Whether called from ISR context
   * @return 写端口的提交结果 / Write-port submission result
   */
  template <typename OperationType, typename = std::enable_if_t<std::is_base_of_v<
                                        WriteOperation, std::decay_t<OperationType>>>>
  ErrorCode Write(ConstRawData data, OperationType&& op, bool in_isr = false)
  {
    return (*write_port_)(data, std::forward<OperationType>(op), in_isr);
  }

  /**
   * @brief 提交一条 UART 读操作 / Submit one UART read operation
   * @tparam OperationType `ReadOperation` 的派生类型 / Type derived from `ReadOperation`
   * @param data 接收目标缓冲区，必须保持有效直到操作完成 / Destination buffer that
   * must remain valid until completion
   * @param op 完成方式和回调信息 / Completion mode and callback information
   * @param in_isr 是否从 ISR 上下文调用 / Whether called from ISR context
   * @return 读端口的提交结果 / Read-port submission result
   */
  template <typename OperationType, typename = std::enable_if_t<std::is_base_of_v<
                                        ReadOperation, std::decay_t<OperationType>>>>
  ErrorCode Read(RawData data, OperationType&& op, bool in_isr = false)
  {
    return (*read_port_)(data, std::forward<OperationType>(op), in_isr);
  }
};

}  // namespace LibXR
