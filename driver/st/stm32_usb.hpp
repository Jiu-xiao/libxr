#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "stm32_uart_def.h"
#include "uart.hpp"
#include "usbd_cdc.h"
#include "usbd_cdc_if.h"

extern uint8_t UserRxBufferFS[APP_RX_DATA_SIZE];  // NOLINT
extern uint8_t UserTxBufferFS[APP_TX_DATA_SIZE];  // NOLINT

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
  static ErrorCode WriteFun(WritePort &port) {
    STM32VirtualUART *uart = CONTAINER_OF(&port, STM32VirtualUART, write_port_);
    auto p_data_class = reinterpret_cast<USBD_CDC_HandleTypeDef *>(
        uart->usb_handle_->pClassData);

    if (p_data_class->TxState == 0) {
      size_t need_write = 0;
      port.Flush();
      if (port.queue_data_->PopBlock(UserTxBufferFS, &need_write) !=
          ErrorCode::OK) {
        return ErrorCode::EMPTY;
      }

      USBD_CDC_SetTxBuffer(uart->usb_handle_, UserTxBufferFS, need_write);
      USBD_CDC_TransmitPacket(uart->usb_handle_);

      WriteOperation op;
      port.queue_op_->Peek(op);
      op.MarkAsRunning();
    } else {
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

  STM32VirtualUART(USBD_HandleTypeDef &usb_handle, uint32_t rx_queue_size = 5,
                   uint32_t tx_queue_size = 5)
      : UART(ReadPort(rx_queue_size, APP_RX_DATA_SIZE),
             WritePort(tx_queue_size, APP_TX_DATA_SIZE)),
        usb_handle_(&usb_handle) {
    map[0] = this;

    static USBD_CDC_ItfTypeDef usbd_cdc_itf = {
        libxr_stm32_virtual_uart_init,     libxr_stm32_virtual_uart_deinit,
        libxr_stm32_virtual_uart_control,  libxr_stm32_virtual_uart_receive,
        libxr_stm32_virtual_uart_transmit,
    };

    USBD_CDC_RegisterInterface(usb_handle_, &usbd_cdc_itf);

    write_port_ = WriteFun;
    read_port_ = ReadFun;

    USBD_CDC_ReceivePacket(usb_handle_);
  }

  void CheckReceive() {
    ReadInfoBlock block;
    while (read_port_.queue_block_->Peek(block) == ErrorCode::OK) {
      if (read_port_.queue_data_->Size() >= block.data_.size_) {
        read_port_.queue_data_->PopBatch(block.data_.addr_, block.data_.size_);
        read_port_.read_size_ = block.data_.size_;
        block.op_.UpdateStatus(true, ErrorCode::OK);
        read_port_.queue_block_->Pop();
      } else {
        break;
      }
    }
  }

  ErrorCode SetConfig(UART::Configuration config) {
    UNUSED(config);
    return ErrorCode::OK;
  }

  static STM32VirtualUART *map[1];  // NOLINT

  USBD_HandleTypeDef *usb_handle_ = nullptr;
};

}  // namespace LibXR
