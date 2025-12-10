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
 * @brief USB端点基类 / USB Endpoint base class
 *
 * 用于描述和操作USB端点，包括配置、数据读写、状态管理等接口。
 * Used for describing and manipulating USB endpoints, including configuration,
 * data read/write, state management, etc.
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
    BOTH = 2  ///< 双向(可配置成IN/OUT) / Both (can be configured as IN/OUT)
  };

  /**
   * @brief 端点号 / Endpoint number
   */
  enum class EPNumber : uint8_t
  {
    EP0 = 0,           ///< 端点0 / Endpoint 0
    EP1 = 1,           ///< 端点1 / Endpoint 1
    EP2 = 2,           ///< 端点2 / Endpoint 2
    EP3 = 3,           ///< 端点3 / Endpoint 3
    EP4 = 4,           ///< 端点4 / Endpoint 4
    EP5 = 5,           ///< 端点5 / Endpoint 5
    EP6 = 6,           ///< 端点6 / Endpoint 6
    EP7 = 7,           ///< 端点7 / Endpoint 7
    EP8 = 8,           ///< 端点8 / Endpoint 8
    EP9 = 9,           ///< 端点9 / Endpoint 9
    EP10 = 10,         ///< 端点10 / Endpoint 10
    EP11 = 11,         ///< 端点11 / Endpoint 11
    EP12 = 12,         ///< 端点12 / Endpoint 12
    EP13 = 13,         ///< 端点13 / Endpoint 13
    EP14 = 14,         ///< 端点14 / Endpoint 14
    EP15 = 15,         ///< 端点15 / Endpoint 15
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
    STALLED,   ///< 挂起 / Stalled
    ERROR      ///< 错误 / Error
  };

  /**
   * @brief 端点号转换为uint8_t / Convert endpoint number to uint8_t
   *
   * @param ep 端点号 / Endpoint number
   * @return constexpr uint8_t
   */
  static constexpr uint8_t EPNumberToInt8(EPNumber ep)
  {
    return static_cast<uint8_t>(ep);
  }

  /**
   * @brief 端点号转换为端点地址 / Convert endpoint number to endpoint address
   *
   * @param ep 端点号 / Endpoint number
   * @param dir 端点方向 / Endpoint direction
   * @return constexpr uint8_t
   */
  static constexpr uint8_t EPNumberToAddr(EPNumber ep, Direction dir)
  {
    ASSERT(dir == Direction::IN || dir == Direction::OUT);
    return static_cast<uint8_t>(ep) | (dir == Direction::IN ? 0x80 : 0x00);
  }

  /**
   * @brief 端点地址转换为端点号 / Convert endpoint address to endpoint number
   *
   * @param addr 端点地址 / Endpoint address
   * @param dir 返回的端点方向 / Return endpoint direction
   * @return constexpr EPNumber
   */
  static constexpr EPNumber AddrToEPNumber(uint8_t addr, Direction& dir)
  {
    dir = addr & 0x80 ? Direction::IN : Direction::OUT;
    return static_cast<EPNumber>(addr & 0x7F);
  }

  /**
   * @brief 获取下一个端点号 / Get the next endpoint number
   *
   * @param ep 当前端点号 / Current endpoint number
   * @return constexpr EPNumber
   */
  static constexpr EPNumber NextEPNumber(EPNumber ep)
  {
    ASSERT(ep <= EPNumber::EP15);
    return static_cast<EPNumber>(EPNumberToInt8(ep) + 1);
  }

  /**
   * @brief 端点配置结构体
   *        Endpoint configuration struct
   */
  struct Config
  {
    Direction direction = Direction::OUT;   ///< 端点方向 / Endpoint direction
    Type type = Type::BULK;                 ///< 端点类型 / Endpoint type
    uint16_t max_packet_size = UINT16_MAX;  ///< 最大包长 / Max packet size
    bool double_buffer = false;
    uint8_t mult = 0;  ///< 多包，高带宽端点用 / Multiplier (high-bandwidth)
  };

  /**
   * @brief 构造函数
   *        Constructor
   *
   * @param number 端点号 / Endpoint number
   * @param dir 允许配置的端点方向 / Allowed endpoint direction
   */
  explicit Endpoint(EPNumber number, Direction dir, RawData buffer)
      : number_(number), avail_direction_(dir), buffer_(buffer), double_buffer_(buffer)
  {
  }

  /**
   * @brief 析构函数
   *        Destructor
   */
  ~Endpoint() = default;

  // 禁止拷贝
  Endpoint(const Endpoint&) = delete;
  Endpoint& operator=(const Endpoint&) = delete;

  /**
   * @brief 获取端点号
   *        Get endpoint number
   */
  EPNumber GetNumber() const { return number_; }

  /**
   * @brief 获取允许配置的端点方向
   *        Get allowed endpoint direction
   */
  Direction AvailableDirection() const { return avail_direction_; }

  /**
   * @brief 获取端点方向
   *        Get endpoint direction
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
   * @brief 获取端点地址（方向 + 号）
   *        Get endpoint address (dir + num)
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
   * @brief 获取端点状态
   *        Get endpoint state
   */
  State GetState() const { return state_; }

  /**
   * @brief 设置端点状态
   *        Set endpoint state
   *
   * @param state 状态 / State
   */
  void SetState(State state) { state_ = state; }

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

  /**
   * @brief 是否挂起 / Is endpoint stalled
   *
   * @return true
   * @return false
   */
  bool IsStalled() const { return state_ == State::STALLED; }

  /**
   * @brief 是否使用双缓冲区 / Use double buffer
   *
   * @return true
   * @return false
   */
  bool UseDoubleBuffer() const { return config_.double_buffer; }

  /**
   * @brief 获取端点缓冲区
   *        Get endpoint buffer
   *
   * @return RawData
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
   *
   * @param cb 传输完成回调 / Transfer complete callback
   */
  void SetOnTransferCompleteCallback(Callback<ConstRawData&> cb)
  {
    on_transfer_complete_ = cb;
  }

  void SetActiveLength(uint16_t len) { double_buffer_.SetActiveLength(len); }

  size_t GetActiveLength() { return double_buffer_.GetActiveLength(); }

  /**
   * @brief 返回最大可传输字节数
   *        Return the maximum transferable size at this time
   *
   * 默认为最大包长，可在支持 FIFO 优化的平台中重载为动态剩余容量。
   * Default equals MaxPacketSize; override for FIFO-aware implementation.
   */
  virtual size_t MaxTransferSize() const { return MaxPacketSize(); }

  /**
   * @brief 二次初始化/配置端点协议参数（由Pool/Manager分配后调用）
   *        Configure endpoint protocol parameters (call after pool allocation)
   */
  virtual void Configure(const Config& cfg) = 0;

  /**
   * @brief 关闭端点（软禁用/资源复位）
   *        Close (soft disable)
   */
  virtual void Close() = 0;

  /**
   * @brief 停止端点传输
   *        Stop endpoint transfer
   *
   * @return ErrorCode
   */
  ErrorCode virtual Stall() = 0;

  /**
   * @brief 清除端点停止状态
   *        Clear endpoint stop status
   *
   * @return ErrorCode
   */
  ErrorCode virtual ClearStall() = 0;

  /**
   * @brief 传输数据
   *        Transfer data
   *
   * @param size 传输大小 / Transfer size
   * @return ErrorCode
   */
  virtual ErrorCode Transfer(size_t size) = 0;

  virtual ErrorCode TransferMultiBulk(RawData& data)
  {
    auto ep_buf = GetBuffer();
    size_t max_chunk = MaxTransferSize();

    if (max_chunk == 0)
    {
      return ErrorCode::ARG_ERR;
    }

    // 单包就能搞定的情况：不进入 multi-bulk 状态机，保持原有行为
    if (data.size_ <= max_chunk)
    {
      multi_bulk_ = false;
      multi_bulk_remain_ = 0;
      multi_bulk_data_ = {nullptr, 0};

      if (GetDirection() == Direction::IN)
      {
        // IN：发送，先把应用 buffer 拷到 EP buffer，再一次性 Transfer
        auto src = static_cast<const uint8_t*>(data.addr_);
        auto dst = static_cast<uint8_t*>(ep_buf.addr_);
        Memory::FastCopy(dst, src, data.size_);
      }
      // OUT：接收，OUT 方向不预拷贝，直接启动一次接收即可

      return Transfer(data.size_);
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
   * @brief 传输空包
   *        Transfer zero length packet
   *
   * @return ErrorCode
   */
  virtual ErrorCode TransferZLP() { return Transfer(0); }

  void OnTransferCompleteCallback(bool in_isr, size_t actual_transfer_size)
  {
    if (GetState() != State::BUSY)
    {
      return;
    }

    bool callback_uses_app_buffer = false;  // 是否用 multi_bulk_data_ 作为回调数据
    const Direction dir = GetDirection();
    const size_t max_chunk = MaxTransferSize();

    if (multi_bulk_)
    {
      if (dir == Direction::IN)
      {
        // ========= IN 方向：device -> host，多包发送 =========
        if (multi_bulk_remain_ > 0)
        {
          // 还有剩余没发送的字节，准备下一包
          auto ep_buf = GetBuffer();

          // 已经拷贝出去的字节数 = 总长 - 剩余
          const size_t sent = multi_bulk_data_.size_ - multi_bulk_remain_;

          size_t chunk = max_chunk;
          if (chunk > multi_bulk_remain_)
          {
            chunk = multi_bulk_remain_;
          }

          auto src = static_cast<const uint8_t*>(multi_bulk_data_.addr_) + sent;
          auto dst = static_cast<uint8_t*>(ep_buf.addr_);

          Memory::FastCopy(dst, src, chunk);
          multi_bulk_remain_ -= chunk;

          // 启动下一次传输（中间过程不回调上层）
          SetState(State::IDLE);
          (void)Transfer(chunk);
          return;
        }
        else
        {
          // 已经没有剩余需要发送了，这是最后一包完成
          multi_bulk_ = false;
          // 对 TX 来说，上层一般只关心“发完了”，data 内容用不用无所谓，下面走公共尾部
        }
      }
      else
      {
        // ========= OUT 方向：host -> device，多包接收 =========
        auto ep_buf = GetBuffer();

        size_t prev_remain = multi_bulk_remain_;
        size_t recvd = actual_transfer_size;

        // 理论上 recvd 不会超过我们这次要求的长度，但这里做个保护
        if (recvd > prev_remain)
        {
          recvd = prev_remain;
        }

        // 已写入应用 buffer 的偏移 = 总长 - 之前剩余
        const size_t offset = multi_bulk_data_.size_ - prev_remain;

        auto dst = static_cast<uint8_t*>(multi_bulk_data_.addr_) + offset;
        auto src = static_cast<const uint8_t*>(ep_buf.addr_);

        // 把本次收到的数据拷到应用层 buffer
        Memory::FastCopy(dst, src, recvd);

        // 更新剩余可写容量
        multi_bulk_remain_ = prev_remain - recvd;

        const bool short_packet = (recvd < max_chunk);
        const bool buffer_full = (multi_bulk_remain_ == 0);

        if (!short_packet && !buffer_full)
        {
          // 还没结束：既没有 short packet，应用 buffer 也还有空间
          size_t chunk = max_chunk;
          if (chunk > multi_bulk_remain_)
          {
            chunk = multi_bulk_remain_;
          }

          SetState(State::IDLE);
          (void)Transfer(chunk);
          return;  // 中间过程不回调上层
        }

        // 否则：收到了 short packet 或 buffer 填满 -> 结束 multi-bulk
        multi_bulk_ = false;
        callback_uses_app_buffer = true;

        // 对上层报告的实际总长度 = 已经写入应用 buffer 的字节数
        actual_transfer_size = multi_bulk_data_.size_ - multi_bulk_remain_;
      }
    }

    // ========= 走到这里：
    // 1）不是 multi-bulk 的普通传输，或
    // 2）multi-bulk（IN/OUT）已经完成，准备回调上层 =========

    SetState(State::IDLE);

    ConstRawData data;

    if (callback_uses_app_buffer)
    {
      // multi-bulk OUT：回调给上层的是应用层的 buffer
      data = ConstRawData(multi_bulk_data_.addr_, actual_transfer_size);
    }
    else
    {
      // 普通情况或 multi-bulk IN：保持原有逻辑
      data = UseDoubleBuffer()
                 ? ConstRawData(dir == Direction::OUT ? double_buffer_.ActiveBuffer()
                                                      : double_buffer_.PendingBuffer(),
                                actual_transfer_size)
                 : ConstRawData(buffer_.addr_, actual_transfer_size);

      if (UseDoubleBuffer() && dir == Direction::OUT)
      {
        // 原有 OUT 方向的双缓冲切换策略
        SwitchBuffer();
      }
    }

    on_transfer_complete_.Run(in_isr, data);
  }

 protected:
  /**
   * @brief 获取当前配置
   *        Get endpoint config
   */
  Config& GetConfig() { return config_; }

  /**
   * @brief 切换缓冲区
   *        Switch buffer
   *
   */
  virtual void SwitchBuffer()
  {
    double_buffer_.EnablePending();
    double_buffer_.Switch();
  }

  /**
   * @brief 设置当前活动缓冲区
   *        Set active buffer
   *
   * @param active_block true 表示使用第二个缓冲区，false 表示使用第一个缓冲区
   */
  virtual void SetActiveBlock(bool active_block)
  {
    double_buffer_.SetActiveBlock(active_block);
    double_buffer_.EnablePending();
  }

 private:
  /// 传输完成回调 / Called when transfer completes
  LibXR::Callback<LibXR::ConstRawData&> on_transfer_complete_;

  EPNumber number_;                    ///< 当前端点编号 / Endpoint number
  Direction avail_direction_;          ///< 可配置方向 / Allowed direction
  Config config_;                      ///< 当前端点配置 / Current configuration
  State state_ = State::DISABLED;      ///< 当前状态 / Endpoint status
  LibXR::RawData buffer_;              ///< 端点缓冲区 / Endpoint buffer
  LibXR::DoubleBuffer double_buffer_;  ///< 双缓冲区 / Double buffer
  bool multi_bulk_ = false;             ///< 当前是否处于多包 bulk 发送过程
  RawData multi_bulk_data_{};     ///< 应用层整体 buffer（addr = 起始地址，size_ = 总长度）
  size_t multi_bulk_remain_ = 0;  ///< 剩余尚未发送的字节数
};  // namespace LibXR::USB

}  // namespace LibXR::USB
