#pragma once

#include "main.h"

#ifdef HAL_FDCAN_MODULE_ENABLED

#ifdef FDCAN
#undef FDCAN
#endif

#include "can.hpp"
#include "libxr.hpp"

typedef enum
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

  static constexpr uint32_t FDCAN_PACK_LEN_MAP[16] = {
      FDCAN_DLC_BYTES_0,  FDCAN_DLC_BYTES_1,  FDCAN_DLC_BYTES_2,  FDCAN_DLC_BYTES_3,
      FDCAN_DLC_BYTES_4,  FDCAN_DLC_BYTES_5,  FDCAN_DLC_BYTES_6,  FDCAN_DLC_BYTES_7,
      FDCAN_DLC_BYTES_8,  FDCAN_DLC_BYTES_12, FDCAN_DLC_BYTES_16, FDCAN_DLC_BYTES_20,
      FDCAN_DLC_BYTES_24, FDCAN_DLC_BYTES_32, FDCAN_DLC_BYTES_48, FDCAN_DLC_BYTES_64,
  };

  static constexpr uint32_t FDCAN_PACK_LEN_TO_INT_MAP[16] = {
      0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64,
  };

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

  std::atomic<uint32_t> bus_busy_ = 0;
};
}  // namespace LibXR

#endif
