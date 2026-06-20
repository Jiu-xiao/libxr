#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>

#include "can.hpp"
#include "hpm_clock_drv.h"
#include "hpm_soc.h"
#include "queue.hpp"

#if __has_include("hpm_mcan_drv.h") && __has_include("hpm_mcan_regs.h") && \
                                                     __has_include("hpm_mcan_soc.h")
#include "hpm_mcan_drv.h"
using LibXRHpmMcanStatus = hpm_stat_t;
using LibXRHpmMcanMode = mcan_node_mode_t;
using LibXRHpmMcanTxFrame = mcan_tx_frame_t;
using LibXRHpmMcanRxFrame = mcan_rx_message_t;
#if ((defined(MCAN_SOC_MAX_COUNT) && (MCAN_SOC_MAX_COUNT > 0)) || \
     (defined(CAN_SOC_MAX_COUNT) && (CAN_SOC_MAX_COUNT > 0))) &&  \
    defined(HPM_MCAN0)
#define LIBXR_HPM_MCAN_SUPPORTED 1
#else
#define LIBXR_HPM_MCAN_SUPPORTED 0
#endif
#else
#define LIBXR_HPM_MCAN_SUPPORTED 0
using MCAN_Type = void;
using LibXRHpmMcanStatus = int;
enum LibXRHpmMcanMode : int
{
};
struct LibXRHpmMcanTxFrame
{
};
struct LibXRHpmMcanRxFrame
{
};
#endif

namespace LibXR
{

/**
 * @class HPMMCAN
 * @brief HPM Bosch MCAN IP driver implementation for classic LibXR CAN frames.
 *
 * This class intentionally exposes the classic `CAN` interface first. CAN FD can be
 * layered on top of the same HPM SDK primitives later through LibXR::FDCAN.
 */
class HPMMCAN final : public CAN
{
 public:
  static constexpr uint32_t INVALID_IRQ = 0xFFFFFFFFu;

  /**
   * @brief Construct one HPM MCAN driver.
   * @param mcan MCAN peripheral base.
   * @param clock MCAN peripheral clock name.
   * @param irq IRQ number for this MCAN instance. Pass `INVALID_IRQ` to skip PLIC setup.
   * @param queue_size TX software queue size.
   */
  HPMMCAN(MCAN_Type* mcan, clock_name_t clock, uint32_t irq, uint32_t queue_size);

  /**
   * @brief Construct one HPM MCAN driver with message RAM.
   * @param msg_buf Message RAM buffer base. HPM5E31 requires this in `.ahb_sram`.
   * @param msg_buf_size Message RAM buffer size in bytes.
   */
  HPMMCAN(MCAN_Type* mcan, clock_name_t clock, uint32_t irq, uint32_t queue_size,
          void* msg_buf, uint32_t msg_buf_size);
  ~HPMMCAN() override = default;

  ErrorCode SetConfig(const CAN::Configuration& cfg) override;
  uint32_t GetClockFreq() const override;
  ErrorCode AddMessage(const ClassicPack& pack) override;
  ErrorCode GetErrorState(CAN::ErrorState& state) const override;

  /** @brief Drain RX FIFOs, service TX, and publish error frames for this MCAN IRQ. */
  void ProcessInterrupt();

  /** @brief Enable the MCAN IRQ in the HPM interrupt controller. */
  ErrorCode EnableInterrupt();

  /** @brief Disable the MCAN IRQ in the HPM interrupt controller. */
  ErrorCode DisableInterrupt();

  /**
   * @brief Set message RAM before configuration.
   *
   * HPM5E31/HPM5E00 place MCAN message RAM in AHB SRAM. Board code should pass
   * a buffer placed in `.ahb_sram`, usually `MCAN_MSG_BUF_SIZE_IN_WORDS` words.
   */
  ErrorCode SetMessageBuffer(void* msg_buf, uint32_t msg_buf_size);

 private:
  static ErrorCode ConvertStatus(LibXRHpmMcanStatus status);
  static LibXRHpmMcanMode ConvertMode(const CAN::Mode& mode);
  static void BuildTxFrame(const ClassicPack& pack, LibXRHpmMcanTxFrame& frame);
  static void BuildRxPack(const LibXRHpmMcanRxFrame& frame, ClassicPack& pack);
  static ErrorID ConvertLastError(uint8_t last_error);

  void EnableMcanInterrupts();
  void DisableMcanInterrupts();
  ErrorCode ApplyMessageBuffer();
  void TxService();
  void ProcessRxFifo(uint32_t fifo_index);
  void ProcessTx();
  void ProcessError();

  MCAN_Type* mcan_;
  clock_name_t clock_;
  uint32_t irq_;
  void* msg_buf_{nullptr};
  uint32_t msg_buf_size_{0};

  MPMCQueue<ClassicPack> tx_queue_;
  bool tx_retry_valid_{false};
  ClassicPack tx_retry_pack_{};

  std::atomic<uint32_t> tx_lock_{0};
  std::atomic<uint32_t> tx_pend_{0};
};

}  // namespace LibXR
