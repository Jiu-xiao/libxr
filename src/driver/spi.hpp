#pragma once

#include "libxr.hpp"

namespace LibXR
{

/**
 * @class SPI
 * @brief 串行外设接口（SPI）抽象类。Abstract class for Serial Peripheral Interface (SPI).
 */
class SPI
{
 public:
  /**
   * @enum ClockPolarity
   * @brief 定义 SPI 时钟极性。Defines the SPI clock polarity.
   */
  enum class ClockPolarity : uint8_t
  {
    LOW = 0,  ///< 时钟空闲时为低电平。Clock idle low.
    HIGH = 1  ///< 时钟空闲时为高电平。Clock idle high.
  };

  /**
   * @enum ClockPhase
   * @brief 定义 SPI 时钟相位。Defines the SPI clock phase.
   */
  enum class ClockPhase : uint8_t
  {
    EDGE_1 = 0,  ///< 在第一个时钟边沿采样数据。Data sampled on the first clock edge.
    EDGE_2 = 1  ///< 在第二个时钟边沿采样数据。Data sampled on the second clock edge.
  };

  /**
   * @typedef OperationRW
   * @brief 定义读写操作类型的别名。Defines an alias for the read/write operation type.
   */
  using OperationRW = WriteOperation;

  /**
   * @struct Configuration
   * @brief 存储 SPI 配置参数的结构体。Structure for storing SPI configuration parameters.
   */
  struct Configuration
  {
    ClockPolarity clock_polarity;  ///< SPI 时钟极性。SPI clock polarity.
    ClockPhase clock_phase;        ///< SPI 时钟相位。SPI clock phase.
  };

  /**
   * @struct ReadWriteInfo
   * @brief 存储 SPI 读写操作信息的结构体。Structure for storing SPI read/write operation
   * information.
   */
  struct ReadWriteInfo
  {
    RawData read_data;  ///< 读取的数据缓冲区。Buffer for storing read data.
    ConstRawData write_data;  ///< 待写入的数据缓冲区。Buffer for data to be written.
    OperationRW op;           ///< 读写操作类型。Type of read/write operation.
  };

  /**
   * @brief 默认构造函数。Default constructor.
   */
  SPI() {}

  /**
   * @brief 进行 SPI 读写操作。Performs SPI read and write operations.
   * @param read_data 存储读取数据的缓冲区。Buffer to store the read data.
   * @param write_data 需要写入的数据缓冲区。Buffer containing the data to be written.
   * @param op 读写操作类型。Type of read/write operation.
   * @return 操作结果的错误码。Error code indicating the result of the operation.
   */
  virtual ErrorCode ReadAndWrite(RawData read_data, ConstRawData write_data,
                                 OperationRW &op) = 0;

  /**
   * @brief 进行 SPI 读取操作。Performs SPI read operation.
   * @param read_data 存储读取数据的缓冲区。Buffer to store the read data.
   * @param op 读写操作类型。Type of read/write operation.
   * @return 操作结果的错误码。Error code indicating the result of the operation.
   */
  virtual ErrorCode Read(RawData read_data, OperationRW &op)
  {
    return ReadAndWrite(read_data, ConstRawData(nullptr, 0), op);
  }

  /**
   * @brief 进行 SPI 写入操作。Performs SPI write operation.
   * @param write_data 需要写入的数据缓冲区。Buffer containing the data to be written.
   * @param op 读写操作类型。Type of read/write operation.
   * @return 操作结果的错误码。Error code indicating the result of the operation.
   */
  virtual ErrorCode Write(ConstRawData write_data, OperationRW &op)
  {
    return ReadAndWrite(RawData(nullptr, 0), write_data, op);
  }

  /**
   * @brief 设置 SPI 配置参数。Sets SPI configuration parameters.
   * @param config 需要应用的 SPI 配置。The SPI configuration to apply.
   * @return 操作结果的错误码。Error code indicating the result of the operation.
   */
  virtual ErrorCode SetConfig(Configuration config) = 0;

  /**
   * @brief 向 SPI 设备的寄存器写入数据。
   *        Writes data to a specific register of the SPI device.
   *
   * @param reg 寄存器地址。Register address.
   * @param write_data 写入的数据缓冲区。Buffer containing data to write.
   * @param op 操作类型（同步/异步）。Operation mode (sync/async).
   * @return 操作结果的错误码。Error code indicating success or failure.
   */
  virtual ErrorCode MemWrite(uint16_t reg, ConstRawData write_data, OperationRW &op) = 0;

  /**
   * @brief 从 SPI 设备的寄存器读取数据。
   *        Reads data from a specific register of the SPI device.
   *
   * @param reg 寄存器地址。Register address.
   * @param read_data 读取的数据缓冲区。Buffer to store read data.
   * @param op 操作类型（同步/异步）。Operation mode (sync/async).
   * @return 操作结果的错误码。Error code indicating success or failure.
   */
  virtual ErrorCode MemRead(uint16_t reg, RawData read_data, OperationRW &op) = 0;
};

}  // namespace LibXR
