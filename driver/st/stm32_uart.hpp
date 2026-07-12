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
 * @warning 使用循环 RX DMA 时，UART 全局中断与对应 RX DMA 中断必须配置为相同的
 * NVIC 抢占优先级。HAL 的 UART IDLE、DMA HT 和 DMA TC 路径都可能进入同一 RX event
 * callback；本驱动依赖相同优先级保证这些入口不重入。
 * When circular RX DMA is enabled, the UART global IRQ and its RX DMA IRQ must use the
 * same NVIC preemption priority and, on multicore devices, the same target core. UART
 * IDLE, DMA HT, and DMA TC paths may all enter the same HAL RX event callback; they must
 * remain one logical SPSC producer. Runtime configuration may execute on another core
 * and is coordinated separately by `UartRxConfigGate`.
 */
class STM32UART : public UART
{
  friend class UartCircularDmaRxModel;
  friend class UartDmaTxModel<STM32UART>;

 public:
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

  ReadPort _read_port;
  WritePort _write_port;

  LatestSnapshot<UART::Configuration> requested_config_;
  UartRxConfigGate rx_config_gate_;
  UartCircularDmaRxModel rx_dma_model_;
  UartDmaTxModel<STM32UART> tx_dma_model_;

  UART_HandleTypeDef* uart_handle_;

  stm32_uart_id_t id_ = STM32_UART_ID_ERROR;

  static STM32UART* map[STM32_UART_NUMBER];  // NOLINT

 private:
  void OnConfigRequested() { rx_config_gate_.RequestConfig(); }
  bool ApplyPendingConfig(bool in_isr);
  bool OnConfigApplied(bool)
  {
    rx_config_gate_.LeaveConfig();
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

}  // namespace LibXR

#endif
