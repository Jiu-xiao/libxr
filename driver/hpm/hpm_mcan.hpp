/**
 * @file hpm_mcan.hpp
 * @brief HPM MCAN IP 适配头文件 / Adapter header for HPM MCAN IP.
 *
 * @details
 * 本文件按底层外设 IP 归档，对应 `MCAN_Type`。文件中同时提供 `HPMCAN` 与
 * `HPMCANFD`：前者导出 `LibXR::CAN`，后者导出 `LibXR::FDCAN`。两者共用同一套
 * MCAN 外设模型，但同一个 MCAN instance 不允许共存。
 * This file is grouped by the low-level peripheral IP and targets `MCAN_Type`.
 * It provides both `HPMCAN` and `HPMCANFD`: the former exports `LibXR::CAN`, and the
 * latter exports `LibXR::FDCAN`. They share the same MCAN peripheral model, but they
 * are not allowed to coexist on the same MCAN instance.
 */
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "can.hpp"
#include "hpm_clock_drv.h"
#include "hpm_common.h"
#include "hpm_soc.h"
#include "queue.hpp"

#if defined(MCAN_SOC_MAX_COUNT) && (MCAN_SOC_MAX_COUNT > 0)
#include "hpm_mcan_drv.h"
#define LIBXR_HPM_MCAN_CORE_SUPPORTED 1
#define LIBXR_HPM_MCAN_SUPPORTED 1
using LibXRHpmCanType = MCAN_Type;
using LibXRHpmCanFdType = MCAN_Type;
#else
#define LIBXR_HPM_MCAN_CORE_SUPPORTED 0
#define LIBXR_HPM_MCAN_SUPPORTED 0
using MCAN_Type = void;
using mcan_last_err_code_t = int;
using mcan_node_mode_t = int;
typedef struct
{
  uint16_t prescaler;
  uint16_t num_seg1;
  uint16_t num_seg2;
  uint8_t num_sjw;
  bool enable_tdc;
} mcan_bit_timing_param_t;
typedef struct
{
  uint32_t ext_id;
  uint32_t std_id;
  uint32_t rtr;
  uint32_t use_ext_id;
  uint32_t error_state_indicator;
  uint32_t dlc;
  uint32_t bitrate_switch;
  uint32_t canfd_frame;
  uint8_t data_8[64];
} mcan_tx_frame_t;
typedef struct
{
  uint32_t ext_id;
  uint32_t std_id;
  uint32_t rtr;
  uint32_t use_ext_id;
  uint32_t dlc;
  uint32_t bitrate_switch;
  uint32_t canfd_frame;
  uint8_t data_8[64];
} mcan_rx_message_t;
using LibXRHpmCanType = void;
using LibXRHpmCanFdType = void;
#endif

namespace LibXR
{

namespace detail
{

enum class HpmMcanOwnerKind : uint8_t
{
  NONE = 0,
  CAN,
  CANFD
};

struct HpmMcanSharedOwner
{
  HpmMcanOwnerKind kind = HpmMcanOwnerKind::NONE;
  void* owner = nullptr;
};

template <typename Owner, uint8_t InstanceCount>
class HpmMcanInstanceRegistry
{
 public:
  static void Register(uint8_t index, Owner* owner)
  {
    ASSERT(index < InstanceCount);
    ASSERT(map_[index] == nullptr);
    map_[index] = owner;
  }

  static void Unregister(uint8_t index, Owner* owner)
  {
    if (index < InstanceCount && map_[index] == owner)
    {
      map_[index] = nullptr;
    }
  }

  static Owner* Get(uint8_t index)
  {
    if (index >= InstanceCount)
    {
      return nullptr;
    }
    return map_[index];
  }

