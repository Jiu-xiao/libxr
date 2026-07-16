#pragma once

#include "libxr.hpp"
#include DEF2STR(LIBXR_CH32_CONFIG_FILE)

#include "ch32_uart_def.hpp"
#include "latest_snapshot.hpp"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "model/uart_circular_dma_rx_model.hpp"
#include "model/uart_dma_tx_model.hpp"
#include "model/uart_hardware_gate.hpp"
#include "uart.hpp"

namespace LibXR
{

/**
 * @brief CH32 UART driver implementation.
 *
 * This backend supports the current single-core CH32V20x/V30x BSPs (V203/V307). UART
 * IDLE and RX/TX DMA IRQ entries for one instance must use the same preemption priority.
 * Every entry claims `UartHardwareGate` before inspecting hardware status. A losing
 * entry masks the complete normal IRQ domain and publishes a deferred rescan; it must
 * not cache a terminal result outside the gate.
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
  void UartIRQHandler();

  ch32_uart_id_t id_;
  uint16_t uart_mode_;

  ReadPort _read_port;
  WritePort _write_port;

  LatestSnapshot<UART::Configuration> requested_config_;
  UartHardwareGate hardware_gate_;
  UartCircularDmaRxModel rx_dma_model_;
  UartDmaTxModel<CH32UART> tx_dma_model_;

  USART_TypeDef* instance_;
  DMA_Channel_TypeDef* dma_rx_channel_;
  DMA_Channel_TypeDef* dma_tx_channel_;

  static CH32UART* map_[CH32_UART_NUMBER];

 private:
  void OnConfigRequested() { hardware_gate_.RequestConfig(); }

  [[nodiscard]] bool ConfigRequested() const { return hardware_gate_.ConfigRequested(); }

  bool ApplyPendingConfig(bool in_isr);

  bool OnConfigApplied(bool in_isr);

  static bool InIsr();

  void HandleNormalIrq();

  void DeferNormalIrq(bool in_isr);

  void ScanNormalIrqStatus(bool in_isr, UartHardwareGate::OwnerContext& hardware_context);

  void MaskNormalIrqs();

  void RestoreNormalIrqs();

  void DispatchHardwareActions(UartHardwareGate::PendingAction actions, bool in_isr);

  void ApplyConfigPayload(bool in_isr);

  void OnRxDataAvailable(bool in_isr);

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
   * @param hardware_context Optional stack-local IRQ owner context for nested TX start
   * @return `STARTED` after enabling DMA, or `RETRY` when another hardware action wins
   */
  UartDmaTxStartResult StartDmaTx(uint8_t* data, size_t size, int block,
                                  UartHardwareGate::OwnerContext* hardware_context);
};

}  // namespace LibXR
