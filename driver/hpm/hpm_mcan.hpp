/// @file hpm_mcan.hpp
/// @brief Adapter header for HPM MCAN IP.
///
/// This file adds `HPMCANFD`, the `LibXR::FDCAN` adapter for Bosch MCAN IP.
/// The MCAN classic CAN adapter remains `HPMCAN` in `hpm_can.*`.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "can.hpp"
#include "hpm_clock_drv.h"
#include "hpm_common.h"
#include "hpm_soc.h"
#include "lockfree_pool.hpp"

#if defined(HPMSOC_HAS_HPMSDK_MCAN) && __has_include("hpm_mcan_drv.h") &&           \
                                                     defined(MCAN_SOC_MAX_COUNT) && \
                                                     (MCAN_SOC_MAX_COUNT > 0)
#include "hpm_mcan_drv.h"
#define LIBXR_HPM_MCAN_CORE_SUPPORTED 1
#define LIBXR_HPM_MCAN_SUPPORTED 1
using LibXRHpmCanType = MCAN_Type;
using LibXRHpmCanFdType = MCAN_Type;
#else
#define LIBXR_HPM_MCAN_CORE_SUPPORTED 0
#define LIBXR_HPM_MCAN_SUPPORTED 0
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
void PrepareMcanCommonConfig(MCAN_Type* can, mcan_config_t& config, bool enable_canfd);
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

/// @class HPMCANFD
/// @brief `LibXR::FDCAN` adapter for HPM MCAN IP.
///
/// Supports classic and FD frames, BRS, ESI, and TDC. Do not share one MCAN
/// instance between this adapter and `HPMCAN`.
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

  /// @brief Construct the MCAN FDCAN wrapper.
  /// @param can HPM `MCAN_Type` instance.
  /// @param clock HPM SDK clock used by `clock_get_frequency()` and `mcan_init()`.
  /// @param index MCAN instance index; must be lower than `MCAN_SOC_MAX_COUNT`.
  /// @param irq Peripheral IRQ number, or `kInvalidIrq` to skip NVIC enable.
  /// @param auto_enable_irq Whether the adapter manages IRQ flags and NVIC enable.
  /// @param queue_size Classic and FD software TX queue depth; must be greater than 0.
  HPMCANFD(LibXRHpmCanFdType* can, clock_name_t clock, uint8_t index = 0,
           uint32_t irq = kInvalidIrq, bool auto_enable_irq = true,
           uint32_t queue_size = kDefaultTxPoolSize, void* msg_buf = nullptr,
           uint32_t msg_buf_size = 0);

  /// @brief Destruct and release MCAN/FDCAN state.
  ~HPMCANFD() override;

  /// @brief Perform the lightweight initialization check.
  /// @return `OK`, `PTR_NULL`, or `NOT_SUPPORT`.
  ErrorCode Init(void);

  /**
   * @brief Set MCAN message RAM before configuration.
   *
   * The buffer should be placed in `.ahb_sram` on HPM SoCs whose MCAN message
   * RAM is backed by AHB RAM.
   */
  ErrorCode SetMessageBuffer(void* msg_buf, uint32_t msg_buf_size);

  /// @brief Configure the MCAN/FDCAN wrapper with classic CAN settings.
  /// @param cfg Classic CAN configuration promoted to FDCAN with FD disabled.
  /// @return Result of `SetConfig(const FDCAN::Configuration&)`.
  ErrorCode SetConfig(const CAN::Configuration& cfg) override;

  /// @brief Configure MCAN classic/FD operating parameters.
  /// @param cfg LibXR FDCAN configuration.
  /// @return `OK`, `PTR_NULL`, `ARG_ERR`, `NOT_SUPPORT`, or mapped SDK status.
  ErrorCode SetConfig(const FDCAN::Configuration& cfg) override;

  /// @brief Return the MCAN peripheral input clock.
  /// @return `clock_get_frequency(clock_)` when supported, otherwise 0.
  uint32_t GetClockFreq() const override;

  /// @brief Queue a classic CAN frame.
  /// @param pack Classic CAN data frame; DLC must be <= 8.
  /// @return `OK`, `INIT_ERR`, `ARG_ERR`, `FULL`, or `NOT_SUPPORT`.
  ErrorCode AddMessage(const ClassicPack& pack) override;

  /// @brief Queue an FD frame.
  /// @param pack Standard or extended FD data frame; length must be <= 64.
  /// @return `OK`, `INIT_ERR`, `NOT_SUPPORT`, `ARG_ERR`, or `FULL`.
  ErrorCode AddMessage(const FDPack& pack) override;

  /// @brief Read MCAN error state.
  /// @param state Output LibXR error counters and bus-off/passive/warning state.
  /// @return `OK`, `PTR_NULL`, or `NOT_SUPPORT`.
  ErrorCode GetErrorState(CAN::ErrorState& state) const override;

  /// @brief Query free slots in the hardware TX FIFO.
  /// @return Free hardware TX FIFO slots, or 0 when unsupported.
  size_t HardwareTxQueueEmptySize() const;

  /// @brief Process receive interrupts for one RX FIFO.
  /// @param fifo RX FIFO index, usually 0 or 1.
  void ProcessRxInterrupt(uint32_t fifo);

  /// @brief Process MCAN error/status interrupts.
  /// @param error_status_its MCAN error/status flags hit by this interrupt.
  void ProcessErrorStatusInterrupt(uint32_t error_status_its);

  /// @brief Process combined MCAN interrupts.
  /// @param in_isr Marks whether the call is from ISR context.
  void ProcessInterrupt(bool in_isr = true);

  /// @brief Indexed entry used by the C ISR trampoline.
  /// @param index MCAN instance index.
  static void OnInterrupt(uint8_t index);

  /// @brief Convert a LibXR classic frame to HPM `mcan_tx_frame_t`.
  /// @param pack Input classic frame.
  /// @param frame Output HPM MCAN TX frame.
  static inline void BuildTxFrame(const ClassicPack& pack, mcan_tx_frame_t& frame);

  /// @brief Convert a LibXR FD frame to HPM `mcan_tx_frame_t`.
  /// @param pack Input FD frame.
  /// @param frame Output HPM MCAN TX frame.
  static inline void BuildTxFrame(const FDPack& pack, mcan_tx_frame_t& frame);

  /// @brief Service software TX queues into the MCAN TX FIFO.
  void TxService();

 private:
  static ErrorCode ConvertStatus(hpm_stat_t status);
  static bool HasLowLevelTiming(const CAN::BitTiming& timing);
  static bool HasLowLevelTiming(const FDCAN::DataBitTiming& timing);
  static uint16_t SamplePointToPermilleX10(float sample_point);
  static uint8_t DlcToBytes(uint8_t dlc);
  static uint8_t BytesToDlc(uint8_t bytes);
  ErrorCode ApplyMessageBuffer();
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
  void* msg_buf_{nullptr};
  uint32_t msg_buf_size_{0};
  bool ownership_acquired_ = false;
  bool configured_ = false;
  bool fd_enabled_ = false;
  bool brs_enabled_ = false;
  bool esi_enabled_ = false;

  std::atomic<uint32_t> tx_lock_{0};
  std::atomic<uint32_t> tx_pend_{0};

  LockFreePool<ClassicPack> tx_pool_;
  LockFreePool<FDPack> tx_pool_fd_;

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

extern "C" void libxr_hpm_mcan_process_interrupt(uint8_t index);
