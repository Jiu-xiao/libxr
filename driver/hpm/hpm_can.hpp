#pragma once

#include "hpm_clock_drv.h"
#include "hpm_soc.h"

#if __has_include("hpm_mcan_drv.h") && __has_include("hpm_mcan_regs.h") && \
                                                     __has_include("hpm_mcan_soc.h")
#include "hpm_mcan_drv.h"
#if ((defined(MCAN_SOC_MAX_COUNT) && (MCAN_SOC_MAX_COUNT > 0)) || \
     (defined(CAN_SOC_MAX_COUNT) && (CAN_SOC_MAX_COUNT > 0))) &&  \
    defined(HPM_MCAN0)
#define LIBXR_HPM_CAN_SUPPORTED 1
#else
#define LIBXR_HPM_CAN_SUPPORTED 0
#endif
#else
#define LIBXR_HPM_CAN_SUPPORTED 0
#endif

#if LIBXR_HPM_CAN_SUPPORTED

#include <atomic>
#include <cstdint>
#include <cstring>

#include "can.hpp"
#include "queue.hpp"

namespace LibXR
{

/**
 * @class HPMCAN
 * @brief HPM CAN driver implementation for classic LibXR CAN frames.
 *
 * This class intentionally exposes the classic `CAN` interface first. CAN FD can be
 * layered on top of the same HPM SDK primitives later through LibXR::FDCAN.
 */
class HPMCAN final : public CAN
{
 public:
  static constexpr uint32_t INVALID_IRQ = 0xFFFFFFFFu;

  /**
   * @brief Construct one HPM CAN driver.
   * @param can CAN peripheral base.
   * @param clock CAN peripheral clock name.
   * @param irq IRQ number for this CAN instance. Pass `INVALID_IRQ` to skip PLIC setup.
   * @param queue_size TX software queue size.
   */
  HPMCAN(MCAN_Type* can, clock_name_t clock, uint32_t irq, uint32_t queue_size);

  /**
   * @brief Construct one HPM CAN driver with message RAM.
   * @param msg_buf Message RAM buffer base. HPM5E31 requires this in `.ahb_sram`.
   * @param msg_buf_size Message RAM buffer size in bytes.
   */
  HPMCAN(MCAN_Type* can, clock_name_t clock, uint32_t irq, uint32_t queue_size,
          void* msg_buf, uint32_t msg_buf_size);
  ~HPMCAN() override = default;

  ErrorCode SetConfig(const CAN::Configuration& cfg) override;
  uint32_t GetClockFreq() const override;
  ErrorCode AddMessage(const ClassicPack& pack) override;
  ErrorCode GetErrorState(CAN::ErrorState& state) const override;

  /** @brief Drain RX FIFOs, service TX, and publish error frames for this CAN IRQ. */
  void ProcessInterrupt();

  /** @brief Enable the CAN IRQ in the HPM interrupt controller. */
  ErrorCode EnableInterrupt();

  /** @brief Disable the CAN IRQ in the HPM interrupt controller. */
  ErrorCode DisableInterrupt();

  /**
   * @brief Set message RAM before configuration.
   *
   * HPM5E31/HPM5E00 place CAN message RAM in AHB SRAM. Board code should pass
   * a buffer placed in `.ahb_sram`, usually `MCAN_MSG_BUF_SIZE_IN_WORDS` words.
   */
  ErrorCode SetMessageBuffer(void* msg_buf, uint32_t msg_buf_size);

 private:
  static ErrorCode ConvertStatus(hpm_stat_t status);
  static mcan_node_mode_t ConvertMode(const CAN::Mode& mode);
  static void BuildTxFrame(const ClassicPack& pack, mcan_tx_frame_t& frame);
  static void BuildRxPack(const mcan_rx_message_t& frame, ClassicPack& pack);
  static ErrorID ConvertLastError(uint8_t last_error);

  void EnableCanInterrupts();
  void DisableCanInterrupts();
  ErrorCode ApplyMessageBuffer();
  void TxService();
  void ProcessRxFifo(uint32_t fifo_index);
  void ProcessTx();
  void ProcessError();

  MCAN_Type* can_;
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

#endif
