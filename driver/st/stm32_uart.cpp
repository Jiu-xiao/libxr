#include "stm32_uart.hpp"

#ifdef HAL_UART_MODULE_ENABLED

using namespace LibXR;

STM32UART *STM32UART::map[STM32_UART_NUMBER] = {nullptr};

stm32_uart_id_t STM32_UART_GetID(USART_TypeDef *addr)
{
  if (addr == nullptr)
  {  // NOLINT
    return stm32_uart_id_t::STM32_UART_ID_ERROR;
  }
#ifdef USART1
  else if (addr == USART1)
  {  // NOLINT
    return stm32_uart_id_t::STM32_USART1;
  }
#endif
#ifdef USART2
  else if (addr == USART2)
  {  // NOLINT
    return stm32_uart_id_t::STM32_USART2;
  }
#endif
#ifdef USART3
  else if (addr == USART3)
  {  // NOLINT
    return stm32_uart_id_t::STM32_USART3;
  }
#endif
#ifdef USART4
  else if (addr == USART4)
  {  // NOLINT
    return stm32_uart_id_t::STM32_USART4;
  }
#endif
#ifdef USART5
  else if (addr == USART5)
  {  // NOLINT
    return stm32_uart_id_t::STM32_USART5;
  }
#endif
#ifdef USART6
  else if (addr == USART6)
  {  // NOLINT
    return stm32_uart_id_t::STM32_USART6;
  }
#endif
#ifdef USART7
  else if (addr == USART7)
  {  // NOLINT
    return stm32_uart_id_t::STM32_USART7;
  }
#endif
#ifdef USART8
  else if (addr == USART8)
  {  // NOLINT
    return stm32_uart_id_t::STM32_USART8;
  }
#endif
#ifdef USART9
  else if (addr == USART9)
  {  // NOLINT
    return stm32_uart_id_t::STM32_USART9;
  }
#endif
#ifdef USART10
  else if (addr == USART10)
  {  // NOLINT
    return stm32_uart_id_t::STM32_USART10;
  }
#endif
#ifdef USART11
  else if (addr == USART11)
  {  // NOLINT
    return stm32_uart_id_t::STM32_USART11;
  }
#endif
#ifdef USART12
  else if (addr == USART12)
  {  // NOLINT
    return stm32_uart_id_t::STM32_USART12;
  }
#endif
#ifdef USART13
  else if (addr == USART13)
  {  // NOLINT
    return stm32_uart_id_t::STM32_USART13;
  }
#endif
#ifdef UART1
  else if (addr == UART1)
  {  // NOLINT
    return stm32_uart_id_t::STM32_UART1;
  }
#endif
#ifdef UART2
  else if (addr == UART2)
  {  // NOLINT
    return stm32_uart_id_t::STM32_UART2;
  }
#endif
#ifdef UART3
  else if (addr == UART3)
  {  // NOLINT
    return stm32_uart_id_t::STM32_UART3;
  }
#endif
#ifdef UART4
  else if (addr == UART4)
  {  // NOLINT
    return stm32_uart_id_t::STM32_UART4;
  }
#endif
#ifdef UART5
  else if (addr == UART5)
  {  // NOLINT
    return stm32_uart_id_t::STM32_UART5;
  }
#endif
#ifdef UART6
  else if (addr == UART6)
  {  // NOLINT
    return stm32_uart_id_t::STM32_UART6;
  }
#endif
#ifdef UART7
  else if (addr == UART7)
  {  // NOLINT
    return stm32_uart_id_t::STM32_UART7;
  }
#endif
#ifdef UART8
  else if (addr == UART8)
  {  // NOLINT
    return stm32_uart_id_t::STM32_UART8;
  }
#endif
#ifdef UART9
  else if (addr == UART9)
  {  // NOLINT
    return stm32_uart_id_t::STM32_UART9;
  }
#endif
#ifdef UART10
  else if (addr == UART10)
  {  // NOLINT
    return stm32_uart_id_t::STM32_UART10;
  }
#endif
#ifdef UART11
  else if (addr == UART11)
  {  // NOLINT
    return stm32_uart_id_t::STM32_UART11;
  }
#endif
#ifdef UART12
  else if (addr == UART12)
  {  // NOLINT
    return stm32_uart_id_t::STM32_UART12;
  }
#endif
#ifdef UART13
  else if (addr == UART13)
  {  // NOLINT
    return stm32_uart_id_t::STM32_UART13;
  }
#endif
#ifdef LPUART1
  else if (addr == LPUART1)
  {  // NOLINT
    return stm32_uart_id_t::STM32_LPUART1;
  }
#endif
#ifdef LPUART2
  else if (addr == LPUART2)
  {  // NOLINT
    return stm32_uart_id_t::STM32_LPUART2;
  }
#endif
#ifdef LPUART3
  else if (addr == LPUART3)
  {  // NOLINT
    return stm32_uart_id_t::STM32_LPUART3;
  }
#endif
  else
  {
    return stm32_uart_id_t::STM32_UART_ID_ERROR;
  }
}

