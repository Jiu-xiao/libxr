#pragma once

#include "libxr.hpp"

namespace LibXR
{

/**
 * @class CAN
 * @brief CAN controller abstraction with classic-frame TX plus RX/error
 *        subscription hooks.
 *        CAN 控制器抽象：提供经典帧发送接口，以及接收/错误订阅钩子。
 */
class CAN
{
 public:
  /**
   * @enum IDFormat
   * @brief CAN ID 格式。CAN identifier format.
   */
  enum class IDFormat : uint8_t
  {
    STANDARD = 0,  ///< 11-bit 标准 ID。11-bit standard identifier.
    EXTENDED = 1,  ///< 29-bit 扩展 ID。29-bit extended identifier.
  };

  /**
   * @struct BitTiming
   * @brief CAN 位时序配置。Bit timing configuration for CAN.
   */
  struct BitTiming
  {
    uint32_t brp = 0;         ///< 预分频。Baud rate prescaler.
    uint32_t prop_seg = 0;    ///< 传播段。Propagation segment.
    uint32_t phase_seg1 = 0;  ///< 相位段 1。Phase segment 1.
    uint32_t phase_seg2 = 0;  ///< 相位段 2。Phase segment 2.
    uint32_t sjw = 0;         ///< 同步跳宽。Synchronization jump width.
  };

  /**
   * @struct Mode
   * @brief CAN 工作模式。CAN operating mode.
   */
  struct Mode
  {
    bool loopback = false;         ///< 回环模式。Loopback mode.
    bool listen_only = false;      ///< 只听（静默）模式。Listen-only (silent) mode.
    bool triple_sampling = false;  ///< 三采样。Triple sampling.
    bool one_shot = false;         ///< 单次发送模式。One-shot transmission.
  };

  /**
   * @struct Configuration
   * @brief CAN 配置参数。CAN configuration parameters.
   */
  struct Configuration
  {
    uint32_t bitrate = 0;       ///< 仲裁相位目标波特率。Target nominal bitrate.
    float sample_point = 0.0f;  ///< 仲裁相位采样点（0~1）。Nominal sample point (0–1).
    BitTiming bit_timing;       ///< 位时序配置。Bit timing configuration.
    Mode mode;                  ///< 工作模式。Operating mode.
  };

  /**
   * @brief 设置 CAN 配置。Set CAN configuration.
   * @param cfg 配置参数。Configuration parameters.
   * @return ErrorCode 操作结果。Error code.
   */
  virtual ErrorCode SetConfig(const CAN::Configuration& cfg) = 0;

  /**
   * @brief 获取 CAN 外设时钟频率（Hz）。
   *        Get CAN peripheral clock frequency in Hz.
   */
  virtual uint32_t GetClockFreq() const = 0;

  /**
   * @struct ErrorState
   * @brief CAN 当前错误状态快照（来自硬件计数器/状态机）。
   *        Snapshot of current CAN controller error state (from HW counters/state).
   */
  struct ErrorState
  {
    uint8_t tx_error_counter = 0;  ///< 发送错误计数 TEC。Transmit error counter (TEC).
    uint8_t rx_error_counter = 0;  ///< 接收错误计数 REC。Receive error counter (REC).

    bool bus_off = false;        ///< 是否处于 BUS-OFF。True if controller is bus-off.
    bool error_passive = false;  ///< 是否处于 Error Passive。True if error-passive.
    bool error_warning = false;  ///< 是否处于 Error Warning。True if error-warning.
  };

  /**
   * @brief 查询当前错误状态（快照）。
   *        Query current CAN controller error state (snapshot).
   *
   * 默认实现返回 ErrorCode::NOT_SUPPORT；具体实现（如 bxCAN/FDCAN）可重载，
   * 从硬件寄存器读取 TEC/REC 及状态位，并填充 ErrorState。
   *
   * @param state 输出参数，用于返回当前错误状态。
   * @return ErrorCode 操作结果；若未实现则返回 ErrorCode::NOT_SUPPORT。
   */
  virtual ErrorCode GetErrorState(ErrorState& state) const
  {
    (void)state;
    return ErrorCode::NOT_SUPPORT;
  }

