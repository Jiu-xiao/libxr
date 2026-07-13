#pragma once

#include "main.h"

#ifdef HAL_UART_MODULE_ENABLED

#ifdef UART
#undef UART
#endif

#include "latest_snapshot.hpp"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "model/uart_circular_dma_rx_model.hpp"
#include "model/uart_dma_tx_model.hpp"
#include "model/uart_irq_config_gate.hpp"
#include "model/uart_rx_config_gate.hpp"
#include "stm32_dcache.hpp"
#include "uart.hpp"

typedef enum : uint8_t
{
#ifdef USART1
  STM32_USART1,
#endif
#ifdef USART2
  STM32_USART2,
#endif
#ifdef USART3
  STM32_USART3,
#endif
#ifdef USART4
  STM32_USART4,
#endif
#ifdef USART5
  STM32_USART5,
#endif
#ifdef USART6
  STM32_USART6,
#endif
#ifdef USART7
  STM32_USART7,
#endif
#ifdef USART8
  STM32_USART8,
#endif
#ifdef USART9
  STM32_USART9,
#endif
#ifdef USART10
  STM32_USART10,
#endif
#ifdef USART11
  STM32_USART11,
#endif
#ifdef USART12
  STM32_USART12,
#endif
#ifdef USART13
  STM32_USART13,
#endif
#ifdef UART1
  STM32_UART1,
#endif
#ifdef UART2
  STM32_UART2,
#endif
#ifdef UART3
  STM32_UART3,
#endif
#ifdef UART4
  STM32_UART4,
#endif
#ifdef UART5
  STM32_UART5,
#endif
#ifdef UART6
  STM32_UART6,
#endif
#ifdef UART7
  STM32_UART7,
#endif
#ifdef UART8
  STM32_UART8,
#endif
#ifdef UART9
  STM32_UART9,
#endif
#ifdef UART10
  STM32_UART10,
#endif
#ifdef UART11
  STM32_UART11,
#endif
#ifdef UART12
  STM32_UART12,
#endif
#ifdef UART13
  STM32_UART13,
#endif
#ifdef LPUART1
  STM32_LPUART1,
#endif
#ifdef LPUART2
  STM32_LPUART2,
#endif
#ifdef LPUART3
  STM32_LPUART3,
#endif
  STM32_UART_NUMBER,
  STM32_UART_ID_ERROR
} stm32_uart_id_t;

stm32_uart_id_t stm32_uart_get_id(USART_TypeDef* addr);

namespace LibXR
{
/**
 * @brief STM32 UART 驱动实现 / STM32 UART driver implementation
 * @warning UART 全局中断与关联的 RX/TX DMA 中断必须分别通过
 * `STM32_UART_IRQHandler()` 和 `STM32_UART_DMA_IRQHandler()` 分发，不得直接调用 HAL
 * handler。指向同一核心的相关 IRQ 必须使用相同抢占优先级，避免未取得 gate 的
 * level-triggered IRQ 反复抢占当前 owner；指向不同核心的 IRQ 会在 HAL 读取或清除硬件
 * 状态前由 `UartIrqConfigGate` 串行化。
 * The UART global IRQ and associated RX/TX DMA IRQs must be dispatched through
 * `STM32_UART_IRQHandler()` and `STM32_UART_DMA_IRQHandler()` rather than calling HAL
 * handlers directly. IRQ sources targeting the same core must use the same preemption
 * priority so a losing level-triggered source cannot repeatedly preempt the current
 * owner. Sources on different cores are serialized by `UartIrqConfigGate` before HAL
 * reads or clears hardware status.
 */
class STM32UART : public UART
{
  friend class UartCircularDmaRxModel;
  friend class UartDmaTxModel<STM32UART>;
  friend void STM32_UART_DMA_IRQHandler(DMA_HandleTypeDef* dma_handle);
  friend void STM32_UART_IRQHandler(UART_HandleTypeDef* uart_handle);

 public:
  enum class ConfigPhase : uint32_t
  {
    IDLE = 0U,
    STARTING = 1U,
    ABORTING = 2U,
    ABORTED = 3U,
  };

  static ErrorCode WriteFun(WritePort& port, bool in_isr);

  static ErrorCode ReadFun(ReadPort& port, bool in_isr);

  /**
   * @brief 构造 UART 对象 / Construct UART object
   */
  STM32UART(UART_HandleTypeDef* uart_handle, RawData dma_buff_rx, RawData dma_buff_tx,
            uint32_t tx_queue_size = 5);

