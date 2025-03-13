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
  STM32CAN(CAN_HandleTypeDef* hcan, uint32_t fifo)
      : CAN(), hcan_(hcan), id_(STM32_CAN_GetID(hcan->Instance)), fifo_(fifo) {
    map[id_] = this;
  }

  ErrorCode Init(void) {
    CAN_FilterTypeDef can_filter = {};

    can_filter.FilterBank = static_cast<uint32_t>(id_);
    can_filter.FilterIdHigh = 0;
    can_filter.FilterIdLow = 0;
    can_filter.FilterMode = CAN_FILTERMODE_IDMASK;
    can_filter.FilterScale = CAN_FILTERSCALE_32BIT;
    can_filter.FilterMaskIdHigh = 0;
    can_filter.FilterMaskIdLow = 0;
    can_filter.FilterFIFOAssignment = fifo_;
    can_filter.FilterActivation = ENABLE;

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

    return ErrorCode::OK;
  }

  ErrorCode AddMessage(const ClassicPack& pack) override {
    CAN_TxHeaderTypeDef txHeader;  // NOLINT
    uint32_t txMailbox;            // NOLINT

    txHeader.DLC = sizeof(pack.data);
    txHeader.IDE = (pack.type == Type::EXTENDED) ? CAN_ID_EXT : CAN_ID_STD;
    txHeader.RTR = (pack.type == Type::REMOTE) ? CAN_RTR_REMOTE : CAN_RTR_DATA;
    txHeader.StdId = (pack.type == Type::EXTENDED) ? 0 : pack.id;
    txHeader.ExtId = (pack.type == Type::EXTENDED) ? pack.id : 0;
    txHeader.TransmitGlobalTime = DISABLE;

    if (HAL_CAN_AddTxMessage(hcan_, &txHeader, pack.data, &txMailbox) !=
        HAL_OK) {
      return ErrorCode::FAILED;
    }

    return ErrorCode::OK;
  }

  void ProcessInterrupt() {
    while (HAL_CAN_GetRxMessage(hcan_, fifo_, &rx_buff_.header,
                                rx_buff_.data) == HAL_OK) {
      if (rx_buff_.header.IDE == CAN_ID_STD) {
        rx_buff_.pack.id = rx_buff_.header.StdId;
        rx_buff_.pack.type = Type::STANDARD;
      } else {
        rx_buff_.pack.id = rx_buff_.header.ExtId;
        rx_buff_.pack.type = Type::EXTENDED;
      }

      if (rx_buff_.header.RTR == CAN_RTR_REMOTE) {
        rx_buff_.pack.type = Type::REMOTE;
      }

      memcpy(rx_buff_.pack.data, rx_buff_.data, sizeof(rx_buff_.pack.data));

      classic_tp_.PublishFromCallback(rx_buff_.pack, true);
    }
  }

  CAN_HandleTypeDef* hcan_;

  stm32_can_id_t id_;
  uint32_t fifo_;
  static STM32CAN* map[STM32_CAN_NUMBER];  // NOLINT

  struct {
    CAN_RxHeaderTypeDef header;
    uint8_t data[8];
    ClassicPack pack;
  } rx_buff_;
};
}  // namespace LibXR

#endif
