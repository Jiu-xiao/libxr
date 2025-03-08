#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "main.h"
#include "stm32_uart_def.h"
#include "uart.hpp"

namespace LibXR {
class STM32UART : public UART {
 public:
  static ErrorCode WriteFun(WritePort &port) {
    STM32UART *uart = CONTAINER_OF(&port, STM32UART, write_port_);
    __disable_irq();
    if (uart->uart_handle_->gState == HAL_UART_STATE_READY) {
      size_t need_write = 0;
      port.Flush();
      if (port.queue_data_->PopBlock(uart->dma_buff_tx_.addr_, &need_write) !=
          ErrorCode::OK) {
        __enable_irq();
        return ErrorCode::EMPTY;
      }

      HAL_UART_Transmit_DMA(uart->uart_handle_,
                            static_cast<uint8_t *>(uart->dma_buff_tx_.addr_),
                            need_write);
      __enable_irq();
      WriteOperation op;
      port.queue_op_->Peek(op);
      op.MarkAsRunning();
    } else {
      __enable_irq();
    }

    return ErrorCode::OK;
  }

  static ErrorCode ReadFun(ReadPort &port) {
    STM32UART *uart = CONTAINER_OF(&port, STM32UART, read_port_);
    UNUSED(uart);
    ReadInfoBlock block;

    __disable_irq();

    if (port.queue_block_->Peek(block) != ErrorCode::OK) {
      __enable_irq();
      return ErrorCode::EMPTY;
    }

    block.op_.MarkAsRunning();

    if (port.queue_data_->Size() >= block.data_.size_) {
      port.queue_data_->PopBatch(block.data_.addr_, block.data_.size_);
      port.queue_block_->Pop();
      __enable_irq();
      port.read_size_ = block.data_.size_;
      block.op_.UpdateStatus(false, ErrorCode::OK);
      return ErrorCode::OK;
    } else {
      __enable_irq();
      return ErrorCode::EMPTY;
    }
  }

  STM32UART(UART_HandleTypeDef &uart_handle, RawData dma_buff_rx,
            RawData dma_buff_tx, uint32_t rx_queue_size = 5,
            uint32_t tx_queue_size = 5)
      : UART(ReadPort(rx_queue_size, dma_buff_rx.size_),
             WritePort(tx_queue_size, dma_buff_tx.size_)),
        dma_buff_rx_(dma_buff_rx),
        dma_buff_tx_(dma_buff_tx),
        uart_handle_(&uart_handle),
        id_(STM32_UART_GetID(uart_handle_->Instance)) {
    ASSERT(id_ != STM32_UART_ID_ERROR);

    map[id_] = this;

    if ((uart_handle.Init.Mode & UART_MODE_TX) == UART_MODE_TX) {
      write_port_ = WriteFun;
    }

    if ((uart_handle.Init.Mode & UART_MODE_RX) == UART_MODE_RX) {
      __HAL_UART_ENABLE_IT(&uart_handle, UART_IT_IDLE);

      HAL_UART_Receive_DMA(&uart_handle,
                           reinterpret_cast<uint8_t *>(dma_buff_rx_.addr_),
                           dma_buff_rx_.size_);
      read_port_ = ReadFun;
    }
  }

  void CheckReceive() {
    ReadInfoBlock block;
    while (read_port_.queue_block_->Peek(block) == ErrorCode::OK) {
      if (read_port_.queue_data_->Size() >= block.data_.size_) {
        read_port_.queue_data_->PopBatch(block.data_.addr_, block.data_.size_);
        read_port_.read_size_ = block.data_.size_;
        block.op_.UpdateStatus(true, ErrorCode::OK);
        read_port_.queue_block_->Pop();
      } else {
        break;
      }
    }
  }

  RawData dma_buff_rx_, dma_buff_tx_;

  UART_HandleTypeDef *uart_handle_;

  stm32_uart_id_t id_ = STM32_UART_ID_ERROR;

  static STM32UART *map[STM32_UART_NUMBER];  // NOLINT
};

}  // namespace LibXR