// NOLINTNEXTLINE
extern "C" void STM32_UART_ISR_Handler_IDLE(UART_HandleTypeDef *uart_handle)
{
  if (__HAL_UART_GET_FLAG(uart_handle, UART_FLAG_IDLE))
  {
    __HAL_UART_CLEAR_IDLEFLAG(uart_handle);

    auto uart = STM32UART::map[STM32_UART_GetID(uart_handle->Instance)];
    auto rx_buf = static_cast<uint8_t *>(uart->dma_buff_rx_.addr_);
    size_t dma_size = uart->dma_buff_rx_.size_;

    size_t curr_pos =
        dma_size - __HAL_DMA_GET_COUNTER(uart_handle->hdmarx);  // 当前 DMA 写入位置
    size_t last_pos = uart->last_rx_pos_;

    if (curr_pos != last_pos)
    {
      if (curr_pos > last_pos)
      {
        // 线性接收区
        uart->read_port_->queue_data_->PushBatch(&rx_buf[last_pos], curr_pos - last_pos);
      }
      else
      {
        // 回卷区：last→end，再从0→curr
        uart->read_port_->queue_data_->PushBatch(&rx_buf[last_pos], dma_size - last_pos);
        uart->read_port_->queue_data_->PushBatch(&rx_buf[0], curr_pos);
      }

      uart->last_rx_pos_ = curr_pos;
      uart->read_port_->ProcessPendingReads(true);
    }
  }
}

void STM32_UART_ISR_Handler_TX_CPLT(stm32_uart_id_t id)
{  // NOLINT
  auto uart = STM32UART::map[id];

  WriteInfoBlock &current_info = uart->write_info_active_;

  uart->write_port_->write_size_ = uart->uart_handle_->TxXferSize;
  current_info.op.UpdateStatus(true, ErrorCode::OK);

  if (!uart->dma_buff_tx_.HasPending())
  {
    return;
  }

  if (uart->write_port_->queue_info_->Pop(current_info) != ErrorCode::OK)
  {
    ASSERT(false);
    return;
  }

  uart->dma_buff_tx_.Switch();

  HAL_UART_Transmit_DMA(uart->uart_handle_,
                        static_cast<uint8_t *>(uart->dma_buff_tx_.ActiveBuffer()),
                        current_info.data.size_);

  current_info.op.MarkAsRunning();

  WriteInfoBlock next_info;

  if (uart->write_port_->queue_info_->Peek(next_info) != ErrorCode::OK)
  {
    return;
  }

  if (uart->write_port_->queue_data_->PopBatch(
          reinterpret_cast<uint8_t *>(uart->dma_buff_tx_.PendingBuffer()),
          next_info.data.size_) != ErrorCode::OK)
  {
    ASSERT(false);
    return;
  }

  uart->dma_buff_tx_.EnablePending();
}

extern "C" void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  STM32_UART_ISR_Handler_TX_CPLT(STM32_UART_GetID(huart->Instance));
}

extern "C" __attribute__((used)) void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  HAL_UART_Abort_IT(huart);
}

extern "C" void HAL_UART_AbortCpltCallback(UART_HandleTypeDef *huart)
{
  auto uart = STM32UART::map[STM32_UART_GetID(huart->Instance)];
  HAL_UART_Receive_DMA(huart, huart->pRxBuffPtr, uart->dma_buff_rx_.size_);
  uart->last_rx_pos_ = 0;
  WriteInfoBlock info;
  if (uart->write_port_->queue_info_->Peek(info) == ErrorCode::OK)
  {
    uart->write_port_->Finish(true, ErrorCode::FAILED, info, 0);
  }
}

extern "C" void HAL_UART_AbortTransmitCpltCallback(UART_HandleTypeDef *huart)
{
  auto uart = STM32UART::map[STM32_UART_GetID(huart->Instance)];
  WriteInfoBlock info;
  if (uart->write_port_->queue_info_->Peek(info) == ErrorCode::OK)
  {
    uart->write_port_->Finish(true, ErrorCode::FAILED, info, 0);
  }
}

extern "C" void HAL_UART_AbortReceiveCpltCallback(UART_HandleTypeDef *huart)
{
  HAL_UART_Receive_DMA(
      huart, huart->pRxBuffPtr,
      STM32UART::map[STM32_UART_GetID(huart->Instance)]->dma_buff_rx_.size_);
}

#endif
