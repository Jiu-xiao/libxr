#pragma once

#include "double_buffer.hpp"
#include "driverlib.h"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "uart.hpp"

typedef enum
{
#ifdef UART0_BASE
  MSPM0_UART0,
#endif
#ifdef UART1_BASE
  MSPM0_UART1,
#endif
#ifdef UART2_BASE
  MSPM0_UART2,
#endif
#ifdef UART3_BASE
  MSPM0_UART3,
#endif
  MSPM0_UART_NUMBER,
  MSPM0_UART_ID_ERROR = 255
} mspm0_uart_id_t;

mspm0_uart_id_t MSPM0_UART_GetID(UART_Regs *addr);

namespace LibXR
{

class MSPM0UART : public UART
{
 public:
  static ErrorCode WriteFun(WritePort &port);

  static ErrorCode ReadFun(ReadPort &port);

  MSPM0UART(UART_Regs *uart_handle, DMA_Regs *dma_handle, uint8_t rx_dma_ch,
            uint8_t tx_dma_ch, RawData dma_buff_rx, RawData dma_buff_tx,
            uint32_t tx_queue_size = 5);

  ErrorCode SetConfig(UART::Configuration config);

  ReadPort _read_port;
  WritePort _write_port;

  RawData dma_buff_rx_;
  DoubleBuffer dma_buff_tx_;
  WriteInfoBlock write_info_active_;

  size_t last_rx_pos_ = 0;

  UART_Regs *uart_regs_;
  DMA_Regs *dma_regs_;
  uint8_t dma_ch_rx_;
  uint8_t dma_ch_tx_;

  mspm0_uart_id_t id_ = MSPM0_UART_ID_ERROR;

  static MSPM0UART *map[4];  // NOLINT (Maximum 4 UART instances)
};

// ISR Handler declarations
void MSPM0_UART_ISR_Handler_RX(UART_Regs *uart_base);
void MSPM0_UART_ISR_Handler_TX_DMA_Done(mspm0_uart_id_t id);
void MSPM0_UART_ISR_Handler_RX_DMA_Done(mspm0_uart_id_t id);

}  // namespace LibXR