  ErrorCode SetConfig(UART::Configuration config);

  void SetRxDMA();
  void OnRxDataAvailable(bool in_isr);
  void OnAbortComplete(bool in_isr);
  void DmaIRQHandler(DMA_HandleTypeDef* dma_handle);
  void UartIRQHandler();

  ReadPort _read_port;
  WritePort _write_port;

  LatestSnapshot<UART::Configuration> requested_config_;
  UartRxConfigGate rx_config_gate_;
  UartIrqConfigGate irq_config_gate_;
  UartCircularDmaRxModel rx_dma_model_;
  UartDmaTxModel<STM32UART> tx_dma_model_;
  std::atomic<ConfigPhase> config_phase_{ConfigPhase::IDLE};

  UART_HandleTypeDef* uart_handle_;

  stm32_uart_id_t id_ = STM32_UART_ID_ERROR;

  static STM32UART* map[STM32_UART_NUMBER];  // NOLINT

 private:
  void OnConfigRequested()
  {
    irq_config_gate_.RequestConfig();
    rx_config_gate_.RequestConfig();
  }
  [[nodiscard]] bool ConfigRequested() const
  {
    return irq_config_gate_.ConfigRequested() || rx_config_gate_.ConfigRequested();
  }
  bool ApplyPendingConfig(bool in_isr);
  bool OnConfigApplied(bool)
  {
    rx_config_gate_.LeaveConfig();
    config_phase_.store(ConfigPhase::IDLE, std::memory_order_release);
    irq_config_gate_.LeaveConfig();
    return true;
  }

  /**
   * @brief 通过 STM32 HAL 配置并启动 UART 循环 RX DMA / Configure and start circular
   * UART RX DMA through STM32 HAL
   * @param data DMA 可写的接收缓冲区 / DMA-writable receive buffer
   * @param size 接收缓冲区字节数 / Receive buffer capacity in bytes
   */
  void StartCircularDmaRx(uint8_t* data, size_t size)
  {
    uart_handle_->hdmarx->Init.Mode = DMA_CIRCULAR;
    HAL_DMA_Init(uart_handle_->hdmarx);
    HAL_UARTEx_ReceiveToIdle_DMA(uart_handle_, data, size);
  }

  /**
   * @brief 获取 STM32 RX DMA 剩余传输计数 / Get the STM32 RX DMA remaining count
   * @return DMA 尚未写入的字节数 / Number of bytes not yet written by DMA
   */
  [[nodiscard]] size_t GetCircularDmaRxRemaining() const
  {
    return __HAL_DMA_GET_COUNTER(uart_handle_->hdmarx);
  }

  /**
   * @brief 使循环 DMA RX 数据对 CPU 可见 / Make circular DMA RX data visible to the CPU
   * @param data DMA 接收缓冲区起始地址 / DMA receive buffer start address
   * @param size 接收缓冲区字节数 / Receive buffer capacity in bytes
   */
  void PrepareCircularDmaRxForCpu(uint8_t* data, size_t size)
  {
    STM32_InvalidateDCacheByAddr(data, size);
  }

  /**
   * @brief 通过 STM32 HAL 启动一个 active UART TX DMA 载荷 / Start one active UART TX
   * DMA payload through STM32 HAL
   * @param data DMA 可读的载荷缓冲区 / DMA-readable payload buffer
   * @param size 载荷字节数 / Payload size in bytes
   * @param block active 双缓冲块索引，STM32 HAL 不使用 / Active double-buffer block
   * index, unused by STM32 HAL
   * @return HAL 接受 DMA 传输时返回 true / True when HAL accepts the DMA transfer
   */
  bool StartDmaTx(uint8_t* data, size_t size, int block);
};

/**
 * @brief Dispatch one STM32 UART DMA IRQ through the libxr TX/config gate.
 *
 * BSP DMA IRQ handlers associated with a libxr UART must call this function instead of
 * calling `HAL_DMA_IRQHandler()` directly. The gate is entered before HAL reads or
 * clears DMA status.
 */
void STM32_UART_DMA_IRQHandler(DMA_HandleTypeDef* dma_handle);

/**
 * @brief Dispatch one STM32 UART global IRQ through the libxr TX/config gate.
 *
 * BSP UART IRQ handlers associated with a libxr UART must call this function instead of
 * calling `HAL_UART_IRQHandler()` directly.
 */
void STM32_UART_IRQHandler(UART_HandleTypeDef* uart_handle);

}  // namespace LibXR

#endif
