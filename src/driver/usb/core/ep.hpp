#pragma once

#include <cstdint>

#include "double_buffer.hpp"
#include "libxr_cb.hpp"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "libxr_type.hpp"

namespace LibXR::USB
{

/**
 * @class Endpoint
 * @brief USB 端点基类 / USB Endpoint base class
 *
 * 用于描述与操作 USB 端点，提供端点配置、传输控制、状态管理与回调接口。
 * Used for describing and manipulating USB endpoints, providing endpoint configuration,
 * transfer control, state management, and callback interfaces.
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
    OUT = 0,  ///< 输出方向 / OUT direction
    IN = 1,   ///< 输入方向 / IN direction
    BOTH = 2  ///< 双向（可配置为 IN/OUT） / Both (configurable as IN/OUT)
  };

  /**
   * @brief 端点号
   *        Endpoint number
   */
  enum class EPNumber : uint8_t
  {
    EP0 = 0,           ///< 端点 0 / Endpoint 0
    EP1 = 1,           ///< 端点 1 / Endpoint 1
    EP2 = 2,           ///< 端点 2 / Endpoint 2
    EP3 = 3,           ///< 端点 3 / Endpoint 3
    EP4 = 4,           ///< 端点 4 / Endpoint 4
    EP5 = 5,           ///< 端点 5 / Endpoint 5
    EP6 = 6,           ///< 端点 6 / Endpoint 6
    EP7 = 7,           ///< 端点 7 / Endpoint 7
    EP8 = 8,           ///< 端点 8 / Endpoint 8
    EP9 = 9,           ///< 端点 9 / Endpoint 9
    EP10 = 10,         ///< 端点 10 / Endpoint 10
    EP11 = 11,         ///< 端点 11 / Endpoint 11
    EP12 = 12,         ///< 端点 12 / Endpoint 12
    EP13 = 13,         ///< 端点 13 / Endpoint 13
    EP14 = 14,         ///< 端点 14 / Endpoint 14
    EP15 = 15,         ///< 端点 15 / Endpoint 15
    EP_MAX_NUM = 16,   ///< 端点数量上限 / Maximum number of endpoints
    EP_AUTO = 0xFE,    ///< 自动分配端点号 / Auto allocate
    EP_INVALID = 0xFF  ///< 非法端点号 / Invalid endpoint
  };

  /**
   * @brief 端点类型
   *        Endpoint type
   */
  enum class Type : uint8_t
  {
    CONTROL = 0,      ///< 控制端点 / Control
    ISOCHRONOUS = 1,  ///< 等时端点 / Isochronous
    BULK = 2,         ///< 批量端点 / Bulk
    INTERRUPT = 3     ///< 中断端点 / Interrupt
  };

  /**
   * @brief 端点状态
   *        Endpoint state
   */
  enum class State : uint8_t
  {
    DISABLED,  ///< 禁用 / Disabled
    IDLE,      ///< 空闲 / Idle
    BUSY,      ///< 忙 / Busy
    STALLED,   ///< 停止/挂起 / Stalled
    ERROR      ///< 错误 / Error
  };

  /**
   * @brief 端点号转换为 uint8_t / Convert endpoint number to uint8_t
   * @param ep 端点号 / Endpoint number
   * @return uint8_t 端点号数值 / Endpoint number value
   */
  static constexpr uint8_t EPNumberToInt8(EPNumber ep)
  {
    return static_cast<uint8_t>(ep);
  }

  /**
   * @brief 端点号转换为端点地址 / Convert endpoint number to endpoint address
   * @param ep 端点号 / Endpoint number
   * @param dir 端点方向 / Endpoint direction
   * @return uint8_t 端点地址 / Endpoint address
   */
  static constexpr uint8_t EPNumberToAddr(EPNumber ep, Direction dir)
  {
    ASSERT(dir == Direction::IN || dir == Direction::OUT);
    return static_cast<uint8_t>(ep) | (dir == Direction::IN ? 0x80 : 0x00);
  }

  /**
   * @brief 端点地址转换为端点号 / Convert endpoint address to endpoint number
   * @param addr 端点地址 / Endpoint address
   * @param dir 输出方向（输出参数） / Output direction (output)
   * @return EPNumber 端点号 / Endpoint number
   */
  static constexpr EPNumber AddrToEPNumber(uint8_t addr, Direction& dir)
  {
    dir = addr & 0x80 ? Direction::IN : Direction::OUT;
    return static_cast<EPNumber>(addr & 0x7F);
  }

  /**
   * @brief 获取下一个端点号 / Get the next endpoint number
   * @param ep 当前端点号 / Current endpoint number
   * @return EPNumber 下一个端点号 / Next endpoint number
   */
  static constexpr EPNumber NextEPNumber(EPNumber ep)
  {
    ASSERT(ep <= EPNumber::EP15);
    return static_cast<EPNumber>(EPNumberToInt8(ep) + 1);
  }

