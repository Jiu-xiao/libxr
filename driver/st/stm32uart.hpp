#include "libxr_rw.hpp"
#include "main.h"
#include "stm32uart_def.h"
#include "uart.hpp"

namespace LibXR {
class STM32UART : public UART {
public:
  static ErrorCode write_fun(WritePort &port) {
    STM32UART *uart = CONTAINER_OF(&port, STM32UART, write_port_);
    if (uart->uart_handle_->gState == HAL_UART_STATE_READY) {
      uart->dma_buff_tx_ = port.info_.data;
      HAL_UART_Transmit_DMA(uart->uart_handle_, (uint8_t *)(uart->dma_buff_tx_),
                            uart->dma_buff_tx_.Used());
      port.UpdateStatus();

    } else {
      WriteInfoBlock block = port.info_;
      return port.queue_->Push(block);
    }

    return ErrorCode::OK;
  }

  static ErrorCode read_fun(ReadPort &port) {
    STM32UART *uart = CONTAINER_OF(&port, STM32UART, read_port_);
    if (uart->rx_queue_->Size() >= port.info_.data.size_) {
      uart->rx_queue_->PopBatch(port.info_.data.addr_, port.info_.data.size_);
      port.UpdateStatus(false, ErrorCode::OK);
      return ErrorCode::OK;
    } else {
      ReadInfoBlock block = port.info_;
      uart->read_port_.UpdateStatus();
      return port.queue_->Push(block);
    }
  }

  STM32UART(UART_HandleTypeDef &uart_handle, RawData dma_buff_rx,
            RawData dma_buff_tx, uint32_t rx_fifo_len = 128,
            uint32_t rx_queue_size = 5, uint32_t tx_queue_size = 5)
      : UART(ReadPort(rx_queue_size), WritePort(tx_queue_size)),
        dma_buff_rx_(dma_buff_rx), dma_buff_tx_(dma_buff_tx),
        uart_handle_(&uart_handle), rx_queue_(new BaseQueue(1, rx_fifo_len)) {
    id_ = STM32_UART_GetID(uart_handle_->Instance);

    ASSERT(id_ != STM32_UART_ID_ERROR);

    map[id_] = this;

    if ((uart_handle.Init.Mode & UART_MODE_TX) == UART_MODE_TX) {
      write_port_ = write_fun;
    }

    if ((uart_handle.Init.Mode & UART_MODE_RX) == UART_MODE_RX) {
      __HAL_UART_ENABLE_IT(&uart_handle, UART_IT_IDLE);

      HAL_UART_Receive_DMA(&uart_handle, (uint8_t *)(dma_buff_rx_),
                           dma_buff_rx_.Size());
      read_port_ = read_fun;
    }
  }

  void CheckReceive() {
    if (read_port_.queue_->Peek(read_port_.info_) == ErrorCode::OK) {
      if (rx_queue_->Size() >= read_port_.info_.data.size_) {
        rx_queue_->PopBatch(read_port_.info_.data.addr_,
                            read_port_.info_.data.size_);
        read_port_.UpdateStatus(true, ErrorCode::OK);
        read_port_.queue_->Pop();
      }
    }
  }

  Buffer dma_buff_rx_, dma_buff_tx_;

  UART_HandleTypeDef *uart_handle_;

  stm32_uart_id_t id_ = STM32_UART_ID_ERROR;

  BaseQueue *rx_queue_ = nullptr;

  static STM32UART *map[STM32_UART_NUMBER];
};

} // namespace LibXR
