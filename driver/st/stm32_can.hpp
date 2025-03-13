#pragma once

#include "main.h"

#ifdef HAL_CAN_MODULE_ENABLED

#ifdef CAN
#undef CAN
#endif

#include "can.hpp"
#include "libxr.hpp"

typedef enum {
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

namespace LibXR {
class STM32CAN : public CAN {
 public:
  STM32CAN(CAN_HandleTypeDef* hcan, const char* tp_name, uint32_t queue_size)
      : CAN(tp_name),
        hcan_(hcan),
        id_(STM32_CAN_GetID(hcan->Instance)),
        tx_queue_(queue_size) {
    map[id_] = this;
    Init();
  }

  ErrorCode Init(void) {
    CAN_FilterTypeDef can_filter = {};

    can_filter.FilterIdHigh = 0;
    can_filter.FilterIdLow = 0;
    can_filter.FilterMode = CAN_FILTERMODE_IDMASK;
    can_filter.FilterScale = CAN_FILTERSCALE_32BIT;
    can_filter.FilterMaskIdHigh = 0;
    can_filter.FilterMaskIdLow = 0;
    can_filter.FilterFIFOAssignment = fifo_;
    can_filter.FilterActivation = ENABLE;
#ifdef CAN3
    if (id_ == STM32_CAN1) {
      can_filter.FilterBank = 0;
      can_filter.SlaveStartFilterBank = 14;
      fifo_ = CAN_RX_FIFO0;
    } else if (id_ == STM32_CAN2) {
      can_filter.FilterBank = 14;
      can_filter.SlaveStartFilterBank = 14;
      fifo_ = CAN_RX_FIFO0;
    } else if (id_ == STM32_CAN3) {
      can_filter.FilterBank = 3;
      fifo_ = CAN_RX_FIFO1;
    }
#else
#ifdef CAN2
    if (id_ == STM32_CAN1) {
      can_filter.FilterBank = 0;
      can_filter.SlaveStartFilterBank = 14;
      fifo_ = CAN_RX_FIFO0;
    } else if (id_ == STM32_CAN2) {
      can_filter.FilterBank = 14;
      can_filter.SlaveStartFilterBank = 14;
      fifo_ = CAN_RX_FIFO1;
    }
#else
    if (id_ == STM32_CAN1) {
      can_filter.FilterBank = 0;
      can_filter.SlaveStartFilterBank = 14;
      fifo_ = CAN_RX_FIFO0;
    }
#endif
#endif
    else {
      ASSERT(false);
      return ErrorCode::FAILED;
    }
    can_filter.FilterFIFOAssignment = fifo_;

    if (HAL_CAN_ConfigFilter(hcan_, &can_filter) != HAL_OK) {
      return ErrorCode::FAILED;
    }

    if (HAL_CAN_Start(hcan_) != HAL_OK) {
      return ErrorCode::FAILED;
    }

    if (fifo_ == CAN_RX_FIFO0) {
      HAL_CAN_ActivateNotification(hcan_, CAN_IT_RX_FIFO0_MSG_PENDING);
    } else {
      HAL_CAN_ActivateNotification(hcan_, CAN_IT_RX_FIFO1_MSG_PENDING);
    }

    HAL_CAN_ActivateNotification(hcan_, CAN_IT_ERROR);
    HAL_CAN_ActivateNotification(hcan_, CAN_IT_TX_MAILBOX_EMPTY);

    return ErrorCode::OK;
  }

  ErrorCode AddMessage(const ClassicPack& pack) override {
    CAN_TxHeaderTypeDef txHeader;  // NOLINT

    txHeader.DLC = sizeof(pack.data);

    switch (pack.type) {
      case Type::STANDARD:
        txHeader.IDE = CAN_ID_STD;
        txHeader.RTR = CAN_RTR_DATA;
        break;
      case Type::EXTENDED:
        txHeader.IDE = CAN_ID_EXT;
        txHeader.RTR = CAN_RTR_DATA;
        break;
      case Type::REMOTE_STANDARD:
        txHeader.IDE = CAN_ID_STD;
        txHeader.RTR = CAN_RTR_REMOTE;
        break;
      case Type::REMOTE_EXTENDED:
        txHeader.IDE = CAN_ID_EXT;
        txHeader.RTR = CAN_RTR_REMOTE;
        break;
    }
    txHeader.StdId = (pack.type == Type::EXTENDED) ? 0 : pack.id;
    txHeader.ExtId = (pack.type == Type::EXTENDED) ? pack.id : 0;
    txHeader.TransmitGlobalTime = DISABLE;

    if (HAL_CAN_AddTxMessage(hcan_, &txHeader, pack.data, &txMailbox) !=
        HAL_OK) {
      if (tx_queue_.Push(pack) != ErrorCode::OK) {
        return ErrorCode::FULL;
      }
    }

    return ErrorCode::OK;
  }

  void ProcessRxInterrupt() {
    while (HAL_CAN_GetRxMessage(hcan_, fifo_, &rx_buff_.header,
                                rx_buff_.pack.data) == HAL_OK) {
      if (rx_buff_.header.IDE == CAN_ID_STD) {
        rx_buff_.pack.id = rx_buff_.header.StdId;
        rx_buff_.pack.type = Type::STANDARD;
      } else {
        rx_buff_.pack.id = rx_buff_.header.ExtId;
        rx_buff_.pack.type = Type::EXTENDED;
      }

      if (rx_buff_.header.RTR == CAN_RTR_REMOTE) {
        if (rx_buff_.pack.type == Type::STANDARD) {
          rx_buff_.pack.type = Type::REMOTE_STANDARD;
        } else {
          rx_buff_.pack.type = Type::REMOTE_EXTENDED;
        }
      }

      classic_tp_.PublishFromCallback(rx_buff_.pack, true);
    }
  }

  void ProcessTxInterrupt() {
    if (tx_queue_.Peek(tx_buff_.pack) == ErrorCode::OK) {
      tx_buff_.header.DLC = sizeof(tx_buff_.pack.data);
      switch (tx_buff_.pack.type) {
        case Type::STANDARD:
          tx_buff_.header.IDE = CAN_ID_STD;
          tx_buff_.header.RTR = CAN_RTR_DATA;
          break;
        case Type::EXTENDED:
          tx_buff_.header.IDE = CAN_ID_EXT;
          tx_buff_.header.RTR = CAN_RTR_DATA;
          break;
        case Type::REMOTE_STANDARD:
          tx_buff_.header.IDE = CAN_ID_STD;
          tx_buff_.header.RTR = CAN_RTR_REMOTE;
          break;
        case Type::REMOTE_EXTENDED:
          tx_buff_.header.IDE = CAN_ID_EXT;
          tx_buff_.header.RTR = CAN_RTR_REMOTE;
          break;
      }
      tx_buff_.header.StdId =
          (tx_buff_.pack.type == Type::EXTENDED) ? 0 : tx_buff_.pack.id;
      tx_buff_.header.ExtId =
          (tx_buff_.pack.type == Type::EXTENDED) ? tx_buff_.pack.id : 0;
      tx_buff_.header.TransmitGlobalTime = DISABLE;

      if (HAL_CAN_AddTxMessage(hcan_, &tx_buff_.header, tx_buff_.pack.data,
                               &txMailbox) == HAL_OK) {
        tx_queue_.Pop();
      }
    }
  }

  CAN_HandleTypeDef* hcan_;

  stm32_can_id_t id_;
  LockFreeQueue<ClassicPack> tx_queue_;
  uint32_t fifo_;
  static STM32CAN* map[STM32_CAN_NUMBER];  // NOLINT

  struct {
    CAN_RxHeaderTypeDef header;
    ClassicPack pack;
  } rx_buff_;

  struct {
    CAN_TxHeaderTypeDef header;
    ClassicPack pack;
  } tx_buff_;

  uint32_t txMailbox;
};
}  // namespace LibXR

#endif
