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
#define LIBXR_HPM_MCAN_SUPPORTED 1
#else
#define LIBXR_HPM_MCAN_SUPPORTED 0
#endif

#if LIBXR_HPM_MCAN_SUPPORTED

namespace LibXR
{

namespace detail
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

}  // namespace detail

/**
 * @brief HPM MCAN FDCAN 驱动实现 / HPM MCAN FDCAN driver implementation.
 */
class HPMCANFD : public FDCAN
{
 public:
  static constexpr uint32_t INVALID_IRQ = 0xFFFFFFFFu;
  static constexpr uint32_t DEFAULT_TX_POOL_SIZE = 8;
  static constexpr uint8_t MAX_INSTANCES = MCAN_SOC_MAX_COUNT;

  /**
   * @brief 构造 MCAN FDCAN wrapper / Construct the MCAN FDCAN wrapper.
   */
  HPMCANFD(MCAN_Type* can, clock_name_t clock, uint8_t index = 0,
           uint32_t irq = INVALID_IRQ, bool auto_enable_irq = true,
           uint32_t queue_size = DEFAULT_TX_POOL_SIZE, void* msg_buf = nullptr,
           uint32_t msg_buf_size = 0);

  ~HPMCANFD() override;

  /**
   * @brief 初始化驱动 / Initialize driver.
   */
  ErrorCode Init(void);

  /** @brief 在配置前设置 MCAN message RAM / Set message RAM before configuration. */
  ErrorCode SetMessageBuffer(void* msg_buf, uint32_t msg_buf_size);

  /**
   * @brief 设置 CAN 配置 / Set CAN configuration.
   */
  ErrorCode SetConfig(const CAN::Configuration& cfg) override;

  /**
   * @brief 设置 FDCAN 全配置 / Set full FDCAN configuration.
   */
  ErrorCode SetConfig(const FDCAN::Configuration& cfg) override;

  uint32_t GetClockFreq() const override;

  ErrorCode AddMessage(const ClassicPack& pack) override;

  ErrorCode AddMessage(const FDPack& pack) override;

  /**
   * @brief 查询当前错误状态 / Query current error state.
   */
  ErrorCode GetErrorState(CAN::ErrorState& state) const override;

  /**
   * @brief 查询硬件 TX FIFO 空闲槽数量 / Query TX FIFO free slots.
   */
  size_t HardwareTxQueueEmptySize() const;

  /**
   * @brief 处理指定 RX FIFO 的接收中断 / Process RX FIFO interrupt.
   */
  void ProcessRxInterrupt(uint32_t fifo);

  /** @brief 处理 MCAN error/status 中断 / Process MCAN error/status interrupt. */
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
  ErrorCode ApplyMessageBuffer();
  void Shutdown();
  static bool BuildRxPack(const mcan_rx_message_t& frame, ClassicPack& pack);
  static bool BuildRxPack(const mcan_rx_message_t& frame, FDPack& pack);
  static CAN::ErrorID ConvertProtocolError(mcan_last_err_code_t code);
  void EmitErrorFrame(CAN::ErrorID error_id, bool in_isr);

  static HPMCANFD* instance_map_[MAX_INSTANCES];

  MCAN_Type* can_;
  clock_name_t clock_;
  uint8_t index_;
  uint32_t irq_;
  bool auto_enable_irq_;
  void* msg_buf_{nullptr};
  uint32_t msg_buf_size_{0};
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

#endif