  /**
   * @brief 构造函数。Constructor.
   */
  CAN() = default;

  /**
   * @brief 虚析构函数。Virtual destructor.
   */
  virtual ~CAN() = default;

  /**
   * @struct ClassicPack
   * @brief 经典 CAN 帧数据结构。Classic CAN frame structure.
   */
  struct ClassicPack
  {
    uint32_t id = 0;                           ///< 纯 11/29-bit ID。Raw 11/29-bit identifier.
    IDFormat format = IDFormat::STANDARD;      ///< ID 格式。Identifier format.
    bool remote = false;                       ///< 是否为 remote frame。Whether this is a remote frame.
    uint8_t dlc = 0;                           ///< classic DLC（0~8）。Classic DLC (0–8).
    uint8_t data[8]{};                         ///< 数据载荷。Data payload.
  };

  /**
   * @enum ErrorID
   * @brief CAN 错误事件标识。CAN error-event identifier.
   */
  enum class ErrorID : uint32_t
  {
    CAN_ERROR_ID_GENERIC = 0,
    CAN_ERROR_ID_BUS_OFF,
    CAN_ERROR_ID_ERROR_PASSIVE,
    CAN_ERROR_ID_ERROR_WARNING,
    CAN_ERROR_ID_PROTOCOL,
    CAN_ERROR_ID_ACK,
    CAN_ERROR_ID_STUFF,
    CAN_ERROR_ID_FORM,
    CAN_ERROR_ID_BIT0,
    CAN_ERROR_ID_BIT1,
    CAN_ERROR_ID_CRC,
    CAN_ERROR_ID_OTHER,
  };

  /**
   * @struct ErrorEvent
   * @brief 控制器错误事件。Controller error event.
   */
  struct ErrorEvent
  {
    ErrorID id = ErrorID::CAN_ERROR_ID_GENERIC;  ///< 错误类型。Error identifier.
    ErrorState state{};                          ///< 错误状态快照。Error-state snapshot.
  };

  /// classic CAN 帧回调类型。Classic CAN frame callback type.
  using Callback = LibXR::Callback<const ClassicPack&>;
  /// CAN 错误事件回调类型。CAN error-event callback type.
  using ErrorCallback = LibXR::Callback<const ErrorEvent&>;

  /**
   * @enum FilterMode
   * @brief CAN 过滤器模式。CAN filter mode.
   */
  enum class FilterMode : uint8_t
  {
    ID_MASK = 0,  ///< 掩码匹配：(id & start_id_mask) == end_id_mask。
                  ///< Mask match: (id & start_id_mask) == end_id_mask.
    ID_RANGE = 1  ///< 区间匹配：start_id_mask <= id <= end_id_mask。
                  ///< Range match: start_id_mask <= id <= end_id_mask.
  };

  /**
   * @struct Filter
   * @brief 经典 CAN 订阅过滤器。Classic CAN subscription filter.
   */
  struct Filter
  {
    FilterMode mode = FilterMode::ID_RANGE;  ///< 过滤模式。Filter mode.
    uint32_t start_id_mask = 0;              ///< 起始 ID 或掩码。Start ID or mask.
    uint32_t end_id_mask = UINT32_MAX;       ///< 结束 ID 或匹配值。End ID or match value.
    IDFormat format = IDFormat::STANDARD;    ///< ID 格式。Identifier format.
    bool remote = false;                     ///< 是否只匹配 remote frame。Whether to match remote frames.
    Callback cb;                             ///< 回调函数。Callback function.
  };

  /**
   * @struct ErrorSubscriber
   * @brief 错误事件订阅项。Error-event subscriber entry.
   */
  struct ErrorSubscriber
  {
    ErrorCallback cb;  ///< 回调函数。Callback function.
  };

