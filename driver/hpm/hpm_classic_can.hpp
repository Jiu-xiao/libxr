/// @file hpm_classic_can.hpp
/// @brief Adapter header for the HPM classic `CAN_Type` IP.
///
/// This file targets non-MCAN classic CAN peripherals. MCAN based classic CAN uses
/// `HPMCAN` from `hpm_can.*`; MCAN FD uses `HPMCANFD` from `hpm_mcan.*`.
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
/// @brief `LibXR::CAN` adapter for the HPM classic `CAN_Type` IP.
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

  /// @brief Construct the classic CAN adapter.
  /// @param can HPM classic `CAN_Type` instance.
  /// @param clock HPM SDK clock used by `clock_get_frequency()` and `can_init()`.
  /// @param index Peripheral index; must be lower than `CAN_SOC_MAX_COUNT`.
  /// @param irq Peripheral IRQ number, or `kInvalidIrq` to skip NVIC enable.
  /// @param auto_enable_irq Whether the adapter manages IRQ flags and NVIC enable.
  /// @param tx_pool_size LibXR TX queue depth; must be greater than 0.
  HPMClassicCAN(LibXRHpmClassicCanType* can, clock_name_t clock, uint8_t index = 0,
                uint32_t irq = kInvalidIrq, bool auto_enable_irq = true,
                uint32_t tx_pool_size = kDefaultTxPoolSize);

  /// @brief Destruct and shut down IRQ/state.
  ~HPMClassicCAN() override;

  /// @brief Configure classic CAN bit timing and mode.
  /// @param cfg LibXR CAN configuration.
  /// @return `OK`, `PTR_NULL`, `ARG_ERR`, `NOT_SUPPORT`, or mapped SDK status.
  ErrorCode SetConfig(const CAN::Configuration& cfg) override;

  /// @brief Return the CAN peripheral input clock.
  /// @return `clock_get_frequency(clock_)` when supported, otherwise 0.
  uint32_t GetClockFreq() const override;

  /// @brief Queue a classic CAN frame for transmission.
  /// @param pack Standard, extended, or remote classic CAN frame; DLC must be <= 8.
  /// @return `OK`, `INIT_ERR`, `ARG_ERR`, `FULL`, or `NOT_SUPPORT`.
  ErrorCode AddMessage(const ClassicPack& pack) override;

  /// @brief Read classic CAN error state.
  /// @param state Output LibXR error counters and bus-off/passive/warning state.
  /// @return `OK`, `PTR_NULL`, or `NOT_SUPPORT`.
  ErrorCode GetErrorState(CAN::ErrorState& state) const override;

  /// @brief Poll the RX buffer and dispatch LibXR callbacks.
  /// @param in_isr Marks whether callbacks are emitted from ISR context.
  void ProcessRx(bool in_isr = false);

  /// @brief Process classic CAN interrupt flags.
  /// @param in_isr Marks whether the call is from ISR context.
  void ProcessInterrupt(bool in_isr = true);

  /// @brief Indexed entry used by the C ISR trampoline.
  /// @param index HPM CAN instance index.
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
