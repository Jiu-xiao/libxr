#pragma once

#include "libxr.hpp"
#include DEF2STR(LIBXR_CH32_CONFIG_FILE)

#include "ch32_uart_def.hpp"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "uart.hpp"
#include "uart_circular_dma_rx_model.hpp"
#include "uart_dma_tx_model.hpp"

namespace LibXR
{

/**
 * @brief CH32 UART 驱动实现 / CH32 UART driver implementation
 */
class CH32UART : public UART
{
  friend class UartCircularDmaRxModel;
  friend class UartDmaTxModel<CH32UART>;

 public:
  /**
   * @brief 构造 UART 对象 / Construct UART object
   */
  CH32UART(ch32_uart_id_t id, RawData dma_rx, RawData dma_tx, GPIO_TypeDef* tx_gpio_port,
           uint16_t tx_gpio_pin, GPIO_TypeDef* rx_gpio_port, uint16_t rx_gpio_pin,
           uint32_t pin_remap = 0, uint32_t tx_queue_size = 5,
           UART::Configuration config = {115200, UART::Parity::NO_PARITY, 8, 1});

  ErrorCode SetConfig(UART::Configuration config);

  static ErrorCode WriteFun(WritePort& port, bool in_isr);
  static ErrorCode ReadFun(ReadPort& port, bool in_isr);

  void TxDmaIRQHandler();
  void RxDmaIRQHandler();

  ch32_uart_id_t id_;
  uint16_t uart_mode_;

  ReadPort _read_port;
  WritePort _write_port;

  UartCircularDmaRxModel rx_dma_model_;
  UartDmaTxModel<CH32UART> tx_dma_model_;

  USART_TypeDef* instance_;
  DMA_Channel_TypeDef* dma_rx_channel_;
  DMA_Channel_TypeDef* dma_tx_channel_;

  static CH32UART* map_[CH32_UART_NUMBER];

 private:
  /**
   * @brief Program and start the CH32 circular UART RX DMA channel.
   * @param data DMA-writable receive buffer.
   * @param size Receive buffer capacity in bytes.
   */
  void StartCircularDmaRx(uint8_t* data, size_t size)
  {
    dma_rx_channel_->MADDR = reinterpret_cast<uint32_t>(data);
    dma_rx_channel_->CNTR = size;
    DMA_Cmd(dma_rx_channel_, ENABLE);
  }

  /**
   * @brief Return the CH32 RX DMA remaining transfer count.
   */
  [[nodiscard]] size_t GetCircularDmaRxRemaining() const { return dma_rx_channel_->CNTR; }

  /**
   * @brief Prepare CH32 circular DMA RX storage for CPU access.
   * @param data DMA receive buffer start address.
   * @param size Receive buffer capacity in bytes.
   * @note CH32 requires no cache maintenance for this buffer.
   */
  void PrepareCircularDmaRxForCpu(uint8_t*, size_t) {}

  /**
   * @brief Program and start one active UART TX DMA payload.
   * @param data DMA-readable payload buffer.
   * @param size Payload size in bytes.
   * @param block Active double-buffer block index; unused by CH32 DMA.
   * @return True after the DMA channel is enabled.
   */
  bool StartDmaTx(uint8_t* data, size_t size, int block);
};

}  // namespace LibXR
