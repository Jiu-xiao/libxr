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
    if (uart->uart_handle_->gState == HAL_UART_STATE_READY) {
      size_t need_write = 0;
      port.Flush();
      if (port.queue_data_->PopBlock(uart->dma_buff_tx_.addr_, &need_write) !=
          ErrorCode::OK) {
        return ErrorCode::EMPTY;
      }

      HAL_UART_Transmit_DMA(uart->uart_handle_,
                            static_cast<uint8_t *>(uart->dma_buff_tx_.addr_),
                            need_write);
      WriteOperation op;
      port.queue_op_->Peek(op);
      op.MarkAsRunning();
    } else {
    }

    return ErrorCode::OK;
  }

  static ErrorCode ReadFun(ReadPort &port) {
    STM32UART *uart = CONTAINER_OF(&port, STM32UART, read_port_);
    UNUSED(uart);
    ReadInfoBlock block;

    if (port.queue_block_->Peek(block) != ErrorCode::OK) {
      return ErrorCode::EMPTY;
    }

    block.op_.MarkAsRunning();

    if (port.queue_data_->Size() >= block.data_.size_) {
      port.queue_data_->PopBatch(block.data_.addr_, block.data_.size_);
      port.queue_block_->Pop();
      port.read_size_ = block.data_.size_;
      block.op_.UpdateStatus(false, ErrorCode::OK);
      return ErrorCode::OK;
    } else {
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

  ErrorCode SetConfig(UART::Configuration config) {
    uart_handle_->Init.BaudRate = config.baudrate;

    switch (config.parity) {
      case UART::Parity::NoParity:
        uart_handle_->Init.Parity = UART_PARITY_NONE;
        uart_handle_->Init.WordLength = UART_WORDLENGTH_8B;
        break;
      case UART::Parity::Even:
        uart_handle_->Init.Parity = UART_PARITY_EVEN;
        uart_handle_->Init.WordLength = UART_WORDLENGTH_9B;
        break;
      case UART::Parity::Odd:
        uart_handle_->Init.Parity = UART_PARITY_ODD;
        uart_handle_->Init.WordLength = UART_WORDLENGTH_9B;
        break;
      default:
        ASSERT(false);
    }

    switch (config.stop_bits) {
      case 1:
        uart_handle_->Init.StopBits = UART_STOPBITS_1;
        break;
      case 2:
        uart_handle_->Init.StopBits = UART_STOPBITS_2;
        break;
      default:
        ASSERT(false);
    }

    if (HAL_UART_Init(uart_handle_) != HAL_OK) {
      return ErrorCode::INIT_ERR;
    }
    return ErrorCode::OK;
  }

  RawData dma_buff_rx_, dma_buff_tx_;

  UART_HandleTypeDef *uart_handle_;

  stm32_uart_id_t id_ = STM32_UART_ID_ERROR;

  static STM32UART *map[STM32_UART_NUMBER];  // NOLINT
};

}  // namespace LibXR
