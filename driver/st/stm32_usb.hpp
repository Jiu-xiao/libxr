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

  static ErrorCode WriteFun(WritePort &port);

  static ErrorCode ReadFun(ReadPort &port);

  STM32VirtualUART(USBD_HandleTypeDef &usb_handle, uint8_t *tx_buffer = UserTxBufferFS,
                   uint8_t *rx_buffer = UserRxBufferFS, uint32_t tx_queue_size = 5);

  ErrorCode SetConfig(UART::Configuration config);

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