  /**
   * @struct Config
   * @brief 端点配置参数 / Endpoint configuration parameters
   */
  struct Config
  {
    Direction direction = Direction::OUT;   ///< 端点方向 / Endpoint direction
    Type type = Type::BULK;                 ///< 端点类型 / Endpoint type
    uint16_t max_packet_size = UINT16_MAX;  ///< 最大包长 / Max packet size
    bool double_buffer = false;             ///< 是否启用双缓冲 / Enable double buffer
    uint8_t mult = 0;  ///< 多包倍数（高带宽端点） / Multiplier (high-bandwidth)
  };

  /**
   * @brief 构造函数 / Constructor
   * @param number 端点号 / Endpoint number
   * @param dir 允许配置的方向 / Allowed direction
   * @param buffer 端点缓冲区 / Endpoint buffer
   */
  explicit Endpoint(EPNumber number, Direction dir, RawData buffer)
      : number_(number), avail_direction_(dir), buffer_(buffer), double_buffer_(buffer)
  {
  }

  /**
   * @brief 虚析构函数 / Virtual destructor
   */
  virtual ~Endpoint() = default;

  Endpoint(const Endpoint&) = delete;
  Endpoint& operator=(const Endpoint&) = delete;

  /**
   * @brief 获取端点号 / Get endpoint number
   * @return EPNumber 端点号 / Endpoint number
   */
  EPNumber GetNumber() const { return number_; }

  /**
   * @brief 获取允许配置的方向 / Get allowed endpoint direction
   * @return Direction 允许方向 / Allowed direction
   */
  Direction AvailableDirection() const { return avail_direction_; }

  /**
   * @brief 获取当前端点方向 / Get current endpoint direction
   * @return Direction 当前方向 / Current direction
   */
  Direction GetDirection() const
  {
    if (state_ == State::DISABLED)
    {
      return AvailableDirection();
    }
    return config_.direction;
  }

  /**
   * @brief 获取端点地址（方向 + 号） / Get endpoint address (dir + num)
   * @return uint8_t 端点地址 / Endpoint address
   */
  uint8_t GetAddress() const
  {
    if (state_ == State::DISABLED)
    {
      return EPNumberToInt8(number_) & 0x0F;
    }
    return EPNumberToAddr(number_, config_.direction);
  }

  /**
   * @brief 获取端点状态 / Get endpoint state
   * @return State 当前状态 / Current state
   */
  State GetState() const { return state_; }

  /**
   * @brief 设置端点状态 / Set endpoint state
   * @param state 状态 / State
   */
  void SetState(State state) { state_ = state; }

  /**
   * @brief 获取端点类型 / Get endpoint type
   * @return Type 端点类型 / Endpoint type
   */
  Type GetType() const { return config_.type; }

  /**
   * @brief 获取最大包长 / Get max packet size
   * @return uint16_t 最大包长 / Max packet size
   */
  uint16_t MaxPacketSize() const { return config_.max_packet_size; }

  /**
   * @brief 是否处于 STALL 状态 / Whether endpoint is stalled
   * @return true 已 STALL / Stalled
   * @return false 非 STALL / Not stalled
   */
  bool IsStalled() const { return state_ == State::STALLED; }

  /**
   * @brief 是否启用双缓冲 / Whether double buffer is enabled
   * @return true 启用 / Enabled
   * @return false 未启用 / Disabled
   */
  bool UseDoubleBuffer() const { return config_.double_buffer; }

  /**
   * @brief 获取当前可用于传输的缓冲区 / Get current transfer buffer
   * @return RawData 缓冲区视图 / Buffer view
   */
  RawData GetBuffer() const
  {
    if (config_.double_buffer)
    {
      return {double_buffer_.ActiveBuffer(), double_buffer_.Size()};
    }
    else
    {
      return buffer_;
    }
  }

  /**
   * @brief 设置传输完成回调 / Set transfer complete callback
   * @param cb 回调函数 / Callback
   */
  void SetOnTransferCompleteCallback(Callback<ConstRawData&> cb)
  {
    on_transfer_complete_ = cb;
  }

  /**
   * @brief 设置当前活动缓冲区有效长度 / Set active buffer valid length
   * @param len 有效长度 / Valid length
   */
  void SetActiveLength(uint16_t len) { double_buffer_.SetActiveLength(len); }

  /**
   * @brief 获取当前活动缓冲区有效长度 / Get active buffer valid length
   * @return size_t 有效长度 / Valid length
   */
  size_t GetActiveLength() { return double_buffer_.GetActiveLength(); }

