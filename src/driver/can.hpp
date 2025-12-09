#pragma once

#include "libxr.hpp"
#include "message.hpp"

namespace LibXR
{

/**
 * @class CAN
 * @brief CAN 通信抽象类，定义经典 CAN 帧与订阅接口。
 *        Abstract class for CAN communication with classic CAN frames and subscription
 * API.
 */
class CAN
{
 public:
  /**
   * @enum Type
   * @brief CAN 消息类型。CAN frame type.
   */
  enum class Type : uint8_t
  {
    STANDARD = 0,         ///< 标准数据帧（11-bit ID）。Standard data frame (11-bit ID).
    EXTENDED = 1,         ///< 扩展数据帧（29-bit ID）。Extended data frame (29-bit ID).
    REMOTE_STANDARD = 2,  ///< 标准远程帧。Standard remote frame.
    REMOTE_EXTENDED = 3,  ///< 扩展远程帧。Extended remote frame.
    TYPE_NUM = 4          ///< 类型数量上界。Number of frame types.
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
    uint32_t bitrate = 0;       ///< 目标仲裁波特率。Target nominal bitrate.
    float sample_point = 0.0f;  ///< 目标采样点（0~1）。Target sample point (0~1).

    BitTiming bit_timing{};  ///< 位时序配置。Bit timing configuration.
    Mode mode{};             ///< 工作模式。Operating mode.
  };

  /**
   * @brief 设置 CAN 配置。Set CAN configuration.
   * @param cfg 配置参数。Configuration parameters.
   * @return ErrorCode 操作结果。Error code.
   */
  virtual ErrorCode SetConfig(const CAN::Configuration &cfg) = 0;

  /**
   * @brief 获取 CAN 外设时钟频率（单位 Hz）。
   *        Get CAN peripheral clock frequency in Hz.
   */
  virtual uint32_t GetClockFreq() const = 0;

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
  struct __attribute__((packed)) ClassicPack
  {
    uint32_t id;      ///< CAN ID（11 或 29 bit）。CAN ID (11 or 29 bits).
    Type type;        ///< 帧类型。Frame type.
    uint8_t data[8];  ///< 数据载荷（最多 8 字节）。Data payload (up to 8 bytes).
  };

  /// 回调类型。Callback type.
  using Callback = LibXR::Callback<const ClassicPack &>;

  /**
   * @enum FilterMode
   * @brief CAN 过滤器模式。CAN filter mode.
   */
  enum class FilterMode : uint8_t
  {
    ID_MASK = 0,  ///< 掩码匹配：(id & start_id_mask) == end_id_match。
                  ///< Mask match: (id & start_id_mask) == end_id_match.
    ID_RANGE = 1  ///< 区间匹配：start_id_mask <= id <= end_id_match。
                  ///< Range match: start_id_mask <= id <= end_id_match.
  };

  /**
   * @struct Filter
   * @brief 经典 CAN 订阅过滤器。Classic CAN subscription filter.
   */
  struct Filter
  {
    FilterMode mode;         ///< 过滤模式。Filter mode.
    uint32_t start_id_mask;  ///< 起始 ID 或掩码。Start ID or mask.
    uint32_t end_id_match;   ///< 结束 ID 或匹配值。End ID or match value.
    Type type;               ///< 帧类型。Frame type.
    Callback cb;             ///< 回调函数。Callback function.
  };

  /**
   * @brief 注册经典 CAN 消息回调。
   *        Register classic CAN message callback.
   *
   * @param cb 回调函数。Callback function.
   * @param type 帧类型。Frame type.
   * @param mode 过滤器模式。Filter mode.
   * @param start_id_mask 起始 ID 或掩码。Start ID or mask.
   * @param end_id_match 结束 ID 或匹配值。End ID or match value.
   */
  void Register(Callback cb, Type type, FilterMode mode = FilterMode::ID_RANGE,
                uint32_t start_id_mask = 0, uint32_t end_id_match = UINT32_MAX);

  /**
   * @brief 添加经典 CAN 消息。Add classic CAN message.
   * @param pack 经典 CAN 帧。Classic CAN frame.
   * @return ErrorCode 操作结果。Error code.
   */
  virtual ErrorCode AddMessage(const ClassicPack &pack) = 0;

 protected:
  /**
   * @brief 分发接收到的经典 CAN 帧。
   *        Dispatch a received classic CAN frame.
   * @param pack 接收到的帧。Received frame.
   * @param in_isr 是否在中断上下文中。True if called in ISR context.
   */
  void OnMessage(const ClassicPack &pack, bool in_isr);

