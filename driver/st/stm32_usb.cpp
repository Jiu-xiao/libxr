#include "stm32_usb.hpp"

#ifdef HAL_PCD_MODULE_ENABLED

using namespace LibXR;

STM32VirtualUART *STM32VirtualUART::map[1];

int8_t libxr_stm32_virtual_uart_init(void)
{
  STM32VirtualUART *uart = STM32VirtualUART::map[0];
  USBD_CDC_SetTxBuffer(STM32VirtualUART::map[0]->usb_handle_, uart->tx_buffer_, 0);
  USBD_CDC_SetRxBuffer(STM32VirtualUART::map[0]->usb_handle_, uart->rx_buffer_);
  return (USBD_OK);
}

int8_t libxr_stm32_virtual_uart_deinit(void) { return (USBD_OK); }

int8_t libxr_stm32_virtual_uart_control(uint8_t cmd, uint8_t *pbuf, uint16_t len)
{
  UNUSED(cmd);
  UNUSED(pbuf);
  UNUSED(len);
  return (USBD_OK);
}

int8_t libxr_stm32_virtual_uart_receive(uint8_t *pbuf, uint32_t *Len)
{
  STM32VirtualUART *uart = STM32VirtualUART::map[0];

  uart->read_port_.queue_data_->PushBatch(pbuf, *Len);
  uart->read_port_.ProcessPendingReads();

  USBD_CDC_ReceivePacket(STM32VirtualUART::map[0]->usb_handle_);

  return (USBD_OK);
}

int8_t libxr_stm32_virtual_uart_transmit(uint8_t *pbuf, uint32_t *Len, uint8_t epnum)
{
  UNUSED(epnum);
  UNUSED(pbuf);

  STM32VirtualUART *uart = STM32VirtualUART::map[0];

  WritePort::WriteInfo info;
  if (uart->write_port_.queue_info_->Pop(info) != ErrorCode::OK)
  {
    return USBD_OK;
  }

  uart->write_port_.write_size_ = *Len;
  info.op.UpdateStatus(true, ErrorCode::OK);

  if (uart->write_port_.queue_info_->Peek(info) != ErrorCode::OK)
  {
    return USBD_OK;
  }

  if (uart->write_port_.queue_data_->PopBatch(uart->tx_buffer_, info.size) !=
      ErrorCode::OK)
  {
    ASSERT(false);
    return USBD_OK;
  }

#if defined(STM32F1)
  uart->write_size_ = info.size;
  uart->writing_ = true;
#endif

  USBD_CDC_SetTxBuffer(uart->usb_handle_, uart->tx_buffer_, info.size);
  USBD_CDC_TransmitPacket(uart->usb_handle_);

  info.op.MarkAsRunning();

  return (USBD_OK);
}

#if defined(STM32F1)
extern "C" void libxr_stm32_transmit_complete_check(void)
{
  if (STM32VirtualUART::map[0] == nullptr)
  {
    return;
  }

  auto p_data_class = reinterpret_cast<USBD_CDC_HandleTypeDef *>(
      STM32VirtualUART::map[0]->usb_handle_->pClassData);

  if (p_data_class == nullptr)
  {
    return;
  }

  if (STM32VirtualUART::map[0]->writing_ && p_data_class->TxState == 0)
  {
    STM32VirtualUART::map[0]->writing_ = false;
    libxr_stm32_virtual_uart_transmit(STM32VirtualUART::map[0]->tx_buffer_,
                                      &STM32VirtualUART::map[0]->write_size_, 0);
  }
}
#endif

#endif