  /**
   * @brief 返回当前最大可传输字节数 / Return maximum transferable size at this time
   * @return size_t 最大可传输字节数 / Maximum transferable bytes
   */
  virtual size_t MaxTransferSize() const { return MaxPacketSize(); }

  /**
   * @brief 配置端点协议参数 / Configure endpoint protocol parameters
   * @param cfg 配置参数 / Configuration parameters
   */
  virtual void Configure(const Config& cfg) = 0;

  /**
   * @brief 关闭端点 / Close endpoint
   */
  virtual void Close() = 0;

  /**
   * @brief 置 STALL / Stall endpoint
   * @return ErrorCode 错误码 / Error code
   */
  virtual ErrorCode Stall() = 0;

  /**
   * @brief 清除 STALL / Clear stall
   * @return ErrorCode 错误码 / Error code
   */
  virtual ErrorCode ClearStall() = 0;

  /**
   * @brief 启动一次传输 / Start a transfer
   * @param size 传输长度 / Transfer size
   * @return ErrorCode 错误码 / Error code
   */
  virtual ErrorCode Transfer(size_t size) = 0;

  /**
   * @brief Bulk 多包传输辅助接口 / Helper for multi-packet bulk transfer
   * @param data 应用层缓冲区（IN：待发送数据；OUT：接收缓冲区） / App buffer (IN: data to
   * send; OUT: receive buffer)
   * @return ErrorCode 错误码 / Error code
   */
  virtual ErrorCode TransferMultiBulk(RawData& data)
  {
    auto ep_buf = GetBuffer();
    size_t max_chunk = MaxTransferSize();

    ASSERT(max_chunk > 0);

    // 单包就能搞定的情况：不进入 multi-bulk 状态机，保持原有行为
    if (data.size_ <= max_chunk)
    {
      if (GetDirection() == Direction::IN)
      {
        multi_bulk_ = false;
        multi_bulk_remain_ = 0;
        multi_bulk_data_ = {nullptr, 0};

        auto src = static_cast<const uint8_t*>(data.addr_);
        auto dst = static_cast<uint8_t*>(ep_buf.addr_);
        Memory::FastCopy(dst, src, data.size_);

        return Transfer(data.size_);
      }

      // OUT：接收——为了把数据回填到 data，单包也走 multi-bulk
      if (GetDirection() == Direction::OUT)
      {
        multi_bulk_ = true;
        multi_bulk_data_ = data;
        multi_bulk_remain_ = data.size_;  // OUT: 剩余可写容量
        return Transfer(data.size_);
      }

      return ErrorCode::ARG_ERR;
    }

    // 需要多包处理的情况
    multi_bulk_ = true;
    multi_bulk_data_ = data;          // 应用层 buffer 指针 + 最大容量 / 逻辑总长
    multi_bulk_remain_ = data.size_;  // IN: 剩余待发送；OUT: 剩余可写容量

    // 第一包大小
    size_t first = max_chunk;
    if (first > multi_bulk_remain_)
    {
      first = multi_bulk_remain_;
    }

    if (GetDirection() == Direction::IN)
    {
      // IN：发送时，先把第一 chunk 拷到 EP buffer
      auto src = static_cast<const uint8_t*>(multi_bulk_data_.addr_);
      auto dst = static_cast<uint8_t*>(ep_buf.addr_);
      Memory::FastCopy(dst, src, first);

      multi_bulk_remain_ -= first;  // 已经准备好 first 字节要发
    }
    // OUT：接收时，先启动一次接收，稍后在回调里拷贝到 multi_bulk_data_

    return Transfer(first);
  }

  /**
   * @brief 发送/接收 ZLP（零长度包） / Transfer zero length packet (ZLP)
   * @return ErrorCode 错误码 / Error code
   */
  virtual ErrorCode TransferZLP() { return Transfer(0); }