 private:
  inline static Owner* map_[InstanceCount] = {};
};

#if LIBXR_HPM_MCAN_CORE_SUPPORTED
inline constexpr uint32_t kMcanRxInterruptMask =
    MCAN_INT_RXFIFO0_NEW_MSG | MCAN_INT_RXFIFO1_NEW_MSG;
inline constexpr uint32_t kMcanTxInterruptMask =
    MCAN_EVENT_TRANSMIT | MCAN_INT_TXFIFO_EMPTY;
inline constexpr uint32_t kMcanErrorInterruptMask = MCAN_EVENT_ERROR;
inline constexpr uint32_t kMcanInterruptMask =
    kMcanRxInterruptMask | kMcanTxInterruptMask | kMcanErrorInterruptMask;
inline constexpr uint32_t kMcanRxFaultMask =
    MCAN_INT_RXFIFO0_FULL | MCAN_INT_RXFIFO1_FULL | MCAN_INT_RXFIFO0_MSG_LOST |
    MCAN_INT_RXFIFO1_MSG_LOST | MCAN_INT_MSG_RAM_ACCESS_FAILURE;
inline constexpr uint32_t kMcanRxFifo0ActivityMask =
    MCAN_INT_RXFIFO0_NEW_MSG | MCAN_INT_RXFIFO0_FULL | MCAN_INT_RXFIFO0_MSG_LOST;
inline constexpr uint32_t kMcanRxFifo1ActivityMask =
    MCAN_INT_RXFIFO1_NEW_MSG | MCAN_INT_RXFIFO1_FULL | MCAN_INT_RXFIFO1_MSG_LOST;

ErrorCode ConvertMcanStatus(hpm_stat_t status);
bool HasLowLevelTiming(const CAN::BitTiming& timing);
bool HasLowLevelTiming(const FDCAN::DataBitTiming& timing);
uint16_t SamplePointToPermille(float sample_point);
mcan_node_mode_t ConvertMcanMode(const CAN::Mode& mode);
void ApplyLowLevelTiming(const CAN::BitTiming& src, mcan_bit_timing_param_t& dst);
void ApplyLowLevelTiming(const FDCAN::DataBitTiming& src, mcan_bit_timing_param_t& dst);
CAN::ErrorID ConvertMcanProtocolError(mcan_last_err_code_t code);
uint32_t AcquireMcanClock(clock_name_t clock);
void PrepareMcanCommonConfig(mcan_config_t& config, bool enable_canfd);
void PrepareMcanAcceptAllFilters(mcan_config_t& config);
void ShutdownMcan(MCAN_Type* can, uint32_t irq, bool auto_enable_irq,
                  uint32_t interrupt_mask);
void EnableMcanInterrupts(MCAN_Type* can, uint32_t irq, bool auto_enable_irq,
                          uint32_t interrupt_mask);
ErrorCode ReadMcanErrorState(MCAN_Type* can, CAN::ErrorState& state);
size_t HardwareTxQueueEmptySize(MCAN_Type* can);
bool AcquireSharedMcanOwnership(uint8_t index, void* owner, HpmMcanOwnerKind kind);
void ReleaseSharedMcanOwnership(uint8_t index, void* owner, HpmMcanOwnerKind kind);

template <typename FrameConsumer, typename ErrorConsumer>
void DrainMcanRxFifo(MCAN_Type* can, uint32_t fifo_index, FrameConsumer&& on_frame,
                     ErrorConsumer&& on_error)
{
  while (true)
  {
    mcan_rx_message_t frame{};
    const hpm_stat_t status = mcan_read_rxfifo(can, fifo_index, &frame);
    if (status == status_mcan_rxfifo_empty)
    {
      break;
    }
    if (status != status_success)
    {
      on_error();
      break;
    }
    on_frame(frame);
  }
}

template <typename RxHandler, typename ErrorEmitter, typename TxHandler,
          typename ErrorHandler>
void ProcessMcanInterrupt(MCAN_Type* can, bool configured, bool in_isr,
                          RxHandler&& on_rx_fifo, ErrorEmitter&& emit_other_error,
                          TxHandler&& on_tx, ErrorHandler&& on_error)
{
  if (!configured || can == nullptr)
  {
    return;
  }

  const uint32_t flags = mcan_get_interrupt_flags(can);
  if (flags == 0U)
  {
    return;
  }

  mcan_clear_interrupt_flags(can, flags);

  if ((flags & kMcanRxFifo0ActivityMask) != 0U)
  {
    on_rx_fifo(0U, flags, in_isr);
  }
  if ((flags & kMcanRxFifo1ActivityMask) != 0U)
  {
    on_rx_fifo(1U, flags, in_isr);
  }
  if ((flags & kMcanRxFaultMask) != 0U)
  {
    emit_other_error(in_isr);
  }
  if ((flags & kMcanTxInterruptMask) != 0U)
  {
    on_tx();
  }
  if ((flags & kMcanErrorInterruptMask) != 0U)
  {
    on_error(flags & kMcanErrorInterruptMask, in_isr);
    on_tx();
  }
}
#endif

}  // namespace detail

#if LIBXR_HPM_MCAN_CORE_SUPPORTED
/**
 * @class HPMCAN
 * @brief HPM MCAN 经典 CAN 适配器 / HPM MCAN classic CAN adapter.
 *
 * @details
 * 该类在 `MCAN_Type` 上实现 `LibXR::CAN`，只承接 classic frame 语义。它与同文件中的
 * `HPMCANFD` 共享同一套 MCAN 外设模型，但同一个 MCAN instance 上不允许共存。
 * This class implements `LibXR::CAN` on top of `MCAN_Type` and only exports classic
 * frame semantics. It shares the same MCAN peripheral model with `HPMCANFD` in the
 * same file, but both wrappers are not allowed to coexist on the same MCAN instance.
 */
class HPMCAN : public CAN
{
 public:
  static constexpr uint32_t kInvalidIrq = 0xFFFFFFFFu;
  static constexpr uint32_t kDefaultTxPoolSize = 8;
  static constexpr uint8_t kMaxInstances = MCAN_SOC_MAX_COUNT;
  using McanRegistry = detail::HpmMcanInstanceRegistry<HPMCAN, kMaxInstances>;

