#pragma once

#include "main.h"

#ifdef HAL_CAN_MODULE_ENABLED

#ifdef CAN
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

stm32_can_id_t STM32_CAN_GetID(CAN_TypeDef* addr);  // NOLINT

namespace LibXR
{
/**
 * @brief STM32CAN 类，用于处理 STM32 系统的 CAN 通道。 Provides handling for STM32 CAN
 * channels.
 *
 */
class STM32CAN : public CAN
{
 public:
  /**
   * @brief STM32CAN 类，用于处理 STM32 系统的 CAN 通道。 Provides handling for STM32 CAN
   *
   * @param hcan STM32CAN对象 CAN object
   * @param queue_size 发送队列大小 Send queue size
   */
  STM32CAN(CAN_HandleTypeDef* hcan, uint32_t queue_size);

  /**
   * @brief 初始化
   *
   * @return ErrorCode
   */
  ErrorCode Init(void);

  ErrorCode AddMessage(const ClassicPack& pack) override;

  /**
   * @brief 处理接收中断
   *
   */
  void ProcessRxInterrupt();

  /**
   * @brief 处理发送中断
   *
   */
  void ProcessTxInterrupt();

  CAN_HandleTypeDef* hcan_;

  stm32_can_id_t id_;
  LockFreeQueue<ClassicPack> tx_queue_;
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
  Mutex write_mutex_;
};
}  // namespace LibXR

#endif