 private:
  /// 按帧类型划分的订阅者链表数组。Subscriber lists per frame type.
  LockFreeList subscriber_list_[static_cast<uint8_t>(Type::TYPE_NUM)];
};

/**
 * @class FDCAN
 * @brief FDCAN 通信抽象类，扩展支持 CAN FD 帧。
 *        Abstract class for FDCAN communication with CAN FD frame support.
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
   * @brief CAN FD 帧数据结构。CAN FD frame structure.
   */
  struct __attribute__((packed)) FDPack
  {
    uint32_t id;       ///< CAN ID。CAN ID.
    Type type;         ///< 帧类型。Frame type.
    uint8_t len;       ///< 数据长度，最大 64 字节。Data length, up to 64 bytes.
    uint8_t data[64];  ///< 数据载荷。Data payload.
  };

  using CAN::AddMessage;
  using CAN::FilterMode;
  using CAN::OnMessage;
  using CAN::Register;

  /// FD 帧回调类型。Callback type for FD frames.
  using CallbackFD = LibXR::Callback<const FDPack &>;

  /**
   * @struct Filter
   * @brief FDCAN 订阅过滤器。FDCAN subscription filter.
   */
  struct Filter
  {
    FilterMode mode;         ///< 过滤模式。Filter mode.
    uint32_t start_id_mask;  ///< 起始 ID 或掩码。Start ID or mask.
    uint32_t end_id_mask;    ///< 结束 ID 或匹配值。End ID or match value.
    Type type;               ///< 帧类型。Frame type.
    CallbackFD cb;           ///< 回调函数。Callback function.
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
   * @brief FDCAN FD 模式配置。FDCAN FD mode configuration.
   */
  struct FDMode
  {
    bool fd_enabled = false;  ///< 是否启用 CAN FD。Enable CAN FD.
    bool brs = false;         ///< 是否启用 BRS。Enable Bit Rate Switch.
    bool esi = false;         ///< 全局 ESI 标志。Global ESI flag.
  };

  /**
   * @struct Configuration
   * @brief FDCAN 配置参数，扩展 CAN::Configuration。
   *        FDCAN configuration, extending CAN::Configuration.
   */
  struct Configuration : public CAN::Configuration
  {
    uint32_t data_bitrate = 0;       ///< 数据相位目标波特率。Target data-phase bitrate.
    float data_sample_point = 0.0f;  ///< 数据相位采样点。Data-phase sample point.

    DataBitTiming data_timing{};  ///< 数据相位位时序。Data-phase bit timing.
    FDMode fd_mode{};             ///< FD 模式配置。FD mode configuration.
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
  virtual ErrorCode SetConfig(const CAN::Configuration &cfg) = 0;

  /**
   * @brief 设置 FDCAN 配置。
   *        Set FDCAN configuration.
   * @param cfg FDCAN 配置。FDCAN configuration.
   * @return ErrorCode 操作结果。Error code.
   */
  virtual ErrorCode SetConfig(const FDCAN::Configuration &cfg) = 0;

  /**
   * @brief 注册 FDCAN FD 帧回调。
   *        Register FDCAN FD frame callback.
   * @param cb 回调函数。Callback function.
   * @param type 帧类型。Frame type.
   * @param mode 过滤器模式。Filter mode.
   * @param start_id_mask 起始 ID 或掩码。Start ID or mask.
   * @param end_id_mask 结束 ID 或匹配值。End ID or match value.
   */
  void Register(CallbackFD cb, Type type, FilterMode mode = FilterMode::ID_RANGE,
                uint32_t start_id_mask = 0, uint32_t end_id_mask = UINT32_MAX);

  /**
   * @brief 添加 FD CAN 消息。Add FD CAN message.
   * @param pack FD CAN 帧。FD CAN frame.
   * @return ErrorCode 操作结果。Error code.
   */
  virtual ErrorCode AddMessage(const FDPack &pack) = 0;

 protected:
  /**
   * @brief 分发接收到的 FD CAN 帧。
   *        Dispatch a received FD CAN frame.
   * @param pack 接收到的 FD 帧。Received FD frame.
   * @param in_isr 是否在中断上下文中。True if called in ISR context.
   */
  void OnMessage(const FDPack &pack, bool in_isr);

 private:
  /// 按帧类型划分的 FD 订阅者链表数组。FD subscriber lists per frame type.
  LockFreeList subscriber_list_fd_[static_cast<uint8_t>(Type::TYPE_NUM)];
};

}  // namespace LibXR