  HPMCAN(LibXRHpmCanType* can, clock_name_t clock, uint8_t index = 0,
         uint32_t irq = kInvalidIrq, bool auto_enable_irq = true,
         uint32_t tx_pool_size = kDefaultTxPoolSize);
  ~HPMCAN() override;

  ErrorCode SetConfig(const CAN::Configuration& cfg) override;
  uint32_t GetClockFreq() const override;
  ErrorCode AddMessage(const ClassicPack& pack) override;
  ErrorCode GetErrorState(CAN::ErrorState& state) const override;

  void ProcessRx(bool in_isr = false);
  void ProcessInterrupt(bool in_isr = true);
  static void OnInterrupt(uint8_t index);

 private:
  static ErrorCode ConvertStatus(hpm_stat_t status);
  static ErrorCode ValidateConfig(const CAN::Configuration& cfg);
  void EmitErrorFrame(CAN::ErrorID error_id, bool in_isr);
  void Shutdown();

  static bool HasLowLevelTiming(const CAN::BitTiming& timing);
  static uint16_t SamplePointToPermille(float sample_point);
  void TxService();

  static void BuildTxFrame(const ClassicPack& pack, mcan_tx_frame_t& frame);
  static bool BuildRxPack(const mcan_rx_message_t& frame, ClassicPack& pack);
  void ProcessRxFifo(uint32_t fifo_index, bool in_isr);
  void ProcessError(bool in_isr);

  LibXRHpmCanType* can_;
  clock_name_t clock_;
  uint8_t index_;
  uint32_t irq_;
  bool auto_enable_irq_;
  bool ownership_acquired_ = false;
  bool configured_ = false;

