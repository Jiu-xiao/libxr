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
#include "libxr.hpp"

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
   * @param pool_size 发送池大小 / TX pool size
   */
  STM32CAN(CAN_HandleTypeDef* hcan, uint32_t pool_size);

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

  LockFreePool<ClassicPack> tx_pool_;

  std::atomic<uint32_t> tx_lock_{0};
  std::atomic<uint32_t> tx_pend_{0};

  static inline void BuildTxHeader(const ClassicPack& p, CAN_TxHeaderTypeDef& h);

  void TxService();
};
}  // namespace LibXR

#endif
