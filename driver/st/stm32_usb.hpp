#pragma once

#include "main.h"

#ifdef HAL_PCD_MODULE_ENABLED

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

int8_t libxr_stm32_virtual_uart_control(uint8_t cmd, uint8_t *pbuf,
                                        uint16_t len);

int8_t libxr_stm32_virtual_uart_receive(uint8_t *pbuf, uint32_t *Len);

int8_t libxr_stm32_virtual_uart_transmit(uint8_t *pbuf, uint32_t *Len,
                                         uint8_t epnum);

namespace LibXR {
class STM32VirtualUART : public UART {
 public:
  template <typename, typename = void>
  struct HasTransmitCplt : std::false_type {};

  template <typename T>
  struct HasTransmitCplt<T,
                         std::void_t<decltype(std::declval<T>().TransmitCplt)>>
      : std::true_type {};
  template <typename T>
  typename std::enable_if<!HasTransmitCplt<T>::value, USBD_CDC_ItfTypeDef>::type
  Apply() {
    return {libxr_stm32_virtual_uart_init, libxr_stm32_virtual_uart_deinit,
            libxr_stm32_virtual_uart_control, libxr_stm32_virtual_uart_receive};
  }

  template <typename T>
  typename std::enable_if<HasTransmitCplt<T>::value, USBD_CDC_ItfTypeDef>::type
  Apply() {
    return {libxr_stm32_virtual_uart_init, libxr_stm32_virtual_uart_deinit,
            libxr_stm32_virtual_uart_control, libxr_stm32_virtual_uart_receive,
            libxr_stm32_virtual_uart_transmit};
  }

  using WriteFunctionType = ErrorCode (*)(WritePort &);

  template <typename T>
  static typename std::enable_if<!HasTransmitCplt<T>::value, ErrorCode>::type
  WriteFun(WritePort &port) {
    STM32VirtualUART *uart = CONTAINER_OF(&port, STM32VirtualUART, write_port_);
    auto p_data_class = reinterpret_cast<USBD_CDC_HandleTypeDef *>(
        uart->usb_handle_->pClassData);

    while (true) {
      if(p_data_class->TxState != 0) {
        continue;
      }
      size_t need_write = 0;
      port.Flush();

      if (port.queue_data_->PopBlock(uart->tx_buffer_, &need_write) !=
          ErrorCode::OK) {
        return ErrorCode::EMPTY;
      }

      USBD_CDC_SetTxBuffer(uart->usb_handle_, uart->tx_buffer_, need_write);
      USBD_CDC_TransmitPacket(uart->usb_handle_);

      WriteOperation op;
      port.queue_op_->Pop(op);
      op.UpdateStatus(false, ErrorCode::OK);
      return ErrorCode::OK;
    }
  }

  template <typename T>
  static typename std::enable_if<HasTransmitCplt<T>::value, ErrorCode>::type
  WriteFun(WritePort &port) {
    STM32VirtualUART *uart = CONTAINER_OF(&port, STM32VirtualUART, write_port_);
    auto p_data_class = reinterpret_cast<USBD_CDC_HandleTypeDef *>(
        uart->usb_handle_->pClassData);

    if (p_data_class->TxState == 0) {
      size_t need_write = 0;
      port.Flush();

      if (port.queue_data_->PopBlock(uart->tx_buffer_, &need_write) !=
          ErrorCode::OK) {
        return ErrorCode::EMPTY;
      }

      USBD_CDC_SetTxBuffer(uart->usb_handle_, uart->tx_buffer_, need_write);
      USBD_CDC_TransmitPacket(uart->usb_handle_);

      WriteOperation op;
      if (port.queue_op_) {
        port.queue_op_->Peek(op);
        op.MarkAsRunning();
      }
      return ErrorCode::OK;
    }
    return ErrorCode::OK;
  }

  static ErrorCode ReadFun(ReadPort &port) {
    ReadInfoBlock block;

    if (port.queue_block_->Peek(block) != ErrorCode::OK) {
      return ErrorCode::EMPTY;
    }

    block.op_.MarkAsRunning();

    if (port.queue_data_->Size() >= block.data_.size_) {
      port.queue_data_->PopBatch(block.data_.addr_, block.data_.size_);
      port.queue_block_->Pop();

      port.read_size_ = block.data_.size_;
      block.op_.UpdateStatus(false, ErrorCode::OK);
      return ErrorCode::OK;
    } else {
      return ErrorCode::EMPTY;
    }
  }

  template <typename T>
  WriteFunctionType GetWriteFunction() {
    return &WriteFun<T>;
  }

  STM32VirtualUART(USBD_HandleTypeDef &usb_handle,
                   uint8_t *tx_buffer = UserTxBufferFS,
                   uint8_t *rx_buffer = UserRxBufferFS,
                   uint32_t rx_queue_size = 5, uint32_t tx_queue_size = 5)
      : UART(ReadPort(rx_queue_size, APP_RX_DATA_SIZE),
             WritePort(tx_queue_size, APP_TX_DATA_SIZE)),
        usb_handle_(&usb_handle),
        tx_buffer_(tx_buffer),
        rx_buffer_(rx_buffer) {
    map[0] = this;

    static USBD_CDC_ItfTypeDef usbd_cdc_itf = Apply<USBD_CDC_ItfTypeDef>();

    USBD_CDC_RegisterInterface(usb_handle_, &usbd_cdc_itf);

    write_port_ = GetWriteFunction<USBD_CDC_ItfTypeDef>();
    read_port_ = ReadFun;

    USBD_CDC_ReceivePacket(usb_handle_);
  }

  ErrorCode SetConfig(UART::Configuration config) {
    UNUSED(config);
    return ErrorCode::OK;
  }

  static STM32VirtualUART *map[1];  // NOLINT

  USBD_HandleTypeDef *usb_handle_ = nullptr;
  uint8_t *tx_buffer_ = nullptr;
  uint8_t *rx_buffer_ = nullptr;
};

}  // namespace LibXR

#endif
