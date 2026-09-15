#pragma once

#include <atomic>
#include <cstdint>

#include "libxr_cb.hpp"
#include "libxr_def.hpp"
#include "libxr_mem.hpp"
#include "libxr_rw.hpp"
#include "libxr_type.hpp"

namespace LibXR::USB
{

class EndpointPool;

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
  enum class State : uint32_t
  {
    DISABLED,  ///< 禁用 / Disabled
    IDLE,      ///< 空闲 / Idle
    BUSY,      ///< 忙 / Busy
    STALLED,   ///< 停止/挂起 / Stalled
    ERROR,     ///< 错误 / Error
    RESULT     ///< 已完成的接收仍由类消费 / Retained receive result
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

  /** Endpoint protocol and bounded payload storage configuration. */
  struct Config
  {
    Direction direction = Direction::OUT;
    Type type = Type::BULK;
    uint16_t max_packet_size = UINT16_MAX;
    size_t transfer_size = 0;  ///< Zero selects the supplied storage's capacity.
    uint8_t mult = 0;
  };

  /**
   * Synchronous, state-authorized producer view. SetSize publishes stable storage
   * before a WriteQueue scope can settle. It neither starts hardware nor callbacks.
   * No SetSize means no work; SetSize(0) requests an ordinary zero-length TX.
   */
  class TxFill
  {
   public:
    TxFill(const TxFill&) = delete;
    TxFill& operator=(const TxFill&) = delete;
    RawData Buffer() const { return buffer_; }
    bool CanStart() const { return can_start_; }
    void SetSize(size_t size);

   private:
    friend class Endpoint;
    TxFill(Endpoint& endpoint, RawData buffer, bool can_start)
        : endpoint_(endpoint), buffer_(buffer), can_start_(can_start)
    {
    }
    Endpoint& endpoint_;
    RawData buffer_;
    const bool can_start_;
    bool supplied_ = false;
    size_t size_ = 0;
  };

  explicit Endpoint(EPNumber number, Direction dir, RawData buffer);
  virtual ~Endpoint() = default;
  Endpoint(const Endpoint&) = delete;
  Endpoint& operator=(const Endpoint&) = delete;

  EPNumber GetNumber() const { return number_; }
  Direction AvailableDirection() const { return avail_direction_; }
  Direction GetDirection() const
  {
    return GetState() == State::DISABLED ? avail_direction_ : config_.direction;
  }
  uint8_t GetAddress() const
  {
    return GetState() == State::DISABLED ? EPNumberToInt8(number_)
                                         : EPNumberToAddr(number_, config_.direction);
  }
  State GetState() const { return state_.load(std::memory_order_acquire); }
  Type GetType() const { return config_.type; }
  uint16_t MaxPacketSize() const { return config_.max_packet_size; }
  size_t MaxTransferSize() const { return capacity_; }
  bool IsStalled() const { return GetState() == State::STALLED; }

  void Configure(const Config& cfg);
  void Close();
  ErrorCode Stall();
  ErrorCode ClearStall();

  void SetOnTxFill(Callback<TxFill&> callback) { on_tx_fill_ = callback; }
  void SetOnTransferCompleteCallback(Callback<ConstRawData&> callback)
  {
    on_transfer_complete_ = callback;
  }
  void SetOnTransferStarted(Callback<size_t> callback) { on_started_ = callback; }
  void SetOnTransferError(Callback<ErrorCode> callback) { on_error_ = callback; }
  void SetOnWork(Callback<> callback) { on_work_ = callback; }

  /// Payload must already reside in persistent class storage before this doorbell.
  void RequestService(bool in_isr = false);
  void RequestTx(bool in_isr = false) { RequestService(in_isr); }

