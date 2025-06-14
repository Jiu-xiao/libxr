#pragma once

#include "main.h"

#ifdef HAL_UART_MODULE_ENABLED

#ifdef UART
#undef UART
#endif

#include "double_buffer.hpp"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "uart.hpp"

typedef enum
{
#ifdef USART1
  STM32_USART1,
#endif
#ifdef USART2
  STM32_USART2,
#endif
#ifdef USART3
  STM32_USART3,
#endif
#ifdef USART4
  STM32_USART4,
#endif
#ifdef USART5
  STM32_USART5,
#endif
#ifdef USART6
  STM32_USART6,
#endif
#ifdef USART7
  STM32_USART7,
#endif
#ifdef USART8
  STM32_USART8,
#endif
#ifdef USART9
  STM32_USART9,
#endif
#ifdef USART10
  STM32_USART10,
#endif
#ifdef USART11
  STM32_USART11,
#endif
#ifdef USART12
  STM32_USART12,
#endif
#ifdef USART13
  STM32_USART13,
#endif
#ifdef UART1
  STM32_UART1,
#endif
#ifdef UART2
  STM32_UART2,
#endif
#ifdef UART3
  STM32_UART3,
#endif
#ifdef UART4
  STM32_UART4,
#endif
#ifdef UART5
  STM32_UART5,
#endif
#ifdef UART6
  STM32_UART6,
#endif
#ifdef UART7
  STM32_UART7,
#endif
#ifdef UART8
  STM32_UART8,
#endif
#ifdef UART9
  STM32_UART9,
#endif
#ifdef UART10
  STM32_UART10,
#endif
#ifdef UART11
  STM32_UART11,
#endif
#ifdef UART12
  STM32_UART12,
#endif
#ifdef UART13
  STM32_UART13,
#endif
#ifdef LPUART1
  STM32_LPUART1,
#endif
#ifdef LPUART2
  STM32_LPUART2,
#endif
#ifdef LPUART3
  STM32_LPUART3,
#endif
  STM32_UART_NUMBER,
  STM32_UART_ID_ERROR
} stm32_uart_id_t;

stm32_uart_id_t STM32_UART_GetID(USART_TypeDef *addr);

namespace LibXR
{
class STM32UART : public UART
{
 public:
  static ErrorCode WriteFun(WritePort &port)
  {
    STM32UART *uart = CONTAINER_OF(&port, STM32UART, _write_port);
    if (!uart->dma_buff_tx_.HasPending())
    {
      WriteInfoBlock info;
      if (port.queue_info_->Peek(info) != ErrorCode::OK)
      {
        return ErrorCode::EMPTY;
      }

      uint8_t *buffer = nullptr;
      bool use_pending = false;

      if (uart->uart_handle_->gState == HAL_UART_STATE_READY)
      {
        buffer = reinterpret_cast<uint8_t *>(uart->dma_buff_tx_.ActiveBuffer());
      }
      else
      {
        buffer = reinterpret_cast<uint8_t *>(uart->dma_buff_tx_.PendingBuffer());
        use_pending = true;
      }

      if (port.queue_data_->PopBatch(reinterpret_cast<uint8_t *>(buffer),
                                     info.data.size_) != ErrorCode::OK)
      {
        ASSERT(false);
        return ErrorCode::EMPTY;
      }

      if (use_pending)
      {
        uart->dma_buff_tx_.EnablePending();
        if (uart->uart_handle_->gState == HAL_UART_STATE_READY)
        {
          uart->dma_buff_tx_.Switch();
        }
        else
        {
          return ErrorCode::FAILED;
        }
      }

      port.queue_info_->Pop(uart->write_info_active_);

      auto ans = HAL_UART_Transmit_DMA(
          uart->uart_handle_, static_cast<uint8_t *>(uart->dma_buff_tx_.ActiveBuffer()),
          info.data.size_);

      if (ans != HAL_OK)
      {
        port.Finish(false, ErrorCode::FAILED, info, 0);
      }

      return ErrorCode::FAILED;
    }

    return ErrorCode::FAILED;
  }

  static ErrorCode ReadFun(ReadPort &port)
  {
    STM32UART *uart = CONTAINER_OF(&port, STM32UART, _read_port);
    UNUSED(uart);

    return ErrorCode::EMPTY;
  }

  STM32UART(UART_HandleTypeDef *uart_handle, RawData dma_buff_rx, RawData dma_buff_tx,
            uint32_t tx_queue_size = 5)
      : UART(&_read_port, &_write_port),
        _read_port(dma_buff_rx.size_),
        _write_port(tx_queue_size, dma_buff_tx.size_ / 2),
        dma_buff_rx_(dma_buff_rx),
        dma_buff_tx_(dma_buff_tx),
        uart_handle_(uart_handle),
        id_(STM32_UART_GetID(uart_handle_->Instance))
  {
    ASSERT(id_ != STM32_UART_ID_ERROR);

    map[id_] = this;

    if ((uart_handle->Init.Mode & UART_MODE_TX) == UART_MODE_TX)
    {
      ASSERT(uart_handle_->hdmatx != NULL);
      _write_port = WriteFun;
    }

    if ((uart_handle->Init.Mode & UART_MODE_RX) == UART_MODE_RX)
    {
      ASSERT(uart_handle->hdmarx != NULL);

      uart_handle_->hdmarx->Init.Mode = DMA_CIRCULAR;
      HAL_DMA_Init(uart_handle_->hdmarx);

      __HAL_UART_ENABLE_IT(uart_handle, UART_IT_IDLE);

      HAL_UART_Receive_DMA(uart_handle, reinterpret_cast<uint8_t *>(dma_buff_rx_.addr_),
                           dma_buff_rx_.size_);
      _read_port = ReadFun;
    }
  }

  ErrorCode SetConfig(UART::Configuration config)
  {
    uart_handle_->Init.BaudRate = config.baudrate;

    switch (config.parity)
    {
      case UART::Parity::NO_PARITY:
        uart_handle_->Init.Parity = UART_PARITY_NONE;
        uart_handle_->Init.WordLength = UART_WORDLENGTH_8B;
        break;
      case UART::Parity::EVEN:
        uart_handle_->Init.Parity = UART_PARITY_EVEN;
        uart_handle_->Init.WordLength = UART_WORDLENGTH_9B;
        break;
      case UART::Parity::ODD:
        uart_handle_->Init.Parity = UART_PARITY_ODD;
        uart_handle_->Init.WordLength = UART_WORDLENGTH_9B;
        break;
      default:
        ASSERT(false);
    }

    switch (config.stop_bits)
    {
      case 1:
        uart_handle_->Init.StopBits = UART_STOPBITS_1;
        break;
      case 2:
        uart_handle_->Init.StopBits = UART_STOPBITS_2;
        break;
      default:
        ASSERT(false);
    }

    if (HAL_UART_Init(uart_handle_) != HAL_OK)
    {
      return ErrorCode::INIT_ERR;
    }
    return ErrorCode::OK;
  }

  ReadPort _read_port;
  WritePort _write_port;

  RawData dma_buff_rx_;
  DoubleBuffer dma_buff_tx_;
  WriteInfoBlock write_info_active_;

  size_t last_rx_pos_ = 0;

  UART_HandleTypeDef *uart_handle_;

  stm32_uart_id_t id_ = STM32_UART_ID_ERROR;

  static STM32UART *map[STM32_UART_NUMBER];  // NOLINT
};

}  // namespace LibXR

#endif
