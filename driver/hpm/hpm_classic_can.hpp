#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>

#include "can.hpp"
#include "hpm_clock_drv.h"
#include "hpm_soc.h"
#include "queue.hpp"

#if __has_include("hpm_can_drv.h") && __has_include("hpm_can_regs.h")
#include "hpm_can_drv.h"
using LibXRHpmClassicCanStatus = hpm_stat_t;
using LibXRHpmClassicCanMode = can_node_mode_t;
using LibXRHpmClassicCanTxMessage = can_transmit_buf_t;
using LibXRHpmClassicCanRxMessage = can_receive_buf_t;
#if defined(CAN_SOC_MAX_COUNT) && (CAN_SOC_MAX_COUNT > 0) && defined(HPM_CAN0)
#define LIBXR_HPM_CLASSIC_CAN_SUPPORTED 1
#else
#define LIBXR_HPM_CLASSIC_CAN_SUPPORTED 0
#endif
#else
#define LIBXR_HPM_CLASSIC_CAN_SUPPORTED 0
using CAN_Type = void;
using LibXRHpmClassicCanStatus = int;
enum LibXRHpmClassicCanMode : int
{
};
struct LibXRHpmClassicCanTxMessage
{
};
struct LibXRHpmClassicCanRxMessage
{
};
#endif

namespace LibXR
{

/**
 * @class HPMClassicCAN
 * @brief HPM classic CAN driver implementation.
 *
 * Wraps HPM SDK's `CAN_Type` controller as the LibXR classic CAN interface.
 * Board pin mux and transceiver standby pins are intentionally left to board code.
 */
class HPMClassicCAN final : public CAN
{
 public:
  static constexpr uint32_t INVALID_IRQ = 0xFFFFFFFFu;

  /**
   * @brief Construct one HPM classic CAN driver.
   * @param can CAN peripheral base.
   * @param clock CAN peripheral clock name.
   * @param irq IRQ number for this CAN instance. Pass `INVALID_IRQ` to skip PLIC setup.
   * @param queue_size TX software queue size.
   */
  HPMClassicCAN(CAN_Type* can, clock_name_t clock, uint32_t irq, uint32_t queue_size);
  ~HPMClassicCAN() override = default;

  ErrorCode SetConfig(const CAN::Configuration& cfg) override;
  uint32_t GetClockFreq() const override;
  ErrorCode AddMessage(const ClassicPack& pack) override;
  ErrorCode GetErrorState(CAN::ErrorState& state) const override;

  /** @brief Drain RX, service TX, and publish error frames for this CAN IRQ. */
  void ProcessInterrupt();

  /** @brief Enable the CAN IRQ in the HPM interrupt controller. */
  ErrorCode EnableInterrupt();

  /** @brief Disable the CAN IRQ in the HPM interrupt controller. */
  ErrorCode DisableInterrupt();

 private:
  static ErrorCode ConvertStatus(LibXRHpmClassicCanStatus status);
  static LibXRHpmClassicCanMode ConvertMode(const CAN::Mode& mode);
  static void BuildTxMessage(const ClassicPack& pack, LibXRHpmClassicCanTxMessage& message);
  static void BuildRxPack(const LibXRHpmClassicCanRxMessage& message, ClassicPack& pack);
  static ErrorID ConvertErrorKind(uint8_t kind);

  void EnableCanInterrupts();
  void DisableCanInterrupts();
  void TxService();
  void ProcessRx();
  void ProcessTx();
  void ProcessError(uint8_t error_flags, uint8_t last_error_kind);

  CAN_Type* can_;
  clock_name_t clock_;
  uint32_t irq_;

  MPMCQueue<ClassicPack> tx_queue_;
  bool tx_retry_valid_{false};
  ClassicPack tx_retry_pack_{};

  std::atomic<uint32_t> tx_lock_{0};
  std::atomic<uint32_t> tx_pend_{0};
};

}  // namespace LibXR