  /// Owner-only receive authorization. RESULT is retained until explicit rearm.
  ErrorCode ArmReceive(size_t size);
  bool CanPrepareTx() const
  {
    return config_.direction == Direction::IN && prepared_size_ == 0U &&
           (GetState() == State::IDLE || GetState() == State::BUSY);
  }
  bool HasReceiveResult() const { return GetState() == State::RESULT; }
  ConstRawData ReceiveResult() const;

  // Owner-only direct transfer for EP0 and class migration. No buffer can escape
  // its controller-serialized scope; production classes should bind TxFill.
  RawData GetBuffer() const;
  ErrorCode Transfer(size_t size);
  // Backend/core control-state handoff only; not an application admission API.
  void ResetAfterHardwareStop();
  // Control setup arbitration may retire a captured old status completion before
  // aborting the old request. This does not start/produce a subsequent transfer.
  void RetireCapturedCompletion(bool in_isr) { CompleteSegment(in_isr); }
  ErrorCode TransferZLP() { return Transfer(0); }
  void SetActiveLength(size_t size);
  size_t GetActiveLength() const { return prepared_size_; }

  /// Hardware must stop accepting more data and clear its old source before this
  /// exact segment completion is published. Zero length is still a completion.
  void OnTransferCompleteCallback(bool in_isr, size_t actual_size,
                                  ErrorCode result = ErrorCode::OK);

  /// Backend-visible snapshot of the one armed segment, not the writable buffer.
  RawData TransferBuffer() const { return transfer_buffer_; }
  size_t SegmentSize() const { return segment_size_; }

 protected:
  Config& GetConfig() { return config_; }
  RawData HardwareBuffer() const { return hardware_buffer_; }
  void SetState(State state) { state_.store(state, std::memory_order_release); }
  bool IsPlanning() const;
  void RequireStaging() { fixed_hardware_buffer_ = true; }
  virtual void ConfigureHardware(const Config& cfg) = 0;
  virtual void CloseHardware() = 0;
  virtual ErrorCode StallHardware() = 0;
  virtual ErrorCode ClearStallHardware() = 0;
  virtual ErrorCode StartHardware(RawData buffer, size_t size) = 0;
  virtual size_t MaxHardwareTransferSize() const { return MaxPacketSize(); }

 private:
  friend class EndpointPool;
  void Attach(EndpointPool& pool) { pool_ = &pool; }
  void Service(bool in_isr, bool force = false);
  void DriveTx(bool in_isr);
  void CompleteSegment(bool in_isr);
  void ResetTransfer();
  void InitializeStorage(size_t required);
  bool CanProgress() const;
  ErrorCode StartPrepared();
  ErrorCode StartSegment();
  ErrorCode StartZero();
  RawData PayloadBuffer() const;

  EPNumber number_;
  Direction avail_direction_;
  Config config_;
  std::atomic<State> state_{State::DISABLED};
  EndpointPool* pool_ = nullptr;
  RawData hardware_buffer_;
  uint8_t* storage_ = nullptr;
  size_t capacity_ = 0;
  bool storage_initialized_ = false;
  bool fixed_hardware_buffer_ = false;
  uint8_t current_buffer_ = 1;
  size_t active_size_ = 0;
  size_t prepared_size_ = 0;
  size_t completed_size_ = 0;
  size_t segment_size_ = 0;
  RawData transfer_buffer_{nullptr, 0};
  bool hardware_active_ = false;

  // A controller IRQ is the only producer, the controller owner is the consumer.
  // Word-sized publication avoids byte-atomic runtime ABIs on Cortex-M0.
  std::atomic<uint32_t> completion_ready_{0};
  size_t completion_size_ = 0;
  ErrorCode completion_result_ = ErrorCode::OK;
  std::atomic<uint32_t> work_pending_{0};
  Callback<size_t> on_started_;
  Callback<TxFill&> on_tx_fill_;
  Callback<ConstRawData&> on_transfer_complete_;
  Callback<ErrorCode> on_error_;
  Callback<> on_work_;
};

}  // namespace LibXR::USB
