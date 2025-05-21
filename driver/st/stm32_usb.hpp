#pragma once

#include "double_buffer.hpp"
#include "main.h"
#include "stm32_usbx.hpp"

#if defined(HAL_PCD_MODULE_ENABLED) && !defined(LIBXR_SYSTEM_ThreadX)

#ifdef UART
#undef UART
#endif

#include <type_traits>
#include <utility>

#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "uart.hpp"
#include "usbd_cdc.h"
#include "usbd_cdc_if.h"

extern uint8_t UserRxBufferFS[APP_RX_DATA_SIZE];
extern uint8_t UserTxBufferFS[APP_TX_DATA_SIZE];

int8_t libxr_stm32_virtual_uart_init(void);

int8_t libxr_stm32_virtual_uart_deinit(void);

int8_t libxr_stm32_virtual_uart_control(uint8_t cmd, uint8_t *pbuf, uint16_t len);

int8_t libxr_stm32_virtual_uart_receive(uint8_t *pbuf, uint32_t *Len);

int8_t libxr_stm32_virtual_uart_transmit(uint8_t *pbuf, uint32_t *Len, uint8_t epnum);

namespace LibXR
{
class STM32VirtualUART : public UART
{
 public:
  template <typename, typename = void>
  struct HasTransmitCplt : std::false_type
  {
  };

  template <typename T>
  struct HasTransmitCplt<T, std::void_t<decltype(std::declval<T>().TransmitCplt)>>
      : std::true_type
  {
  };
  template <typename T>
  typename std::enable_if<!HasTransmitCplt<T>::value, USBD_CDC_ItfTypeDef>::type Apply()
  {
    return {libxr_stm32_virtual_uart_init, libxr_stm32_virtual_uart_deinit,
            libxr_stm32_virtual_uart_control, libxr_stm32_virtual_uart_receive};
  }

  template <typename T>
  typename std::enable_if<HasTransmitCplt<T>::value, USBD_CDC_ItfTypeDef>::type Apply()
  {
    return {libxr_stm32_virtual_uart_init, libxr_stm32_virtual_uart_deinit,
            libxr_stm32_virtual_uart_control, libxr_stm32_virtual_uart_receive,
            libxr_stm32_virtual_uart_transmit};
  }

  using WriteFunctionType = ErrorCode (*)(WritePort &);

  static ErrorCode WriteFun(WritePort &port)
  {
    STM32VirtualUART *uart = CONTAINER_OF(&port, STM32VirtualUART, _write_port);
    auto p_data_class =
        reinterpret_cast<USBD_CDC_HandleTypeDef *>(uart->usb_handle_->pClassData);

    WriteInfoBlock info;

    if (p_data_class == nullptr)
    {
      port.queue_info_->Pop(info);
      port.queue_data_->PopBatch(uart->tx_buffer_.PendingBuffer(), info.data.size_);
      port.Finish(false, ErrorCode::INIT_ERR, info, 0);
      return ErrorCode::INIT_ERR;
    }

    if (uart->tx_buffer_.HasPending())
    {
      return ErrorCode::FULL;
    }

    if (port.queue_info_->Peek(info) != ErrorCode::OK)
    {
      return ErrorCode::EMPTY;
    }

    if (port.queue_data_->PopBatch(uart->tx_buffer_.PendingBuffer(), info.data.size_) !=
        ErrorCode::OK)
    {
      ASSERT(false);
      return ErrorCode::EMPTY;
    }

    uart->tx_buffer_.EnablePending();

#if defined(STM32F1)
    if (!uart->writing_ && p_data_class->TxState == 0)
#else
    if (p_data_class->TxState == 0)
#endif
    {
      uart->tx_buffer_.Switch();
      port.queue_info_->Pop(uart->write_info_active_);

#if defined(STM32F1)
      uart->write_size_ = info.data.size_;
      uart->writing_ = true;
#endif

      USBD_CDC_SetTxBuffer(uart->usb_handle_, uart->tx_buffer_.ActiveBuffer(),
                           info.data.size_);
      USBD_CDC_TransmitPacket(uart->usb_handle_);

      info.op.MarkAsRunning();

      return ErrorCode::FAILED;
    }
    return ErrorCode::FAILED;
  }

  static ErrorCode ReadFun(ReadPort &port)
  {
    UNUSED(port);
    return ErrorCode::EMPTY;
  }

  STM32VirtualUART(USBD_HandleTypeDef &usb_handle, uint8_t *tx_buffer = UserTxBufferFS,
                   uint8_t *rx_buffer = UserRxBufferFS, uint32_t tx_queue_size = 5)
      : UART(&_read_port, &_write_port),
        usb_handle_(&usb_handle),
        tx_buffer_(RawData(tx_buffer, APP_TX_DATA_SIZE)),
        rx_buffer_(RawData(rx_buffer, APP_RX_DATA_SIZE)),
        _write_port(tx_queue_size, APP_TX_DATA_SIZE),
        _read_port(APP_RX_DATA_SIZE)
  {
    map[0] = this;

    static USBD_CDC_ItfTypeDef usbd_cdc_itf = Apply<USBD_CDC_ItfTypeDef>();

    USBD_CDC_RegisterInterface(usb_handle_, &usbd_cdc_itf);

    _write_port = WriteFun;
    _read_port = ReadFun;

    USBD_CDC_ReceivePacket(usb_handle_);
  }

  ErrorCode SetConfig(UART::Configuration config)
  {
    UNUSED(config);
    return ErrorCode::OK;
  }

  static STM32VirtualUART *map[1];  // NOLINT

  USBD_HandleTypeDef *usb_handle_ = nullptr;
  DoubleBuffer tx_buffer_;
  DoubleBuffer rx_buffer_;

  WritePort _write_port;
  ReadPort _read_port;

  WriteInfoBlock write_info_active_;
  ReadInfoBlock read_info_active_;

#if defined(STM32F1)
  bool writing_ = false;
  uint32_t write_size_ = 0;
#endif
};

}  // namespace LibXR

#endif
