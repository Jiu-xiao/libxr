#pragma once

#include "main.h"

#ifdef HAL_FDCAN_MODULE_ENABLED

#ifdef FDCAN
#undef FDCAN
#endif

#include "can.hpp"
#include "libxr.hpp"

typedef enum {
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

static uint32_t counter = 0;

namespace LibXR {
class STM32CANFD : public FDCAN {
 public:
  STM32CANFD(FDCAN_HandleTypeDef* hcan, const char* tp_name,
             uint32_t queue_size)
      : FDCAN(tp_name),
        hcan_(hcan),
        id_(STM32_FDCAN_GetID(hcan->Instance)),
        tx_queue_(queue_size),
        tx_queue_fd_(queue_size) {
    map[id_] = this;
    Init();
  }

  ErrorCode Init(void) {
    FDCAN_FilterTypeDef can_filter = {};
    can_filter.IdType = FDCAN_STANDARD_ID;
    can_filter.FilterType = FDCAN_FILTER_MASK;
    can_filter.FilterID1 = 0x0000;
    can_filter.FilterID2 = 0x0000;

#ifdef FDCAN3
    if (id_ == STM32_FDCAN1) {
      can_filter.FilterConfig = FDCAN_RX_FIFO0;
      can_filter.FilterIndex = 0;
    } else if (id_ == STM32_FDCAN2) {
      can_filter.FilterConfig = FDCAN_RX_FIFO0;
      can_filter.FilterIndex = 1;
    } else if (id_ == STM32_FDCAN3) {
      can_filter.FilterConfig = FDCAN_RX_FIFO1;
      can_filter.FilterIndex = 2;
    }
#else
#ifdef FDCAN2
    if (id_ == STM32_FDCAN1) {
      can_filter.FilterConfig = FDCAN_RX_FIFO0;
      can_filter.FilterIndex = 0;
    } else if (id_ == STM32_FDCAN2) {
      can_filter.FilterConfig = FDCAN_RX_FIFO1;
      can_filter.FilterIndex = 1;
    }
#else
    can_filter.FilterConfig = FDCAN_RX_FIFO0;
    can_filter.FilterIndex = 0;
#endif
#endif

    if (HAL_FDCAN_ConfigFilter(hcan_, &can_filter) != HAL_OK) {
      return ErrorCode::FAILED;
    }

    can_filter.IdType = FDCAN_EXTENDED_ID;

    if (HAL_FDCAN_ConfigFilter(hcan_, &can_filter) != HAL_OK) {
      return ErrorCode::FAILED;
    }

    if (HAL_FDCAN_Start(hcan_) != HAL_OK) {
      return ErrorCode::FAILED;
    }

    if (can_filter.FilterConfig == FDCAN_RX_FIFO0) {
      HAL_FDCAN_ActivateNotification(hcan_, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0);
    } else {
      HAL_FDCAN_ActivateNotification(hcan_, FDCAN_IT_RX_FIFO1_NEW_MESSAGE, 0);
    }

    HAL_FDCAN_ActivateNotification(hcan_, FDCAN_IT_TX_FIFO_EMPTY, 0);

    return ErrorCode::OK;
  }

  ErrorCode AddMessage(const ClassicPack& pack) override {
    FDCAN_TxHeaderTypeDef header;  // NOLINT

    header.Identifier = pack.id;

    switch (pack.type) {
      case Type::STANDARD:
        header.IdType = FDCAN_STANDARD_ID;
        header.TxFrameType = FDCAN_DATA_FRAME;
        break;

      case Type::EXTENDED:
        header.IdType = FDCAN_EXTENDED_ID;
        header.TxFrameType = FDCAN_DATA_FRAME;
        break;

      case Type::REMOTE_STANDARD:
        header.IdType = FDCAN_STANDARD_ID;
        header.TxFrameType = FDCAN_REMOTE_FRAME;
        break;

      case Type::REMOTE_EXTENDED:
        header.IdType = FDCAN_EXTENDED_ID;
        header.TxFrameType = FDCAN_REMOTE_FRAME;
        break;
    }

    header.DataLength = FDCAN_DLC_BYTES_8;
    header.ErrorStateIndicator = FDCAN_ESI_PASSIVE;
    header.BitRateSwitch = FDCAN_BRS_OFF;
    header.FDFormat = FDCAN_CLASSIC_CAN;
    header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    header.MessageMarker = 0x01;

    if (HAL_FDCAN_AddMessageToTxFifoQ(hcan_, &header, pack.data) != HAL_OK) {
      if (tx_queue_.Push(pack) != ErrorCode::OK) {
        return ErrorCode::FAILED;
      }
    }

    return ErrorCode::OK;
  }

  static constexpr uint32_t FDCAN_PACK_LEN_MAP[16] = {
      FDCAN_DLC_BYTES_0,  FDCAN_DLC_BYTES_1,  FDCAN_DLC_BYTES_2,
      FDCAN_DLC_BYTES_3,  FDCAN_DLC_BYTES_4,  FDCAN_DLC_BYTES_5,
      FDCAN_DLC_BYTES_6,  FDCAN_DLC_BYTES_7,  FDCAN_DLC_BYTES_8,
      FDCAN_DLC_BYTES_12, FDCAN_DLC_BYTES_16, FDCAN_DLC_BYTES_20,
      FDCAN_DLC_BYTES_24, FDCAN_DLC_BYTES_32, FDCAN_DLC_BYTES_48,
      FDCAN_DLC_BYTES_64,
  };

  ErrorCode AddMessage(const FDPack& pack) override {
    FDCAN_TxHeaderTypeDef header;
    ASSERT(pack.len <= 64);

    header.Identifier = pack.id;

    switch (pack.type) {
      case Type::STANDARD:
        header.IdType = FDCAN_STANDARD_ID;
        header.TxFrameType = FDCAN_DATA_FRAME;
        break;

      case Type::EXTENDED:
        header.IdType = FDCAN_EXTENDED_ID;
        header.TxFrameType = FDCAN_DATA_FRAME;
        break;

      case Type::REMOTE_STANDARD:
        header.IdType = FDCAN_STANDARD_ID;
        header.TxFrameType = FDCAN_REMOTE_FRAME;
        break;

      case Type::REMOTE_EXTENDED:
        header.IdType = FDCAN_EXTENDED_ID;
        header.TxFrameType = FDCAN_REMOTE_FRAME;
        break;
    }

    if (pack.len <= 8) {
      header.DataLength = FDCAN_PACK_LEN_MAP[pack.len];
    } else if (pack.len <= 24) {
      header.DataLength = FDCAN_PACK_LEN_MAP[(pack.len - 9) / 4 + 1 + 8];
    } else if (pack.len < 32) {
      header.DataLength = FDCAN_DLC_BYTES_32;
    } else if (pack.len < 48) {
      header.DataLength = FDCAN_DLC_BYTES_48;
    } else {
      header.DataLength = FDCAN_DLC_BYTES_64;
    }

    header.ErrorStateIndicator = FDCAN_ESI_PASSIVE;
    header.BitRateSwitch = FDCAN_BRS_ON;
    header.FDFormat = FDCAN_FD_CAN;
    header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    header.MessageMarker = 0x00;

    if (HAL_FDCAN_AddMessageToTxFifoQ(hcan_, &header, pack.data) != HAL_OK) {
      if (tx_queue_fd_.Push(pack) != ErrorCode::OK) {
        return ErrorCode::FAILED;
      }
    }

    return ErrorCode::OK;
  }

  void ProcessRxInterrupt(uint32_t fifo) {
    if (HAL_FDCAN_GetRxMessage(hcan_, fifo, &rx_buff_.header,
                               rx_buff_.pack.data) == HAL_OK) {
      counter++;
      if (rx_buff_.header.FDFormat == FDCAN_FD_CAN) {
        rx_buff_.pack_fd.id = rx_buff_.header.Identifier;
        rx_buff_.pack_fd.type = (rx_buff_.header.IdType == FDCAN_EXTENDED_ID)
                                    ? Type::EXTENDED
                                    : Type::STANDARD;

        if (rx_buff_.header.RxFrameType != FDCAN_DATA_FRAME) {
          if (rx_buff_.pack_fd.type == Type::STANDARD) {
            rx_buff_.pack_fd.type = Type::REMOTE_STANDARD;
          } else {
            rx_buff_.pack_fd.type = Type::REMOTE_EXTENDED;
          }
        }

        rx_buff_.pack_fd.len = rx_buff_.header.DataLength;

        fd_tp_.PublishFromCallback(rx_buff_.pack_fd, true);
      } else {
        rx_buff_.pack.id = rx_buff_.header.Identifier;
        rx_buff_.pack.type = (rx_buff_.header.IdType == FDCAN_EXTENDED_ID)
                                 ? Type::EXTENDED
                                 : Type::STANDARD;

        if (rx_buff_.header.RxFrameType != FDCAN_DATA_FRAME) {
          if (rx_buff_.pack.type == Type::STANDARD) {
            rx_buff_.pack.type = Type::REMOTE_STANDARD;
          } else {
            rx_buff_.pack.type = Type::REMOTE_EXTENDED;
          }
        }

        classic_tp_.PublishFromCallback(rx_buff_.pack, true);
      }
    }
  }

  void ProcessTxInterrupt() {
    if (tx_queue_fd_.Peek(tx_buff_.pack_fd) == ErrorCode::OK) {
      tx_buff_.header.Identifier = tx_buff_.pack_fd.id;
      switch (tx_buff_.pack_fd.type) {
        case Type::STANDARD:
          tx_buff_.header.IdType = FDCAN_STANDARD_ID;
          tx_buff_.header.TxFrameType = FDCAN_DATA_FRAME;
          break;

        case Type::EXTENDED:
          tx_buff_.header.IdType = FDCAN_EXTENDED_ID;
          tx_buff_.header.TxFrameType = FDCAN_DATA_FRAME;
          break;

        case Type::REMOTE_STANDARD:
          tx_buff_.header.IdType = FDCAN_STANDARD_ID;
          tx_buff_.header.TxFrameType = FDCAN_REMOTE_FRAME;
          break;

        case Type::REMOTE_EXTENDED:
          tx_buff_.header.IdType = FDCAN_EXTENDED_ID;
          tx_buff_.header.TxFrameType = FDCAN_REMOTE_FRAME;
          break;
      }
      tx_buff_.header.DataLength = tx_buff_.pack_fd.len;
      tx_buff_.header.FDFormat = FDCAN_FD_CAN;
      tx_buff_.header.TxFrameType = FDCAN_DATA_FRAME;
      tx_buff_.header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
      tx_buff_.header.MessageMarker = 0x00;
      tx_buff_.header.ErrorStateIndicator = FDCAN_ESI_PASSIVE;
      tx_buff_.header.BitRateSwitch = FDCAN_BRS_ON;

      if (HAL_FDCAN_AddMessageToTxFifoQ(hcan_, &tx_buff_.header,
                                        tx_buff_.pack_fd.data) == HAL_OK) {
        tx_queue_fd_.Pop();
      }

    } else if (tx_queue_.Peek(tx_buff_.pack) == ErrorCode::OK) {
      tx_buff_.header.Identifier = tx_buff_.pack.id;
      switch (tx_buff_.pack.type) {
        case Type::STANDARD:
          tx_buff_.header.IdType = FDCAN_STANDARD_ID;
          tx_buff_.header.TxFrameType = FDCAN_DATA_FRAME;
          break;

        case Type::EXTENDED:
          tx_buff_.header.IdType = FDCAN_EXTENDED_ID;
          tx_buff_.header.TxFrameType = FDCAN_DATA_FRAME;
          break;

        case Type::REMOTE_STANDARD:
          tx_buff_.header.IdType = FDCAN_STANDARD_ID;
          tx_buff_.header.TxFrameType = FDCAN_REMOTE_FRAME;
          break;

        case Type::REMOTE_EXTENDED:
          tx_buff_.header.IdType = FDCAN_EXTENDED_ID;
          tx_buff_.header.TxFrameType = FDCAN_REMOTE_FRAME;
          break;
      }
      tx_buff_.header.DataLength = 8;
      tx_buff_.header.FDFormat = FDCAN_CLASSIC_CAN;
      tx_buff_.header.TxFrameType = FDCAN_DATA_FRAME;
      tx_buff_.header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
      tx_buff_.header.MessageMarker = 0x00;
      tx_buff_.header.ErrorStateIndicator = FDCAN_ESI_PASSIVE;
      tx_buff_.header.BitRateSwitch = FDCAN_BRS_OFF;

      if (HAL_FDCAN_AddMessageToTxFifoQ(hcan_, &tx_buff_.header,
                                        tx_buff_.pack.data) == HAL_OK) {
        tx_queue_.Pop();
      }
    }
  }

  FDCAN_HandleTypeDef* hcan_;

  stm32_fdcan_id_t id_;
  LockFreeQueue<ClassicPack> tx_queue_;
  LockFreeQueue<FDPack> tx_queue_fd_;
  static STM32CANFD* map[STM32_FDCAN_NUMBER];  // NOLINT

  struct {
    FDCAN_RxHeaderTypeDef header;
    ClassicPack pack;
    FDPack pack_fd;
  } rx_buff_;

  struct {
    FDCAN_TxHeaderTypeDef header;
    ClassicPack pack;
    FDPack pack_fd;
  } tx_buff_;

  uint32_t txMailbox;
};
}  // namespace LibXR

#endif
