#pragma once

#include <type_traits>

#include "main.h"

#ifdef HAL_FDCAN_MODULE_ENABLED

#ifdef FDCAN
#undef FDCAN
#endif

#include "can.hpp"
#include "libxr.hpp"

typedef enum : uint8_t
{
#ifdef FDCAN1
  STM32_FDCAN1,
#endif
#ifdef FDCAN2
  STM32_FDCAN2,
#endif
#ifdef FDCAN3
  STM32_FDCAN3,
#endif
  STM32_FDCAN_NUMBER,
  STM32_FDCAN_ID_ERROR
} stm32_fdcan_id_t;

stm32_fdcan_id_t STM32_FDCAN_GetID(FDCAN_GlobalTypeDef* addr);  // NOLINT

template <typename, typename = void>
struct HasTxFifoQueueElmtsNbr : std::false_type
{
};

template <typename T>
struct HasTxFifoQueueElmtsNbr<
    T, std::void_t<decltype(std::declval<T>()->Init.TxFifoQueueElmtsNbr)>>
    : std::true_type
{
};

// 当没有TxFifoQueueElmtsNbr成员时的处理函数
template <typename T>
typename std::enable_if<!HasTxFifoQueueElmtsNbr<T>::value, uint32_t>::type
GetTxFifoTotalElements(T& hcan)  // NOLINT
{
  UNUSED(hcan);
  return 3;
}

// 当有TxFifoQueueElmtsNbr成员时的处理函数
template <typename T>
typename std::enable_if<HasTxFifoQueueElmtsNbr<T>::value, uint32_t>::type
GetTxFifoTotalElements(T& hcan)  // NOLINT
{
  return hcan->Init.TxFifoQueueElmtsNbr;
}

namespace LibXR
{
/**
 * @brief STM32CANFD 类，用于处理 STM32 系统的 CANFD 通道。 Provides handling for STM32
 * CANFD
 *
 */
class STM32CANFD : public FDCAN
{
 public:
  /**
   * @brief STM32CANFD 类，用于处理 STM32 系统的 CANFD 通道。 Provides handling for STM32
   * CANFD
   *
   * @param hcan STM32CANFD对象 CAN object
   * @param queue_size 发送队列大小 Send queue size
   */
  STM32CANFD(FDCAN_HandleTypeDef* hcan, uint32_t queue_size);

  /**
   * @brief 初始化
   *
   * @return ErrorCode
   */
  ErrorCode Init(void);

  ErrorCode AddMessage(const ClassicPack& pack) override;

  ErrorCode AddMessage(const FDPack& pack) override;

  /**
   * @brief 处理接收中断
   *
   * @param fifo 接收缓冲区号 Receive buffer number
   */
  void ProcessRxInterrupt(uint32_t fifo);

  /**
   * @brief 处理发送中断
   *
   */
  void ProcessTxInterrupt();

  /**
   * @brief 获取硬件发送队列空闲大小
   *
   * @return size_t 硬件发送队列空闲大小
   */
  size_t HardwareTxQueueEmptySize()
  {
    auto free = HAL_FDCAN_GetTxFifoFreeLevel(hcan_);
    return free;
  }

  FDCAN_HandleTypeDef* hcan_;

  stm32_fdcan_id_t id_;
  LockFreePool<ClassicPack> tx_pool_;
  LockFreePool<FDPack> tx_pool_fd_;

  static STM32CANFD* map[STM32_FDCAN_NUMBER];  // NOLINT

  struct
  {
    FDCAN_RxHeaderTypeDef header;
    ClassicPack pack;
    FDPack pack_fd;
  } rx_buff_;

  struct
  {
    FDCAN_TxHeaderTypeDef header;
    ClassicPack pack;
    FDPack pack_fd;
  } tx_buff_;

  uint32_t txMailbox;
};
}  // namespace LibXR

#endif
