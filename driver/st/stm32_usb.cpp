#include "stm32_usb.hpp"

#ifdef HAL_PCD_MODULE_ENABLED

using namespace LibXR;

STM32VirtualUART *STM32VirtualUART::map[1];

int8_t libxr_stm32_virtual_uart_init(void) {
  STM32VirtualUART *uart = STM32VirtualUART::map[0];
  USBD_CDC_SetTxBuffer(STM32VirtualUART::map[0]->usb_handle_, uart->tx_buffer_,
                       0);
  USBD_CDC_SetRxBuffer(STM32VirtualUART::map[0]->usb_handle_, uart->rx_buffer_);
  return (USBD_OK);
}

int8_t libxr_stm32_virtual_uart_deinit(void) { return (USBD_OK); }

int8_t libxr_stm32_virtual_uart_control(uint8_t cmd, uint8_t *pbuf,
                                        uint16_t len) {
  UNUSED(cmd);
  UNUSED(pbuf);
  UNUSED(len);
  return (USBD_OK);
}

int8_t libxr_stm32_virtual_uart_receive(uint8_t *pbuf, uint32_t *Len) {
  STM32VirtualUART *uart = STM32VirtualUART::map[0];

  uart->read_port_.queue_data_->PushBatch(pbuf, *Len);
  uart->read_port_.ProcessPendingReads();

  USBD_CDC_ReceivePacket(STM32VirtualUART::map[0]->usb_handle_);

  return (USBD_OK);
}

int8_t libxr_stm32_virtual_uart_transmit(uint8_t *pbuf, uint32_t *Len,
                                         uint8_t epnum) {
  UNUSED(epnum);
  UNUSED(pbuf);

  STM32VirtualUART *uart = STM32VirtualUART::map[0];

  WriteOperation op;
  if (uart->write_port_.queue_op_->Pop(op) != ErrorCode::OK) {
    return USBD_OK;
  }

  uart->write_port_.write_size_ = *Len;
  op.UpdateStatus(true, ErrorCode::OK);

  size_t size = 0;

  if (uart->write_port_.queue_data_->PopBlockFromCallback(
          uart->tx_buffer_, &size, true) != ErrorCode::OK) {
    return USBD_OK;
  }

  ASSERT(size > 0);

  USBD_CDC_SetTxBuffer(uart->usb_handle_, uart->tx_buffer_, size);
  USBD_CDC_TransmitPacket(uart->usb_handle_);

  uart->write_port_.queue_op_->Peek(op);
  op.MarkAsRunning();

  return (USBD_OK);
}

#endif
