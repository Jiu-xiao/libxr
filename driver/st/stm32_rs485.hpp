#pragma once

#include "main.h"

#ifdef HAL_UART_MODULE_ENABLED

#include "flag.hpp"
#include "rs485.hpp"
#include "stm32_uart.hpp"

namespace LibXR
{

class STM32GPIO;

class STM32RS485 : public RS485
{
 public:
  STM32RS485(UART_HandleTypeDef* uart_handle, RawData rx_buffer, RawData tx_buffer,
             STM32GPIO* tx_enable = nullptr);

  ErrorCode SetConfig(const Configuration& config) override;

  ErrorCode Write(ConstRawData frame, WriteOperation& op, bool in_isr = false) override;

  void Reset() override;

  void OnRxEvent(uint16_t size, bool in_isr);

  void OnTxComplete(bool in_isr);

  void OnError(bool in_isr);

  void OnAbortComplete();

  UART_HandleTypeDef* uart_handle_;
  STM32GPIO* tx_enable_;
  RawData rx_buffer_;
  RawData tx_buffer_;
  Configuration config_;
  WriteOperation write_op_;
  AsyncBlockWait block_wait_;
  Flag::Atomic tx_busy_;
  stm32_uart_id_t id_ = STM32_UART_ID_ERROR;

  static STM32RS485* map[STM32_UART_NUMBER];  // NOLINT

 private:
  void ArmReceive();
  void SetTransmitDirection();
  void SetReceiveDirection();
  void FinishWrite(bool in_isr, ErrorCode ec);
};

}  // namespace LibXR

#endif
