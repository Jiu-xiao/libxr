// ch32_uart.hpp
#pragma once

#include "libxr.hpp"
#include DEF2STR(LIBXR_CH32_CONFIG_FILE)

#include "ch32_uart_def.hpp"
#include "double_buffer.hpp"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "uart.hpp"

namespace LibXR
{

class CH32UART : public UART
{
 public:
  CH32UART(ch32_uart_id_t id, RawData dma_rx, RawData dma_tx, GPIO_TypeDef *tx_gpio_port,
           uint16_t tx_gpio_pin, GPIO_TypeDef *rx_gpio_port, uint16_t rx_gpio_pin,
           uint32_t pin_remap = 0, uint32_t tx_queue_size = 5,
           UART::Configuration config = {115200, UART::Parity::NO_PARITY, 8, 1});

  ErrorCode SetConfig(UART::Configuration config);

  static ErrorCode WriteFun(WritePort &port);
  static ErrorCode ReadFun(ReadPort &port);

  static void DmaIRQHandler(DMA_Channel_TypeDef *channel, ch32_uart_id_t id);

  ch32_uart_id_t id_;
  uint16_t uart_mode_;

  ReadPort _read_port;
  WritePort _write_port;

  RawData dma_buff_rx_;
  DoubleBuffer dma_buff_tx_;
  WriteInfoBlock write_info_active_;

  size_t last_rx_pos_ = 0;

  USART_TypeDef *instance_;
  DMA_Channel_TypeDef *dma_rx_channel_;
  DMA_Channel_TypeDef *dma_tx_channel_;

  static CH32UART *map[CH32_UART_NUMBER];
};

}  // namespace LibXR