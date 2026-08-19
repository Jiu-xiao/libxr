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
 * @brief CH32 UART 驱动实现 / CH32 UART driver implementation
 *
 * 当前后端支持 CH32V20x/V30x BSP（V203/V307）。IRQ handler 先读取并确认外设或 DMA
 * 状态，再把可合并的 TX 事实发布到 `Write()` 与 `SetConfig()` 共用的 serialized
 * service。owner admission 不屏蔽本实例的 IRQ 源，也不关闭全局中断；只有在实际停止
 * data path 时，CONFIG/ERROR 才可关闭 TC/HT/TE/IDLE。 / This backend supports the
 * current CH32V20x/V30x BSPs (V203/V307). IRQ handlers inspect and acknowledge their
 * peripheral or DMA status before publishing coalesced TX facts into the serialized
 * service shared by `Write()` and `SetConfig()`. Owner admission masks neither this
 * instance's IRQ sources nor global interrupts; CONFIG/ERROR may disable TC/HT/TE/IDLE
 * only while actually stopping the data path.
 *
 * 普通 TX DMA 完成会立即推进双缓冲 TX 模型。破坏性 CONFIG 在复位外设前异步等待
 * USART transmission-complete，并使用 USART TC IRQ 作为 service carrier。BSP 必须把
 * 相关 UART、TX-DMA 和 RX-DMA IRQ 保持在同一个 owner core，并使用相同的抢占优先级。
 * / Normal TX DMA completion advances the buffered TX model immediately. Destructive
 * CONFIG waits asynchronously for the USART transmission-complete flag before resetting
 * the peripheral, using the USART TC IRQ as its service carrier. The BSP must keep the
 * related UART, TX-DMA, and RX-DMA IRQs on one owner core at the same preemption
 * priority.
 *
 * RX DMA 只向模型报告环内位置；两次成功采样之间新增字节数必须严格小于 RX ring 容量。
 * 对正常 HT/TC 唤醒，BSP 应使最坏 IRQ 触发到有效采样延迟小于半环填充时间，并为中断
 * 屏蔽和高优先级工作留出余量。 / RX DMA reports only a position within the ring, so
 * fewer than one ring capacity of bytes may arrive between successful samples. For normal
 * HT/TC wakeups, keep worst-case IRQ-to-sample latency below half a ring-fill time, with
 * margin for interrupt masking and higher-priority work.
 */
class CH32UART : public UART
{
  friend class UartCircularDmaRxModel;
  friend class UartDmaModel<CH32UART, UartDirectPolicy>;

 public:
  /**
   * @brief 构造 UART 对象 / Construct UART object
   * @param id CH32 UART 外设编号 / CH32 UART peripheral identifier
   * @param dma_rx 循环 RX DMA 缓冲区 / Circular RX DMA buffer
   * @param dma_tx TX DMA 双缓冲存储区 / TX DMA double-buffer storage
   * @param tx_gpio_port TX GPIO 端口 / TX GPIO port
   * @param tx_gpio_pin TX GPIO 引脚 / TX GPIO pin
   * @param rx_gpio_port RX GPIO 端口 / RX GPIO port
   * @param rx_gpio_pin RX GPIO 引脚 / RX GPIO pin
   * @param pin_remap AFIO 引脚重映射值，0 表示不重映射 / AFIO pin-remap value; zero
   * disables remapping
   * @param tx_queue_size 待发送记录队列深度 / Pending TX record queue depth
   * @param config 初始 UART 帧格式和波特率 / Initial UART framing and baud rate
   * @warning The id must name a real hardware UART, and each id may be bound to only one
   *          process-lifetime CH32UART instance. Invalid or duplicate construction fails
   *          a strong runtime requirement.
   */
  CH32UART(ch32_uart_id_t id, RawData dma_rx, RawData dma_tx, GPIO_TypeDef* tx_gpio_port,
           uint16_t tx_gpio_pin, GPIO_TypeDef* rx_gpio_port, uint16_t rx_gpio_pin,
           uint32_t pin_remap = 0, uint32_t tx_queue_size = 5,
           UART::Configuration config = {115200, UART::Parity::NO_PARITY, 8, 1});

  /**
   * @brief 提交一次串行化 UART 配置 / Submit one serialized UART configuration
   * @param config 新的帧格式和波特率 / New framing and baud rate
   * @return 前一个配置仍未完成时返回 `BUSY` / `BUSY` while an earlier configuration
   * request is outstanding
   * @warning 不得从本 UART 的 callback，或能抢占其 UART/TX-DMA/RX-DMA IRQ 域的 ISR
   * 调用 / Do not call from this UART's callbacks or from an ISR that can preempt its
   * UART/TX-DMA/RX-DMA IRQ domain
   */
  ErrorCode SetConfig(UART::Configuration config);

  /**
   * @brief WritePort 提交入口 / WritePort submission entry
   * @param port 发起提交的写端口 / Write port issuing the submission
   * @param in_isr 当前调用是否位于 ISR / Whether the call is in an ISR
   */
  static void WriteFun(WritePort& port, bool in_isr);

  /** @brief 处理本实例的 TX DMA IRQ / Handle this instance's TX DMA IRQ. */
  void TxDmaIRQHandler();

  /** @brief 处理本实例的 RX DMA IRQ / Handle this instance's RX DMA IRQ. */
  void RxDmaIRQHandler();

  /** @brief 处理本实例的 USART IRQ / Handle this instance's USART IRQ. */
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

  uint32_t ScanNormalIrqStatus(bool in_isr, ReadPort::ReadQueue& queue);

  UartDmaControlResult AdvanceRecovery(bool active_tx, bool in_isr);
  UartDmaControlProgress CompleteRecovery(bool in_isr);

  void SetDataPathInterrupts(bool enabled);

  UartOldTxTerminal StopDataPath(bool active_tx, bool in_isr);

  void StartDataPath(bool in_isr);

  void ApplyConfigPayload(UART::Configuration config, bool in_isr);

  bool config_waiting_for_tx_idle_ = false;
  UartOldTxTerminal config_tx_terminal_ = UartOldTxTerminal::NONE;

  /**
   * @brief 配置并启动 CH32 UART 循环 RX DMA 通道 / Configure and start the CH32 UART
   * circular RX DMA channel
   * @param data DMA 可写的接收缓冲区 / DMA-writable receive buffer
   * @param size 接收缓冲区字节数 / Receive buffer capacity in bytes
   * @param in_isr 当前启动是否位于 ISR / Whether the start runs in an ISR
   */
  void StartCircularDmaRx(uint8_t* data, size_t size, bool in_isr);

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
  void PrepareCircularDmaRxForCpu(uint8_t* data, size_t size)
  {
    static_cast<void>(data);
    static_cast<void>(size);
  }

  /**
   * @brief 配置并启动一个 active UART TX DMA 载荷 / Configure and start one active UART
   * TX DMA payload
   * @param data DMA 可读的载荷缓冲区 / DMA-readable payload buffer
   * @param size 载荷字节数 / Payload size in bytes
   * @param block active 双缓冲块索引，CH32 DMA 不使用 / Active double-buffer block index,
   * unused by CH32 DMA
   * @param in_isr 是否从 ISR 上下文启动 / Whether started from ISR context
   * @return `STARTED` after enabling DMA
   */
  UartDmaTxStartResult StartDmaTx(uint8_t* data, size_t size, int block, bool in_isr);
};

}  // namespace LibXR