  /**
   * @brief 注册经典 CAN 消息回调。
   *        Register classic CAN frame callback.
   *
   * @param cb 回调函数。Callback function.
   * @param format 目标 ID 格式。Target identifier format.
   * @param remote 是否匹配 remote frame。Whether to match remote frames.
   * @param mode 过滤器模式。Filter mode.
   * @param start_id_mask 起始 ID 或掩码。Start ID or mask.
   * @param end_id_mask 结束 ID 或匹配值。End ID or match value.
   */
  void Register(Callback cb, IDFormat format, bool remote = false,
                FilterMode mode = FilterMode::ID_RANGE, uint32_t start_id_mask = 0,
                uint32_t end_id_mask = UINT32_MAX);

  /**
   * @brief 注册控制器错误事件回调。Register controller error-event callback.
   * @param cb 回调函数。Callback function.
   */
  void RegisterError(ErrorCallback cb);

  /**
   * @brief 添加经典 CAN 消息。Add classic CAN message.
   * @param pack 经典 CAN 帧。Classic CAN frame.
   * @return ErrorCode 操作结果。Error code.
   */
  virtual ErrorCode AddMessage(const ClassicPack& pack) = 0;

 protected:
  /**
   * @brief 分发接收到的经典 CAN 帧。
   *        Dispatch a received classic CAN frame.
   * @param pack 接收到的帧。Received frame.
   * @param in_isr 是否在中断上下文中。True if called in ISR context.
   */
  void OnMessage(const ClassicPack& pack, bool in_isr);

  /**
   * @brief 分发控制器错误事件。Dispatch one controller error event.
   * @param event 错误事件。Error event.
   * @param in_isr 是否在中断上下文中。True if called in ISR context.
   */
  void OnError(const ErrorEvent& event, bool in_isr);

 private:
  [[nodiscard]] static constexpr size_t ClassicBucketIndex(IDFormat format, bool remote)
  {
    return static_cast<size_t>(format) + (remote ? 2u : 0u);
  }

  /// 按 format/remote 维度划分的 classic 订阅者链表。Classic subscriber lists by format/remote bucket.
  LockFreeList subscriber_list_[4];
  /// 错误事件订阅者链表。Error-event subscriber list.
  LockFreeList error_subscriber_list_;
};

/**
 * @class FDCAN
 * @brief FDCAN 通信抽象类，扩展支持 CAN FD 数据帧。
 *        Abstract class for FDCAN communication with CAN FD data-frame support.
 */
class FDCAN : public CAN
{
 public:
  /**
   * @brief 构造函数。Constructor.
   */
  FDCAN() = default;

  /**
   * @brief 虚析构函数。Virtual destructor.
   */
  virtual ~FDCAN() = default;

  /**
   * @struct FDPack
   * @brief CAN FD 数据帧数据结构。CAN FD data-frame structure.
   */
  struct FDPack
  {
    uint32_t id = 0;                           ///< 纯 11/29-bit ID。Raw 11/29-bit identifier.
    IDFormat format = IDFormat::STANDARD;      ///< ID 格式。Identifier format.
    bool brs = false;                          ///< 该帧是否启用 BRS。Whether this frame uses BRS.
    bool esi = false;                          ///< 该帧的 ESI 值。ESI observed/carried by this frame.
    uint8_t len = 0;                           ///< 数据长度（0~64）。Payload length (0–64 bytes).
    uint8_t data[64]{};                        ///< 数据载荷。Data payload.
  };

  using CAN::AddMessage;
  using CAN::FilterMode;
  using CAN::Register;
  using CAN::RegisterError;

  /// FD 帧回调类型。Callback type for FD frames.
  using CallbackFD = LibXR::Callback<const FDPack&>;

  /**
   * @struct Filter
   * @brief FDCAN 订阅过滤器。FDCAN subscription filter.
   */
  struct Filter
  {
    FilterMode mode = FilterMode::ID_RANGE;  ///< 过滤模式。Filter mode.
    uint32_t start_id_mask = 0;              ///< 起始 ID 或掩码。Start ID or mask.
    uint32_t end_id_mask = UINT32_MAX;       ///< 结束 ID 或匹配值。End ID or match value.
    IDFormat format = IDFormat::STANDARD;    ///< ID 格式。Identifier format.
    CallbackFD cb;                           ///< 回调函数。Callback function.
  };

