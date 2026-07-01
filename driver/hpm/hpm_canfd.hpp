#pragma once

#include <atomic>
#include <cstdint>

#include "hpm_mcan_backend.hpp"
#include "lockfree_pool.hpp"

#if LIBXR_HPM_MCAN_SUPPORTED

namespace LibXR
{

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
  static uint8_t DlcToBytes(uint8_t dlc);
  static uint8_t BytesToDlc(uint8_t bytes);
  ErrorCode ApplyMessageBuffer();
  void Shutdown();
  static bool BuildRxPack(const mcan_rx_message_t& frame, ClassicPack& pack);
  static bool BuildRxPack(const mcan_rx_message_t& frame, FDPack& pack);
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
