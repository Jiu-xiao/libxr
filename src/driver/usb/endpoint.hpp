#pragma once

#include <cstdint>

#include "libxr_cb.hpp"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "libxr_type.hpp"

namespace LibXR::USB
{

/**
 * @brief USB端点类
 *        USB endpoint class
 */
class Endpoint
{
 public:
  /**
   * @brief 端点方向
   *        Endpoint direction
   */
  enum class Direction : uint8_t
  {
    OUT = 0,
    IN = 1,
    BOTH = 2
  };

  /**
   * @brief 端点类型
   *        Endpoint type
   */
  enum class Type : uint8_t
  {
    CONTROL = 0,
    ISOCHRONOUS = 1,
    BULK = 2,
    INTERRUPT = 3
  };

  /**
   * @brief 端点状态
   *        Endpoint state
   */
  enum class State : uint8_t
  {
    DISABLED,
    IDLE,
    BUSY,
    STALLED,
    ERROR
  };

  /**
   * @brief 端点配置结构体
   *        Endpoint configuration struct
   */
  struct Config
  {
    Direction direction = Direction::OUT;
    Type type = Type::BULK;
    uint16_t max_packet_size = 64;
    uint8_t interval = 0;
    uint8_t mult = 1;
    bool double_buffer = false;
  };

  /**
   * @brief 构造函数，仅分配编号和状态，不配置协议参数
   *        Construct, only assign ep number/resource, not configure protocol
   */
  explicit Endpoint(uint8_t number, Direction dir) : number_(number), direction_(dir) {}

  /**
   * @brief 析构函数
   *        Destructor
   */
  virtual ~Endpoint() = default;

  // 禁止拷贝
  Endpoint(const Endpoint&) = delete;
  Endpoint& operator=(const Endpoint&) = delete;

  /**
   * @brief 二次初始化/配置端点协议参数（由Pool/Manager分配后调用）
   *        Configure endpoint protocol parameters (call after pool allocation)
   */
  virtual bool Configure(const Config& cfg) = 0;

  /**
   * @brief 关闭端点（软禁用/资源复位）
   *        Close (soft disable)
   */
  virtual void Close() = 0;

  /**
   * @brief 获取端点号
   *        Get endpoint number
   */
  uint8_t Number() const { return number_; }

  Direction AvailableDirection() const { return direction_; }

  Direction GetDirection() const
  {
    if (state_ == State::DISABLED)
    {
      return AvailableDirection();
    }
    return config_.direction;
  }

  /**
   * @brief 获取端点地址（方向 + 号）
   *        Get endpoint address (dir + num)
   */
  uint8_t Address() const
  {
    if (state_ == State::DISABLED)
    {
      return number_ & 0x0F;
    }
    return (number_ & 0x0F) | (config_.direction == Direction::IN ? 0x80 : 0x00);
  }

  /**
   * @brief 获取端点状态
   *        Get endpoint state
   */
  State GetState() const { return state_; }

  /**
   * @brief 获取端点类型
   *        Get endpoint type
   */
  Type GetType() const { return config_.type; }

  /**
   * @brief 获取最大包长
   *        Get max packet size
   */
  uint16_t MaxPacketSize() const { return config_.max_packet_size; }

  virtual size_t MaxTransferSize() const
  {
    // 默认=MaxPacketSize，无FIFO优化的平台
    // 可重载，返回当前FIFO剩余可用空间等
    return MaxPacketSize();
  }

  /**
   * @brief 获取当前配置
   *        Get endpoint config
   */
  const Config& GetConfig() const { return config_; }

  /**
   * @brief 写入数据到端点（IN端点）
   *        Write data to endpoint (for IN endpoint)
   *
   * @param data 要写入的数据
   *             Data to write
   * @return ErrorCode 写入结果
   *                   Write result
   */
  ErrorCode virtual Write(LibXR::ConstRawData& data) = 0;

  /**
   * @brief 从端点读取数据（OUT端点）
   *        Read data from endpoint (for OUT endpoint)
   *
   * @param data 接收数据的缓冲区
   *             Buffer to receive data
   * @return ErrorCode 读取结果
   *                   Read result
   */
  ErrorCode virtual Read(LibXR::RawData& data) = 0;

  ErrorCode virtual Stall() = 0;

  ErrorCode virtual ClearStall() = 0;

  bool IsStalled() const { return state_ == State::STALLED; }

  // ================== 回调接口 ================== //

  LibXR::Callback<LibXR::ConstRawData&> on_transfer_complete_;
  LibXR::Callback<LibXR::ErrorCode> on_error_;

  uint8_t number_;
  Direction direction_;  ///< 端点允许配置的方向
  Config config_;
  State state_ = State::DISABLED;
};

}  // namespace LibXR::USB