  ClassicPack pending_tx_{};
  bool pending_tx_valid_ = false;
  MPMCQueue<ClassicPack> tx_pool_;
  std::atomic<uint32_t> tx_lock_{0};
  std::atomic<uint32_t> tx_pend_{0};
};
#endif

/**
 * @class HPMCANFD
 * @brief HPM MCAN FDCAN 驱动适配器，适配 LibXR FDCAN 接口 /
 * HPM MCAN FDCAN adapter for the LibXR FDCAN interface.
 *
 * @details
 * 该类在 `MCAN_Type` 上实现 `LibXR::FDCAN`，在 classic frame 之外还支持 FD frame、
 * BRS、ESI、TDC 等 FDCAN 语义。它与同文件中的 `HPMCAN` 共享同一套 MCAN 外设模型，
 * 但同一个 MCAN instance 上不允许共存。
 * This class implements `LibXR::FDCAN` on top of `MCAN_Type` and supports FD frame,
 * BRS, ESI, TDC, and related FDCAN semantics in addition to classic frames. It shares
 * the same MCAN peripheral model with `HPMCAN` in the same file, but both wrappers are
 * not allowed to coexist on the same MCAN instance.
 */
class HPMCANFD : public FDCAN
{
 public:
  static constexpr uint32_t kInvalidIrq = 0xFFFFFFFFu;
  static constexpr uint32_t kDefaultTxPoolSize = 8;

#if LIBXR_HPM_MCAN_SUPPORTED
  static constexpr uint8_t kMaxInstances = MCAN_SOC_MAX_COUNT;
  using McanRegistry = detail::HpmMcanInstanceRegistry<HPMCANFD, kMaxInstances>;
#else
  static constexpr uint8_t kMaxInstances = 1;
#endif

  HPMCANFD(LibXRHpmCanFdType* can, clock_name_t clock, uint8_t index = 0,
           uint32_t irq = kInvalidIrq, bool auto_enable_irq = true,
           uint32_t queue_size = kDefaultTxPoolSize);
  ~HPMCANFD() override;

  ErrorCode Init(void);

  ErrorCode SetConfig(const CAN::Configuration& cfg) override;
  ErrorCode SetConfig(const FDCAN::Configuration& cfg) override;
  uint32_t GetClockFreq() const override;
  ErrorCode AddMessage(const ClassicPack& pack) override;
  ErrorCode AddMessage(const FDPack& pack) override;
  ErrorCode GetErrorState(CAN::ErrorState& state) const override;

  size_t HardwareTxQueueEmptySize() const;
  void ProcessRxInterrupt(uint32_t fifo);
  void ProcessErrorStatusInterrupt(uint32_t error_status_its);
  void ProcessInterrupt(bool in_isr = true);
  static void OnInterrupt(uint8_t index);

  static inline void BuildTxFrame(const ClassicPack& pack, mcan_tx_frame_t& frame);
  static inline void BuildTxFrame(const FDPack& pack, mcan_tx_frame_t& frame);
  void TxService();

 private:
  static ErrorCode ConvertStatus(hpm_stat_t status);
  static bool HasLowLevelTiming(const CAN::BitTiming& timing);
  static bool HasLowLevelTiming(const FDCAN::DataBitTiming& timing);
  static uint16_t SamplePointToPermilleX10(float sample_point);
  static uint8_t DlcToBytes(uint8_t dlc);
  static uint8_t BytesToDlc(uint8_t bytes);
  void Shutdown();
  static bool BuildRxPack(const mcan_rx_message_t& frame, ClassicPack& pack);
  static bool BuildRxPack(const mcan_rx_message_t& frame, FDPack& pack);
  static CAN::ErrorID ConvertProtocolError(mcan_last_err_code_t code);
  void EmitErrorFrame(CAN::ErrorID error_id, bool in_isr);

  LibXRHpmCanFdType* can_;
  clock_name_t clock_;
  uint8_t index_;
  uint32_t irq_;
  bool auto_enable_irq_;
  bool ownership_acquired_ = false;
  bool configured_ = false;
  bool fd_enabled_ = false;
  bool brs_enabled_ = false;
  bool esi_enabled_ = false;

  std::atomic<uint32_t> tx_lock_{0};
  std::atomic<uint32_t> tx_pend_{0};

  ClassicPack pending_tx_{};
  bool pending_tx_valid_ = false;
  MPMCQueue<ClassicPack> tx_pool_;
  FDPack pending_fd_{};
  bool pending_fd_valid_ = false;
  MPMCQueue<FDPack> tx_pool_fd_;

  struct
  {
    mcan_rx_message_t frame;
    ClassicPack pack;
    FDPack pack_fd;
  } rx_buff_;

  struct
  {
    mcan_tx_frame_t frame;
  } tx_buff_;
};

}  // namespace LibXR

extern "C" void libxr_hpm_can_process_interrupt(uint8_t index);
extern "C" void libxr_hpm_mcan_process_interrupt(uint8_t index);
