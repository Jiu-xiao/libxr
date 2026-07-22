#pragma once

#include <cstddef>
#include <cstdint>

#include "main.h"

#ifdef HAL_UART_MODULE_ENABLED

#if defined(DMA_IT_SUSP) && defined(DMA_FLAG_SUSP)
#error "LibXR STM32UART does not support H5/U5 GPDMA; use the linked-list RX model"
#endif

#ifdef UART
#undef UART
#endif

#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "model/uart_circular_dma_rx_model.hpp"
#include "model/uart_dma_tx_model.hpp"
#include "model/uart_execution_policy.hpp"
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
 * @brief STM32 UART backend using the HAL callback boundary.
 *
 * HAL IRQ handlers own HAL flags and handle state. They call the callbacks below,
 * which publish TX completion/error facts to `UartDmaTxModel` and push RX data through
 * `UartCircularDmaRxModel`. CONFIG and runtime recovery stop both DMA directions through
 * DMA abort completion callbacks, which publish `STOP_DONE` into the same serialized
 * service. Stream-DMA abort admission briefly masks that stream's NVIC vector to
 * serialize the HAL `BUSY` to `ABORT` transition without modifying an active Stream
 * control register; it never polls for completion or clears a pending terminal flag.
 *
 * This backend supports traditional STM32 Stream, Channel, and BDMA circular RX. H5/U5
 * GPDMA is rejected because its linked-list receive state needs another model.
 */
class STM32UART : public UART
{
  friend class UartCircularDmaRxModel;
  friend class UartDmaTxModel<STM32UART>;
  friend void ::HAL_UARTEx_RxEventCallback(UART_HandleTypeDef* huart, uint16_t size);
  friend void ::HAL_UART_TxCpltCallback(UART_HandleTypeDef* huart);
  friend void ::HAL_UART_ErrorCallback(UART_HandleTypeDef* huart);

 public:
  static ErrorCode WriteFun(WritePort& port, bool in_isr);
  static ErrorCode ReadFun(ReadPort& port, bool in_isr);

  STM32UART(UART_HandleTypeDef* uart_handle, RawData dma_buff_rx, RawData dma_buff_tx,
            uint32_t tx_queue_size = 5);

  /** @return `BUSY` while an earlier configuration request is outstanding. */
  ErrorCode SetConfig(UART::Configuration config);

  void SetRxDMA();
  void OnRxDataAvailable(bool in_isr);

  ReadPort _read_port;
  WritePort _write_port;

  UART::Configuration requested_config_;
  UartDirectPolicy execution_policy_;
  UartCircularDmaRxModel rx_dma_model_;
  UartDmaTxModel<STM32UART> tx_dma_model_;
  UartRxConfigGate rx_config_gate_;

  UART_HandleTypeDef* uart_handle_;
  stm32_uart_id_t id_ = STM32_UART_ID_ERROR;

  static STM32UART* map[STM32_UART_NUMBER];  // NOLINT

 private:
  static bool InIsr();

  UartDmaControlResult ApplyPendingConfig(bool in_isr);
  void ReleaseConfigAdmission(bool) { rx_config_gate_.LeaveConfig(); }
  UartDmaControlResult RecoverDataPath(bool in_isr);
  UartDmaControlResult StopDataPath(bool in_isr);
  bool ApplyConfigPayload(bool in_isr);
  void FinishControl();

  [[nodiscard]] bool AllDmaStopsComplete() const;

  void LaunchDmaStop(DMA_HandleTypeDef* dma_handle, bool in_isr);
  static void DmaAbortCallback(DMA_HandleTypeDef* dma_handle);

  void StartCircularDmaRx(uint8_t* data, size_t size)
  {
    uart_handle_->hdmarx->Init.Mode = DMA_CIRCULAR;
    const bool in_isr = InIsr();
    REQUIRE_FROM_CALLBACK(HAL_DMA_Init(uart_handle_->hdmarx) == HAL_OK, in_isr);
    REQUIRE_FROM_CALLBACK(
        HAL_UARTEx_ReceiveToIdle_DMA(uart_handle_, data, size) == HAL_OK, in_isr);
  }

  [[nodiscard]] size_t GetCircularDmaRxRemaining() const
  {
    return __HAL_DMA_GET_COUNTER(uart_handle_->hdmarx);
  }

  void PrepareCircularDmaRxForCpu(uint8_t* data, size_t size)
  {
    STM32_InvalidateDCacheByAddr(data, size);
  }

  UartDmaTxStartResult StartDmaTx(uint8_t* data, size_t size, int block);

  bool stop_active_ = false;
};

}  // namespace LibXR

#endif
