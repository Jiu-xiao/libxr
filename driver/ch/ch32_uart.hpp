#pragma once

#include "libxr.hpp"
#include DEF2STR(LIBXR_CH32_CONFIG_FILE)

#include "ch32_uart_def.hpp"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "uart.hpp"
#include "uart/uart_circular_dma_rx_model.hpp"
#include "uart/uart_dma_model.hpp"
#include "uart/uart_execution_policy.hpp"

namespace LibXR
{

/**
 * @brief CH32 UART driver implementation.
 *
 * This backend supports the current CH32V20x/V30x BSPs (V203/V307). The IRQ handlers
 * inspect and acknowledge their peripheral/DMA status before publishing coalesced TX
 * facts into the same serialized service used by Write() and SetConfig(). Owner
 * admission neither masks this instance's IRQ sources nor disables global interrupts.
 * CONFIG/ERROR may disable TC/HT/TE/IDLE only while actually stopping the data path.
 * Normal TX DMA completion advances the buffered TX model immediately. Destructive
 * CONFIG waits asynchronously for the USART transmission-complete flag before resetting
 * the peripheral, using the USART TC IRQ as its service carrier.
 * The BSP must keep the related UART, TX-DMA, and RX-DMA IRQs on one owner core at the
 * same preemption priority.
 */
class CH32UART : public UART
{
  friend class UartCircularDmaRxModel;
  friend class UartDmaModel<CH32UART, UartDirectPolicy>;

 public:
  /**
   * @brief 构造 UART 对象 / Construct UART object
   */
  CH32UART(ch32_uart_id_t id, RawData dma_rx, RawData dma_tx, GPIO_TypeDef* tx_gpio_port,
           uint16_t tx_gpio_pin, GPIO_TypeDef* rx_gpio_port, uint16_t rx_gpio_pin,
           uint32_t pin_remap = 0, uint32_t tx_queue_size = 5,
           UART::Configuration config = {115200, UART::Parity::NO_PARITY, 8, 1});

  /**
   * @return `BUSY` while an earlier configuration request is outstanding.
   * @warning Do not call from this UART's callbacks or from an ISR that can preempt its
   * UART/TX-DMA/RX-DMA IRQ domain.
   */
  ErrorCode SetConfig(UART::Configuration config);

  static ErrorCode WriteFun(WritePort& port, bool in_isr);
  static ErrorCode ReadFun(ReadPort& port, bool in_isr);

  void TxDmaIRQHandler();
  void RxDmaIRQHandler();
  void UartIRQHandler();

  ch32_uart_id_t id_;
  uint16_t uart_mode_;

  ReadPort _read_port;
  WritePort _write_port;

  UartDirectPolicy execution_policy_;
  UartCircularDmaRxModel rx_dma_model_;
  UartDmaModel<CH32UART, UartDirectPolicy> dma_model_;

  USART_TypeDef* instance_;
  DMA_Channel_TypeDef* dma_rx_channel_;
  DMA_Channel_TypeDef* dma_tx_channel_;

  static CH32UART* map_[CH32_UART_NUMBER];

 private:
  [[nodiscard]] ErrorCode ValidateConfig(UART::Configuration config) const;
  UartDmaControlResult AdvanceConfig(UART::Configuration config, bool active_tx,
                                     bool in_isr);
  UartDmaControlProgress CompleteConfig(bool in_isr);

  static bool InIsr();

  void HandleNormalIrq();

  uint32_t ScanNormalIrqStatus(bool in_isr, bool& pushed_any);

  UartDmaControlResult AdvanceRecovery(bool active_tx, bool in_isr);
  UartDmaControlProgress CompleteRecovery(bool in_isr);

  void SetDataPathInterrupts(bool enabled);

  UartOldTxTerminal StopDataPath(bool active_tx, bool in_isr);

  void StartDataPath();

  void ApplyConfigPayload(UART::Configuration config, bool in_isr);

  bool config_waiting_for_tx_idle_ = false;
  UartOldTxTerminal config_tx_terminal_ = UartOldTxTerminal::NONE;

  /**
   * @brief 配置并启动 CH32 UART 循环 RX DMA 通道 / Configure and start the CH32 UART
   * circular RX DMA channel
   * @param data DMA 可写的接收缓冲区 / DMA-writable receive buffer
   * @param size 接收缓冲区字节数 / Receive buffer capacity in bytes
   */
  void StartCircularDmaRx(uint8_t* data, size_t size);

  /**
   * @brief 获取 CH32 RX DMA 剩余传输计数 / Get the CH32 RX DMA remaining count
   * @return DMA 尚未写入的字节数 / Number of bytes not yet written by DMA
   */
  [[nodiscard]] size_t GetCircularDmaRxRemaining() const { return dma_rx_channel_->CNTR; }

  /**
   * @brief 为 CPU 访问准备 CH32 循环 DMA RX 存储区 / Prepare CH32 circular DMA RX storage
   * for CPU access
   * @param data DMA 接收缓冲区起始地址 / DMA receive buffer start address
   * @param size 接收缓冲区字节数 / Receive buffer capacity in bytes
   * @note 该 CH32 缓冲区不需要缓存维护 / This CH32 buffer requires no cache maintenance
   */
  void PrepareCircularDmaRxForCpu(uint8_t*, size_t) {}

  /**
   * @brief 配置并启动一个 active UART TX DMA 载荷 / Configure and start one active UART
   * TX DMA payload
   * @param data DMA 可读的载荷缓冲区 / DMA-readable payload buffer
   * @param size 载荷字节数 / Payload size in bytes
   * @param block active 双缓冲块索引，CH32 DMA 不使用 / Active double-buffer block index,
   * unused by CH32 DMA
   * @return `STARTED` after enabling DMA
   */
  UartDmaTxStartResult StartDmaTx(uint8_t* data, size_t size, int block, bool in_isr);
};

}  // namespace LibXR