  /**
   * @brief 由底层在传输完成时调用 / Called by low-level driver when transfer completes
   * @param in_isr 是否在中断上下文 / Whether in ISR context
   * @param actual_transfer_size 实际传输长度 / Actual transferred size
   */
  void OnTransferCompleteCallback(bool in_isr, size_t actual_transfer_size)
  {
    if (GetState() != State::BUSY)
    {
      return;
    }

    bool callback_uses_app_buffer = false;
    bool out_switched_before_cb = false;

    const Direction DIR = GetDirection();
    const size_t MAX_CHUNK = MaxTransferSize();
    const bool DB = UseDoubleBuffer();

    if (multi_bulk_)
    {
      if (DIR == Direction::IN)
      {
        if (multi_bulk_remain_ > 0)
        {
          auto ep_buf = GetBuffer();  // 此时应是“下一块 Active”（因为 Transfer 已切换）
          const size_t SENT = multi_bulk_data_.size_ - multi_bulk_remain_;

          size_t chunk = MAX_CHUNK;
          if (chunk > multi_bulk_remain_) chunk = multi_bulk_remain_;

          auto src = static_cast<const uint8_t*>(multi_bulk_data_.addr_) + SENT;
          auto dst = static_cast<uint8_t*>(ep_buf.addr_);
          Memory::FastCopy(dst, src, chunk);
          multi_bulk_remain_ -= chunk;

          SetState(State::IDLE);
          (void)Transfer(chunk);
          return;
        }

        // 结束：对上层报告 app buffer
        multi_bulk_ = false;
        callback_uses_app_buffer = true;
        actual_transfer_size = multi_bulk_data_.size_;
      }
      else
      {
        // OUT：完成时 Active 里是数据
        auto ep_buf = GetBuffer();
        size_t prev_remain = multi_bulk_remain_;
        size_t recvd = actual_transfer_size;
        if (recvd > prev_remain)
        {
          recvd = prev_remain;
        }

        const size_t OFFSET = multi_bulk_data_.size_ - prev_remain;

        auto dst = static_cast<uint8_t*>(multi_bulk_data_.addr_) + OFFSET;
        auto src = static_cast<const uint8_t*>(ep_buf.addr_);
        Memory::FastCopy(dst, src, recvd);

        multi_bulk_remain_ = prev_remain - recvd;

        const bool SHORT_PACKET = (recvd < MAX_CHUNK);
        const bool BUFFER_FULL = (multi_bulk_remain_ == 0);

        if (DB)
        {
          SwitchBuffer();  // 切换后 Pending = 刚接收的包；Active = 下次用
          out_switched_before_cb = true;
        }

        if (!SHORT_PACKET && !BUFFER_FULL)
        {
          size_t chunk = MAX_CHUNK;
          if (chunk > multi_bulk_remain_)
          {
            chunk = multi_bulk_remain_;
          }

          SetState(State::IDLE);
          (void)Transfer(chunk);  // 下一包将落到新的 Active（
          return;
        }

        multi_bulk_ = false;
        callback_uses_app_buffer = true;
        actual_transfer_size = multi_bulk_data_.size_ - multi_bulk_remain_;
      }
    }

    // 非 multi-bulk：OUT 也必须“回调前切换”
    if (!multi_bulk_ && DB && DIR == Direction::OUT && !out_switched_before_cb)
    {
      SwitchBuffer();
      out_switched_before_cb = true;
    }

    SetState(State::IDLE);

    ConstRawData data;
    if (callback_uses_app_buffer)
    {
      data = ConstRawData(multi_bulk_data_.addr_, actual_transfer_size);
    }
    else
    {
      if (DB)
      {
        // 回调里永远取 Pending = 刚刚完成的那包（IN/OUT 一致）
        data = ConstRawData(double_buffer_.PendingBuffer(), actual_transfer_size);
      }
      else
      {
        data = ConstRawData(buffer_.addr_, actual_transfer_size);
      }
    }

    on_transfer_complete_.Run(in_isr, data);
  }

 protected:
  /**
   * @brief 获取当前配置引用 / Get endpoint config reference
   * @return Config& 配置引用 / Configuration reference
   */
  Config& GetConfig() { return config_; }

  /**
   * @brief 切换双缓冲 / Switch double buffer
   */
  virtual void SwitchBuffer()
  {
    double_buffer_.EnablePending();
    double_buffer_.Switch();
  }

  /**
   * @brief 设置当前活动缓冲块 / Set active buffer block
   * @param active_block true 使用第二块；false 使用第一块 / true selects second block;
   * false selects first block
   */
  virtual void SetActiveBlock(bool active_block)
  {
    double_buffer_.SetActiveBlock(active_block);
    double_buffer_.EnablePending();
  }

 private:
  LibXR::Callback<LibXR::ConstRawData&>
      on_transfer_complete_;  ///< 传输完成回调 / Transfer complete callback

  EPNumber number_;                    ///< 端点号 / Endpoint number
  Direction avail_direction_;          ///< 可配置方向 / Allowed direction
  Config config_;                      ///< 当前配置 / Current configuration
  State state_ = State::DISABLED;      ///< 当前状态 / Current state
  LibXR::RawData buffer_;              ///< 端点缓冲区 / Endpoint buffer
  LibXR::DoubleBuffer double_buffer_;  ///< 双缓冲管理 / Double buffer manager

  bool multi_bulk_ = false;  ///< 多包 bulk 状态机使能 / Multi-bulk state enabled
  RawData multi_bulk_data_;  ///< 多包 bulk 应用层 buffer / App buffer for multi-bulk
  size_t multi_bulk_remain_ =
      0;  ///< 多包 bulk 剩余字节数 / Remaining bytes for multi-bulk
};

}  // namespace LibXR::USB
