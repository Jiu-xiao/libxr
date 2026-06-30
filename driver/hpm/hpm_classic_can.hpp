/// @file hpm_classic_can.hpp
/// @brief HPM classic `CAN_Type` IP 适配头文件 / Adapter header.
///
/// 本文件面向非 MCAN 的 classic CAN 外设。
/// MCAN classic CAN 使用 `hpm_can.*` 中的 `HPMCAN`。
/// MCAN FD 使用 `hpm_mcan.*` 中的 `HPMCANFD`。
#pragma once

#include <atomic>

#include "can.hpp"
#include "hpm_clock_drv.h"
#include "hpm_common.h"
#include "hpm_soc.h"
#include "lockfree_pool.hpp"

#if defined(MCAN_SOC_MAX_COUNT) && (MCAN_SOC_MAX_COUNT > 0)
#include "hpm_mcan.hpp"
#else

#if defined(HPMSOC_HAS_HPMSDK_CAN) && __has_include("hpm_can_drv.h") &&           \
                                                    defined(CAN_SOC_MAX_COUNT) && \
                                                    (CAN_SOC_MAX_COUNT > 0)
#include "hpm_can_drv.h"
#define LIBXR_HPM_CLASSIC_CAN_SUPPORTED 1
using LibXRHpmClassicCanType = CAN_Type;
#else
#define LIBXR_HPM_CLASSIC_CAN_SUPPORTED 0
using LibXRHpmClassicCanType = void;
#endif

namespace LibXR
{

/// @class HPMClassicCAN
/// @brief HPM classic `CAN_Type` 的 `LibXR::CAN` 适配器。
class HPMClassicCAN : public CAN
{
 public:
  static constexpr uint32_t kInvalidIrq = 0xFFFFFFFFu;
  static constexpr uint32_t kDefaultTxPoolSize = 8;

#if LIBXR_HPM_CLASSIC_CAN_SUPPORTED
  static constexpr uint8_t kMaxInstances = CAN_SOC_MAX_COUNT;
  static constexpr uint8_t kRxInterruptMask =
      CAN_EVENT_RECEIVE | CAN_EVENT_RX_BUF_OVERRUN | CAN_EVENT_RX_BUF_FULL;
  static constexpr uint8_t kTxInterruptMask =
      CAN_EVENT_TX_SECONDARY_BUF | CAN_EVENT_TX_PRIMARY_BUF;
  static constexpr uint8_t kErrorInterruptMask = CAN_EVENT_ERROR | CAN_EVENT_ABORT;
  static constexpr uint8_t kInterruptMask =
      kRxInterruptMask | kTxInterruptMask | kErrorInterruptMask;
  static constexpr uint8_t kCanErrorInterruptMask =
      CAN_ERROR_PASSIVE_INT_ENABLE | CAN_ERROR_ARBITRATION_LOST_INT_ENABLE |
      CAN_ERROR_BUS_ERROR_INT_ENABLE;
#else
  static constexpr uint8_t kMaxInstances = 1;
#endif

  /// @brief 构造 classic CAN 适配器 / Construct the classic CAN adapter.
  /// @param can HPM classic `CAN_Type` 实例。
  /// @param clock HPM SDK 时钟，用于 `clock_get_frequency()` 和 `can_init()`。
  /// @param index 外设索引，必须小于 `CAN_SOC_MAX_COUNT`。
  /// @param irq 外设 IRQ 号；`kInvalidIrq` 表示不自动打开 NVIC。
  /// @param auto_enable_irq 是否由适配器管理 IRQ flag 和 NVIC。
  /// @param tx_pool_size LibXR TX 队列深度，必须大于 0。
  HPMClassicCAN(LibXRHpmClassicCanType* can, clock_name_t clock, uint8_t index = 0,
                uint32_t irq = kInvalidIrq, bool auto_enable_irq = true,
                uint32_t tx_pool_size = kDefaultTxPoolSize);

  /// @brief 析构并关闭 IRQ/状态 / Destruct and shut down IRQ/state.
  ~HPMClassicCAN() override;

  /// @brief 配置 classic CAN 位时序和模式 / Configure bit timing and mode.
  /// @param cfg LibXR CAN 配置。
  /// @return `OK`、`PTR_NULL`、`ARG_ERR`、`NOT_SUPPORT` 或映射后的 SDK 状态。
  ErrorCode SetConfig(const CAN::Configuration& cfg) override;

  /// @brief 返回 CAN 外设输入时钟 / Return CAN peripheral input clock.
  /// @return 支持时返回 `clock_get_frequency(clock_)`，否则返回 0。
  uint32_t GetClockFreq() const override;

  /// @brief 将 classic CAN 帧加入发送队列 / Queue a classic CAN frame.
  /// @param pack 标准帧、扩展帧或远程帧；DLC 必须 <= 8。
  /// @return `OK`、`INIT_ERR`、`ARG_ERR`、`FULL` 或 `NOT_SUPPORT`。
  ErrorCode AddMessage(const ClassicPack& pack) override;

  /// @brief 读取 classic CAN 错误状态 / Read classic CAN error state.
  /// @param state 输出 LibXR 错误计数和 bus-off/passive/warning 状态。
  /// @return `OK`、`PTR_NULL` 或 `NOT_SUPPORT`。
  ErrorCode GetErrorState(CAN::ErrorState& state) const override;

  /// @brief 轮询 RX buffer 并分发 LibXR 回调 / Poll RX buffer.
  /// @param in_isr 标记回调是否来自 ISR 语境。
  void ProcessRx(bool in_isr = false);

  /// @brief 处理 classic CAN 中断标志 / Process interrupt flags.
  /// @param in_isr 标记当前调用是否来自 ISR。
  void ProcessInterrupt(bool in_isr = true);

  /// @brief C ISR trampoline 使用的按索引入口。
  /// @param index HPM CAN 实例索引。
  static void OnInterrupt(uint8_t index);

 private:
  static ErrorCode ConvertStatus(hpm_stat_t status);
  static ErrorCode ValidateConfig(const CAN::Configuration& cfg);
  void EmitErrorFrame(CAN::ErrorID error_id, bool in_isr);
  void Shutdown();
  static bool HasLowLevelTiming(const CAN::BitTiming& timing);
  static uint16_t SamplePointToPermille(float sample_point);
  void TxService();

#if LIBXR_HPM_CLASSIC_CAN_SUPPORTED
  static can_node_mode_t ConvertMode(const CAN::Mode& mode);
  static void ApplyLowLevelTiming(const CAN::BitTiming& src, can_bit_timing_param_t& dst);
  static void BuildTxFrame(const ClassicPack& pack, can_transmit_buf_t& frame);
  static bool BuildRxPack(const can_receive_buf_t& frame, ClassicPack& pack);
  static CAN::ErrorID ConvertProtocolError(uint8_t error_kind);
  void ProcessRxBuffer(bool in_isr);
  void ProcessError(bool in_isr);
#endif

  LibXRHpmClassicCanType* can_;
  clock_name_t clock_;
  uint8_t index_;
  uint32_t irq_;
  bool auto_enable_irq_;
  bool configured_ = false;

  LockFreePool<ClassicPack> tx_pool_;
  std::atomic<uint32_t> tx_lock_{0};
  std::atomic<uint32_t> tx_pend_{0};

#if LIBXR_HPM_CLASSIC_CAN_SUPPORTED
  static HPMClassicCAN* instance_map_[kMaxInstances];
#endif
};

}  // namespace LibXR

extern "C" void libxr_hpm_classic_can_process_interrupt(uint8_t index);

#endif
