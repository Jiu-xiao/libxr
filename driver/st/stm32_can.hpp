#pragma once

#include "main.h"

#ifdef HAL_CAN_MODULE_ENABLED

#ifdef CAN
#if !defined(CAN1)
#define CAN1 ((CAN_TypeDef*)CAN_BASE)
#endif
#undef CAN
#endif

#include "can.hpp"
#include "slot_pool.hpp"

typedef enum
{
#ifdef CAN1
  STM32_CAN1,
#endif
#ifdef CAN2
  STM32_CAN2,
#endif
#ifdef CAN3
  STM32_CAN3,
#endif
  STM32_CAN_NUMBER,
  STM32_CAN_ID_ERROR
} stm32_can_id_t;

static_assert(STM32_CAN_NUMBER >= 1, "No CAN instance detected for this MCU");

stm32_can_id_t STM32_CAN_GetID(CAN_TypeDef* addr);  // NOLINT

namespace LibXR
{
/**
 * @brief STM32 CAN 驱动实现 / STM32 CAN driver implementation
 */
class STM32CAN : public CAN
{
 public:
  /**
   * @brief 构造 CAN 驱动对象 / Construct CAN driver object
   *
   * @param hcan HAL CAN 句柄 / HAL CAN handle
   * @param queue_size 发送队列大小 / TX queue size
   * @pre queue_size 必须大于 1 / queue_size must be greater than 1.
   */
  STM32CAN(CAN_HandleTypeDef* hcan, uint32_t queue_size);

  /**
   * @brief 初始化驱动 / Initialize driver
   *
   * @return ErrorCode 错误码 / Error code
   */
  ErrorCode Init(void);

  /**
   * @brief 设置 CAN 配置 / Set CAN configuration
   *
   * @param cfg CAN 配置参数 / CAN configuration
   * @return ErrorCode 操作结果 / Operation result
   */
  ErrorCode SetConfig(const CAN::Configuration& cfg) override;

  uint32_t GetClockFreq() const override;

  ErrorCode AddMessage(const ClassicPack& pack) override;

  ErrorCode GetErrorState(CAN::ErrorState& state) const override;

  /**
   * @brief 处理接收中断 / Handle RX interrupt
   *
   */
  void ProcessRxInterrupt();

  /**
   * @brief 处理错误中断 / Handle error interrupt
   *
   */
  void ProcessErrorInterrupt();

  CAN_HandleTypeDef* hcan_;

  stm32_can_id_t id_;
  uint32_t fifo_;
  static STM32CAN* map[STM32_CAN_NUMBER];  // NOLINT

  struct
  {
    CAN_RxHeaderTypeDef header;
    ClassicPack pack;
  } rx_buff_;

  struct
  {
    CAN_TxHeaderTypeDef header;
    ClassicPack pack;
  } tx_buff_;

  uint32_t txMailbox;

  /// 发送软件队列 / TX software queue.
  ///
  /// 槽池替代 MPMC 队列的原因：MPMC 先占槽后拷贝，任务在 `TxService` 的
  /// `Pop` 中途（已 CAS 队头、槽序号未发布）被中断打断时，中断里
  /// `AddMessage` 的 `Push` 会在回绕到该槽的位置上拿到假的 FULL 而丢帧。
  /// 槽池里被打断的操作只占住自己的槽（读取中），`Acquire` 扫描会跳过它；
  /// 只要还有空闲槽，中断里的 `Put` 一定成功，FULL 只在确实没有空闲槽时
  /// 出现。出队顺序从"认领先后"变为"发布完成先后"，同一发送者内部保序。
  ///
  /// Reason to replace the MPMC queue with a slot pool: MPMC claims a cell
  /// before copying. When a task is interrupted inside `TxService`'s `Pop`
  /// (head CAS done, cell sequence not published yet), `AddMessage` from an
  /// interrupt wraps onto that cell and gets a false FULL, dropping the frame.
  /// With the slot pool the interrupted operation only occupies its own slot
  /// (reading), which the `Acquire` scan skips: as long as a free slot exists,
  /// `Put` from an interrupt succeeds, and FULL appears only when no slot is
  /// really free. Dequeue order changes from claim order to publish-completion
  /// order; the order inside one sender is preserved.
  SlotPool<ClassicPack> tx_queue_;
  /// 待重试帧有效标记 / Pending retry frame flag.
  bool tx_retry_valid_{false};
  /// 待重试帧 / Pending retry frame.
  ClassicPack tx_retry_pack_{};

  std::atomic<uint32_t> tx_lock_{0};
  std::atomic<uint32_t> tx_pend_{0};

  static inline void BuildTxHeader(const ClassicPack& p, CAN_TxHeaderTypeDef& h);

  void TxService();
};
}  // namespace LibXR

#endif
