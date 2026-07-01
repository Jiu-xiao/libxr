#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

#include "can.hpp"
#include "hpm_clock_drv.h"
#include "hpm_common.h"
#include "hpm_soc.h"

#if defined(HPMSOC_HAS_HPMSDK_MCAN) && __has_include("hpm_mcan_drv.h") &&           \
                                                     defined(MCAN_SOC_MAX_COUNT) && \
                                                     (MCAN_SOC_MAX_COUNT > 0)
#include "hpm_mcan_drv.h"
#define LIBXR_HPM_MCAN_SUPPORTED 1
#else
#define LIBXR_HPM_MCAN_SUPPORTED 0
#endif

#if LIBXR_HPM_MCAN_SUPPORTED

namespace LibXR::detail
{

inline constexpr uint32_t MCAN_RX_INTERRUPT_MASK =
    MCAN_INT_RXFIFO0_NEW_MSG | MCAN_INT_RXFIFO1_NEW_MSG;
inline constexpr uint32_t MCAN_TX_INTERRUPT_MASK =
    MCAN_EVENT_TRANSMIT | MCAN_INT_TXFIFO_EMPTY;
inline constexpr uint32_t MCAN_ERROR_INTERRUPT_MASK = MCAN_EVENT_ERROR;
inline constexpr uint32_t MCAN_INTERRUPT_MASK =
    MCAN_RX_INTERRUPT_MASK | MCAN_TX_INTERRUPT_MASK | MCAN_ERROR_INTERRUPT_MASK;
inline constexpr uint32_t MCAN_RX_FAULT_MASK =
    MCAN_INT_RXFIFO0_FULL | MCAN_INT_RXFIFO1_FULL | MCAN_INT_RXFIFO0_MSG_LOST |
    MCAN_INT_RXFIFO1_MSG_LOST | MCAN_INT_MSG_RAM_ACCESS_FAILURE;
inline constexpr uint32_t MCAN_RX_FIFO0_ACTIVITY_MASK =
    MCAN_INT_RXFIFO0_NEW_MSG | MCAN_INT_RXFIFO0_FULL | MCAN_INT_RXFIFO0_MSG_LOST;
inline constexpr uint32_t MCAN_RX_FIFO1_ACTIVITY_MASK =
    MCAN_INT_RXFIFO1_NEW_MSG | MCAN_INT_RXFIFO1_FULL | MCAN_INT_RXFIFO1_MSG_LOST;

ErrorCode ConvertMcanStatus(hpm_stat_t status);
bool HasLowLevelTiming(const CAN::BitTiming& timing);
bool HasLowLevelTiming(const FDCAN::DataBitTiming& timing);
uint16_t SamplePointToPermille(float sample_point);
uint16_t SamplePointToHpmRange(float sample_point, bool upper);
mcan_node_mode_t ConvertMcanMode(const CAN::Mode& mode);
void ApplyLowLevelTiming(const CAN::BitTiming& src, mcan_bit_timing_param_t& dst);
void ApplyLowLevelTiming(const FDCAN::DataBitTiming& src, mcan_bit_timing_param_t& dst);
CAN::ErrorID ConvertMcanProtocolError(
    mcan_last_err_code_t code,
    CAN::ErrorID fallback = CAN::ErrorID::CAN_ERROR_ID_GENERIC);
uint32_t AcquireMcanClock(clock_name_t clock);
ErrorCode ApplyMcanMessageBuffer(MCAN_Type* can, void* msg_buf, uint32_t msg_buf_size);
void PrepareMcanCommonConfig(MCAN_Type* can, mcan_config_t& config, bool enable_canfd);
void PrepareMcanAcceptAllFilters(mcan_config_t& config);
void ShutdownMcan(MCAN_Type* can, uint32_t irq, bool auto_enable_irq,
                  uint32_t interrupt_mask);
void EnableMcanInterrupts(MCAN_Type* can, uint32_t irq, bool auto_enable_irq,
                          uint32_t interrupt_mask);
ErrorCode ReadMcanErrorState(MCAN_Type* can, CAN::ErrorState& state);
size_t HardwareTxQueueEmptySize(MCAN_Type* can);
void BuildMcanClassicTxFrame(const CAN::ClassicPack& pack, mcan_tx_frame_t& frame);
void BuildMcanClassicRxPack(const mcan_rx_message_t& frame, CAN::ClassicPack& pack);

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

  if ((flags & MCAN_RX_FIFO0_ACTIVITY_MASK) != 0U)
  {
    on_rx_fifo(0U, flags, in_isr);
  }
  if ((flags & MCAN_RX_FIFO1_ACTIVITY_MASK) != 0U)
  {
    on_rx_fifo(1U, flags, in_isr);
  }
  if ((flags & MCAN_RX_FAULT_MASK) != 0U)
  {
    emit_other_error(in_isr);
  }
  if ((flags & MCAN_TX_INTERRUPT_MASK) != 0U)
  {
    on_tx();
  }
  if ((flags & MCAN_ERROR_INTERRUPT_MASK) != 0U)
  {
    on_error(flags & MCAN_ERROR_INTERRUPT_MASK, in_isr);
    on_tx();
  }
}

}  // namespace LibXR::detail

#endif
