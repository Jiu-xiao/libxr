#include "stm32_uart.hpp"

using namespace LibXR;

STM32UART *STM32UART::map[STM32_UART_NUMBER];

stm32_uart_id_t STM32_UART_GetID(USART_TypeDef *addr) {
  if (addr == nullptr) {
    return stm32_uart_id_t::STM32_UART_ID_ERROR;
  }
#ifdef USART1
  else if (addr == USART1) {
    return stm32_uart_id_t::STM32_USART1;
  }
#endif
#ifdef USART2
  else if (addr == USART2) {
    return stm32_uart_id_t::STM32_USART2;
  }
#endif
#ifdef USART3
  else if (addr == USART3) {
    return stm32_uart_id_t::STM32_USART3;
  }
#endif
#ifdef USART4
  else if (addr == USART4) {
    return stm32_uart_id_t::STM32_USART4;
  }
#endif
#ifdef USART5
  else if (addr == USART5) {
    return stm32_uart_id_t::STM32_USART5;
  }
#endif
#ifdef USART6
  else if (addr == USART6) {
    return stm32_uart_id_t::STM32_USART6;
  }
#endif
#ifdef USART7
  else if (addr == USART7) {
    return stm32_uart_id_t::STM32_USART7;
  }
#endif
#ifdef USART8
  else if (addr == USART8) {
    return stm32_uart_id_t::STM32_USART8;
  }
#endif
#ifdef USART9
  else if (addr == USART9) {
    return stm32_uart_id_t::STM32_USART9;
  }
#endif
#ifdef USART10
  else if (addr == USART10) {
    return stm32_uart_id_t::STM32_USART10;
  }
#endif
#ifdef USART11
  else if (addr == USART11) {
    return stm32_uart_id_t::STM32_USART11;
  }
#endif
#ifdef USART12
  else if (addr == USART12) {
    return stm32_uart_id_t::STM32_USART12;
  }
#endif
#ifdef USART13
  else if (addr == USART13) {
    return stm32_uart_id_t::STM32_USART13;
  }
#endif
#ifdef UART1
  else if (addr == UART1) {
    return stm32_uart_id_t::STM32_UART1;
  }
#endif
#ifdef UART2
  else if (addr == UART2) {
    return stm32_uart_id_t::STM32_UART2;
  }
#endif
#ifdef UART3
  else if (addr == UART3) {
    return stm32_uart_id_t::STM32_UART3;
  }
#endif
#ifdef UART4
  else if (addr == UART4) {
    return stm32_uart_id_t::STM32_UART4;
  }
#endif
#ifdef UART5
  else if (addr == UART5) {
    return stm32_uart_id_t::STM32_UART5;
  }
#endif
#ifdef UART6
  else if (addr == UART6) {
    return stm32_uart_id_t::STM32_UART6;
  }
#endif
#ifdef UART7
  else if (addr == UART7) {
    return stm32_uart_id_t::STM32_UART7;
  }
#endif
#ifdef UART8
  else if (addr == UART8) {
    return stm32_uart_id_t::STM32_UART8;
  }
#endif
#ifdef UART9
  else if (addr == UART9) {
    return stm32_uart_id_t::STM32_UART9;
  }
#endif
#ifdef UART10
  else if (addr == UART10) {
    return stm32_uart_id_t::STM32_UART10;
  }
#endif
#ifdef UART11
  else if (addr == UART11) {
    return stm32_uart_id_t::STM32_UART11;
  }
#endif
#ifdef UART12
  else if (addr == UART12) {
    return stm32_uart_id_t::STM32_UART12;
  }
#endif
#ifdef UART13
  else if (addr == UART13) {
    return stm32_uart_id_t::STM32_UART13;
  }
#endif
#ifdef LPUART1
  else if (addr == LPUART1) {
    return stm32_uart_id_t::STM32_LPUART1;
  }
#endif
#ifdef LPUART2
  else if (addr == LPUART2) {
    return stm32_uart_id_t::STM32_LPUART2;
  }
#endif
#ifdef LPUART3
  else if (addr == LPUART3) {
    return stm32_uart_id_t::STM32_LPUART3;
  }
#endif
  else {
    return stm32_uart_id_t::STM32_UART_ID_ERROR;
  }
}

extern "C" void STM32_UART_ISR_Handler_IDLE(UART_HandleTypeDef *uart_handle) {
  if (__HAL_UART_GET_FLAG(uart_handle, UART_FLAG_IDLE)) {
    __HAL_UART_CLEAR_IDLEFLAG(uart_handle);
    int len =
        uart_handle->RxXferSize - __HAL_DMA_GET_COUNTER(uart_handle->hdmarx);
    ASSERT(len >= 0);

    auto uart = STM32UART::map[STM32_UART_GetID(uart_handle->Instance)];

    uart->rx_queue_->PushBatch(uart->dma_buff_rx_, len);
    HAL_UART_AbortReceive_IT(uart_handle);
    uart->CheckReceive();
  }
}

void STM32_UART_ISR_Handler_TX_CPLT(stm32_uart_id_t id) {
  auto uart = STM32UART::map[id];
  uart->write_port_.UpdateStatus(true, ErrorCode::OK);
  auto need_send = uart->tx_queue_->Size();
  while (true) {
    if (need_send > 0) {
      need_send = MIN(need_send, uart->dma_buff_tx_.Size());
      uart->tx_queue_->PopBatch(uart->dma_buff_tx_.raw_, need_send);
      uart->dma_buff_tx_.used_ = need_send;
      HAL_UART_Transmit_DMA(uart->uart_handle_, (uint8_t *)(uart->dma_buff_tx_),
                            uart->dma_buff_tx_.Used());
      return;
    } else if (uart->write_port_.queue_->Pop(uart->write_port_.info_) ==
               ErrorCode::OK) {
      uart->tx_queue_->PushBatch(uart->write_port_.info_.data.addr_,
                                 uart->write_port_.info_.data.size_);
    } else {
      return;
    }
  }
}

extern "C" void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
  STM32_UART_ISR_Handler_TX_CPLT(STM32_UART_GetID(huart->Instance));
}

extern "C" void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
  auto len = huart->RxXferSize;
  auto uart = STM32UART::map[STM32_UART_GetID(huart->Instance)];
  uart->rx_queue_->PushBatch(static_cast<uint8_t *>(huart->pRxBuffPtr), len);
  HAL_UART_Receive_DMA(huart, huart->pRxBuffPtr, len);
  uart->CheckReceive();
}

extern "C" void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {}

extern "C" void HAL_UART_AbortCpltCallback(UART_HandleTypeDef *huart) {
  auto uart = STM32UART::map[STM32_UART_GetID(huart->Instance)];
  HAL_UART_Receive_DMA(huart, huart->pRxBuffPtr, uart->dma_buff_rx_.Size());
  uart->write_port_.UpdateStatus(true, ErrorCode::FAILED);
}

extern "C" void HAL_UART_AbortTransmitCpltCallback(UART_HandleTypeDef *huart) {
  STM32UART::map[STM32_UART_GetID(huart->Instance)]->write_port_.UpdateStatus(
      true, ErrorCode::FAILED);
}

extern "C" void HAL_UART_AbortReceiveCpltCallback(UART_HandleTypeDef *huart) {
  HAL_UART_Receive_DMA(
      huart, huart->pRxBuffPtr,
      STM32UART::map[STM32_UART_GetID(huart->Instance)]->dma_buff_rx_.Size());
}
