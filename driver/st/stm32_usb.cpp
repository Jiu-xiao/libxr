#include "stm32_usb.hpp"

#if defined(HAL_PCD_MODULE_ENABLED) && !defined(LIBXR_SYSTEM_ThreadX)

using namespace LibXR;

STM32VirtualUART *STM32VirtualUART::map[1];

int8_t libxr_stm32_virtual_uart_init(void)
{
  STM32VirtualUART *uart = STM32VirtualUART::map[0];
  USBD_CDC_SetTxBuffer(STM32VirtualUART::map[0]->usb_handle_,
                       uart->tx_buffer_.ActiveBuffer(), 0);
  USBD_CDC_SetRxBuffer(STM32VirtualUART::map[0]->usb_handle_,
                       uart->rx_buffer_.ActiveBuffer());
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

#if __DCACHE_PRESENT
  SCB_InvalidateDCache_by_Addr(pbuf, *Len);
#endif

  uart->read_port_->queue_data_->PushBatch(pbuf, *Len);
  uart->read_port_->ProcessPendingReads(true);

  USBD_CDC_ReceivePacket(STM32VirtualUART::map[0]->usb_handle_);

  return (USBD_OK);
}

int8_t libxr_stm32_virtual_uart_transmit(uint8_t *pbuf, uint32_t *Len, uint8_t epnum)
{
  UNUSED(epnum);
  UNUSED(pbuf);

  STM32VirtualUART *uart = STM32VirtualUART::map[0];

  WriteInfoBlock &current_info = uart->write_info_active_;

  uart->write_port_->Finish(true, ErrorCode::OK, current_info, *Len);

  if (!uart->tx_buffer_.HasPending())
  {
    return USBD_OK;
  }

  if (uart->write_port_->queue_info_->Pop(current_info) != ErrorCode::OK)
  {
    ASSERT(false);
    return USBD_OK;
  }

  uart->tx_buffer_.Switch();

#if defined(STM32F1)
  uart->write_size_ = current_info.data.size_;
  uart->writing_ = true;
#endif

  USBD_CDC_SetTxBuffer(uart->usb_handle_, uart->tx_buffer_.ActiveBuffer(),
                       current_info.data.size_);
#if __DCACHE_PRESENT
  SCB_CleanDCache_by_Addr(reinterpret_cast<uint32_t *>(uart->tx_buffer_.ActiveBuffer()), *Len);
#endif
  USBD_CDC_TransmitPacket(uart->usb_handle_);

  current_info.op.MarkAsRunning();

  WriteInfoBlock next_info;

  if (uart->write_port_->queue_info_->Peek(next_info) != ErrorCode::OK)
  {
    return USBD_OK;
  }

  if (uart->write_port_->queue_data_->PopBatch(uart->tx_buffer_.PendingBuffer(),
                                               next_info.data.size_) != ErrorCode::OK)
  {
    ASSERT(false);
    return USBD_OK;
  }

  uart->tx_buffer_.EnablePending();

  return (USBD_OK);
}

#if defined(STM32F1)
extern "C" void STM32_USB_ISR_Handler_F1(void)
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
    libxr_stm32_virtual_uart_transmit(STM32VirtualUART::map[0]->tx_buffer_.ActiveBuffer(),
                                      &STM32VirtualUART::map[0]->write_size_, 0);
  }
}
#endif

#endif