  /**
   * @struct DataBitTiming
   * @brief 数据相位位时序配置。Data phase bit timing configuration.
   */
  struct DataBitTiming
  {
    uint32_t brp = 0;         ///< 预分频。Prescaler.
    uint32_t prop_seg = 0;    ///< 传播段。Propagation segment.
    uint32_t phase_seg1 = 0;  ///< 相位段 1。Phase segment 1.
    uint32_t phase_seg2 = 0;  ///< 相位段 2。Phase segment 2.
    uint32_t sjw = 0;         ///< 同步跳宽。Synchronization jump width.
  };

  /**
   * @struct FDMode
   * @brief FDCAN 通道级 FD 模式配置。Channel-level FD mode configuration.
   */
  struct FDMode
  {
    bool fd_enabled = false;  ///< 是否启用 CAN FD。Enable CAN FD.
  };

  /**
   * @struct Configuration
   * @brief FDCAN 配置参数，扩展 CAN::Configuration。
   *        FDCAN configuration, extending CAN::Configuration.
   */
  struct Configuration : public CAN::Configuration
  {
    uint32_t data_bitrate = 0;       ///< 数据相位波特率。Data-phase bitrate.
    float data_sample_point = 0.0f;  ///< 数据相位采样点。Data-phase sample point.
    DataBitTiming data_timing;       ///< 数据相位位时序。Data-phase bit timing.
    FDMode fd_mode;                  ///< FD 通道模式配置。FD channel-mode configuration.
  };

  /**
   * @brief 设置经典 CAN 配置。
   *        Set classic CAN configuration.
   *
   * 作为 FDCAN 的兼容接口，具体实现可仅使用仲裁相位配置。
   *
   * @param cfg CAN 配置。CAN configuration.
   * @return ErrorCode 操作结果。Error code.
   */
  virtual ErrorCode SetConfig(const CAN::Configuration& cfg) override = 0;

  /**
   * @brief 设置 FDCAN 配置。
   *        Set FDCAN configuration.
   * @param cfg FDCAN 配置。FDCAN configuration.
   * @return ErrorCode 操作结果。Error code.
   */
  virtual ErrorCode SetConfig(const FDCAN::Configuration& cfg) = 0;

  /**
   * @brief 注册 FDCAN FD 帧回调。
   *        Register FDCAN FD frame callback.
   * @param cb 回调函数。Callback function.
   * @param format 目标 ID 格式。Target identifier format.
   * @param mode 过滤器模式。Filter mode.
   * @param start_id_mask 起始 ID 或掩码。Start ID or mask.
   * @param end_id_mask 结束 ID 或匹配值。End ID or match value.
   */
  void Register(CallbackFD cb, IDFormat format, FilterMode mode = FilterMode::ID_RANGE,
                uint32_t start_id_mask = 0, uint32_t end_id_mask = UINT32_MAX);

  /**
   * @brief 添加 FD CAN 消息。Add FD CAN message.
   * @param pack FD CAN 帧。FD CAN frame.
   * @return ErrorCode 操作结果。Error code.
   */
  virtual ErrorCode AddMessage(const FDPack& pack) = 0;

 protected:
  using CAN::OnMessage;

  /**
   * @brief 分发接收到的 FD CAN 帧。
   *        Dispatch a received FD CAN frame.
   * @param pack 接收到的 FD 帧。Received FD frame.
   * @param in_isr 是否在中断上下文中。True if called in ISR context.
   */
  void OnMessage(const FDPack& pack, bool in_isr);

 private:
  /// 按 format 维度划分的 FD 订阅者链表。FD subscriber lists by identifier format.
  LockFreeList subscriber_list_fd_[2];
};

}  // namespace LibXR
