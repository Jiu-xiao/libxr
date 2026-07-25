#pragma once

#include <cstddef>
#include <cstdint>

#include "main.h"

#ifdef HAL_UART_MODULE_ENABLED

#if defined(DMA_IT_SUSP) && defined(DMA_FLAG_SUSP)
#error \
    "LibXR STM32UART does not support suspend/linked-list STM32 DMA; no STM32 linked-list RX backend is currently provided"
#endif

#ifdef UART
#undef UART
#endif

#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "model/uart_circular_dma_rx_model.hpp"
#include "model/uart_dma_model.hpp"
#include "model/uart_execution_policy.hpp"
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
 * which publish TX completion/error facts to `UartDmaModel` and push RX data through
 * `UartCircularDmaRxModel`. CONFIG and runtime recovery stop both DMA directions through
 * DMA abort completion callbacks, which publish `STOP_DONE` into the same serialized
 * service. Stream-DMA abort admission briefly masks that stream's NVIC vector to
 * serialize the HAL `BUSY` to `ABORT` transition without modifying an active Stream
 * control register; it never polls for completion or clears a pending terminal flag.
 * Stream DMA also requires the selected NVIC vector and DMA terminal interrupt to be
 * enabled before an asynchronous abort starts. LibXR-initiated control stops check both
 * conditions at their abort boundary. A vendor error abort that races the initial RX
 * arm occurs inside the HAL before LibXR regains control, so the BSP must establish the
 * same conditions before construction. The BSP must additionally wire that vector to
 * `HAL_DMA_IRQHandler()`; handler wiring cannot be checked by this backend. The UART
 * vector must likewise remain enabled and dispatch `HAL_UART_IRQHandler()`, because
 * both normal TX completion and any stopped active TX awaiting UART TC use it as their
 * final non-blocking carrier.
 *
 * This backend supports traditional STM32 Stream, Channel, and BDMA circular RX.
 * Suspend/linked-list families such as H5/U5/U3/N6/H7RS GPDMA are rejected because
 * their receive state needs another model.
 * On D-cache targets, enabled RX storage must start and end on cache-line boundaries
 * so invalidating DMA-written bytes cannot discard unrelated dirty data.
 * A documented HAL_ERROR caused by a pending RX line error is reported as a pending
 * control step; the UART handler has already published the error callback or arranged
 * its DMA-abort callback as the dedicated retry carrier. This transient is accepted
 * during the initial constructor arm as well as CONFIG/recovery. Other RX-arm failures
 * remain fail-fast so a stopped receive path cannot be mistaken for success.
 *
 * After construction, this backend exclusively owns the UART/DMA data path and their
 * HAL handles. Application code must not directly call HAL UART transmit, DMA start,
 * DMA stop, or UART/DMA abort APIs on those handles. In particular, such calls could
 * disable TCIE without publishing `HAL_UART_TxCpltCallback()` and would invalidate the
 * old-generation retirement proof used by CONFIG and recovery.
 *
 * The BSP must assign this UART's UART, TX-DMA, and RX-DMA IRQs the same NVIC
 * preemption priority so their vendor HAL handlers cannot nest each other. Their
 * subpriorities may differ. LibXR starts serialization only at the HAL callback
 * boundary and therefore cannot repair HAL-handle races caused by nested related IRQs.
 *
 * `SetConfig()` must not be called from this UART's HAL callbacks or from an ISR that can
 * preempt its UART, TX-DMA, or RX-DMA IRQ. The callback-only HAL boundary cannot make
 * such a caller safe after the vendor handler has already touched hardware state.
 */
class STM32UART : public UART
{
  friend class UartCircularDmaRxModel;
  friend class UartDmaModel<STM32UART, UartDirectPolicy>;
  friend void ::HAL_UARTEx_RxEventCallback(UART_HandleTypeDef* huart, uint16_t size);
  friend void ::HAL_UART_TxCpltCallback(UART_HandleTypeDef* huart);
  friend void ::HAL_UART_ErrorCallback(UART_HandleTypeDef* huart);

 public:
  static ErrorCode WriteFun(WritePort& port, bool in_isr);
  static ErrorCode ReadFun(ReadPort& port, bool in_isr);

  STM32UART(UART_HandleTypeDef* uart_handle, RawData dma_buff_rx, RawData dma_buff_tx,
            uint32_t tx_queue_size = 5);

  /**
   * @return `BUSY` while an earlier configuration request is outstanding.
   * @warning Do not call from this UART's HAL callbacks or from an ISR that can preempt
   * its UART/TX-DMA/RX-DMA IRQ domain.
   */
  ErrorCode SetConfig(UART::Configuration config);

  /**
   * @brief Re-arm circular RX DMA outside the runtime CONFIG state machine.
   * @warning Compatibility hook. The caller must keep UART/RX-DMA callbacks,
   * CONFIG/recovery, and the RX producer quiescent for the entire call. Use SetConfig()
   * for ordinary runtime reconfiguration.
   */
  void SetRxDMA();
  void OnRxDataAvailable(bool in_isr);

  ReadPort _read_port;
  WritePort _write_port;

  UartDirectPolicy execution_policy_;
  UartCircularDmaRxModel rx_dma_model_;
  UartDmaModel<STM32UART, UartDirectPolicy> dma_model_;

  UART_HandleTypeDef* uart_handle_;
  stm32_uart_id_t id_ = STM32_UART_ID_ERROR;

  static STM32UART* map[STM32_UART_NUMBER];  // NOLINT

 private:
  enum class RxArmResult : uint8_t
  {
    STARTED,
    PENDING_LINE_ERROR,
    FAILED,
  };

  static bool InIsr();
  static bool DmaTransferSizeSupported(size_t size);
  static bool IsPendingRxLineError(uint32_t error_code);

  [[nodiscard]] ErrorCode ValidateConfig(UART::Configuration config) const;
  UartDmaControlResult AdvanceConfig(UART::Configuration config, bool active_tx,
                                     bool in_isr);
  UartDmaControlProgress CompleteConfig(bool in_isr);
  UartDmaControlResult AdvanceRecovery(bool active_tx, bool in_isr);
  UartDmaControlProgress CompleteRecovery(bool in_isr);
  UartDmaControlResult StopDataPath(bool active_tx, bool wait_for_uart_tc, bool in_isr);
  bool ApplyConfigPayload(UART::Configuration config, bool in_isr);
  void FinishControl();
  UartDmaControlProgress SetRxDMA(bool in_isr);
  void OnTxComplete(bool in_isr);

  [[nodiscard]] bool AllDmaStopsComplete() const;

  void CloseTxTerminalSource();
  void LaunchDmaStop(DMA_HandleTypeDef* dma_handle, bool in_isr, bool classify_tx);
  static void DmaAbortCallback(DMA_HandleTypeDef* dma_handle);

  RxArmResult StartCircularDmaRx(uint8_t* data, size_t size)
  {
    const bool in_isr = InIsr();
    REQUIRE_FROM_CALLBACK(DmaTransferSizeSupported(size), in_isr);
    STM32_CleanDCacheByAddr(data, size);
    STM32_InvalidateDCacheByAddr(data, size);
    uart_handle_->hdmarx->Init.Mode = DMA_CIRCULAR;
    REQUIRE_FROM_CALLBACK(HAL_DMA_Init(uart_handle_->hdmarx) == HAL_OK, in_isr);
    const HAL_StatusTypeDef status =
        HAL_UARTEx_ReceiveToIdle_DMA(uart_handle_, data, static_cast<uint16_t>(size));
    if (status == HAL_OK)
    {
      rx_arm_result_ = RxArmResult::STARTED;
      return rx_arm_result_;
    }

    if ((status == HAL_ERROR) && IsPendingRxLineError(uart_handle_->ErrorCode))
    {
      // HAL may return HAL_ERROR after a pending UART line error has already
      // aborted this RX arm. The HAL error/abort callback is the next retry carrier.
      rx_arm_result_ = RxArmResult::PENDING_LINE_ERROR;
      return rx_arm_result_;
    }

    rx_arm_result_ = RxArmResult::FAILED;
    REQUIRE_FROM_CALLBACK(false, in_isr);
    return rx_arm_result_;
  }

  [[nodiscard]] size_t GetCircularDmaRxRemaining() const
  {
    return __HAL_DMA_GET_COUNTER(uart_handle_->hdmarx);
  }

  void PrepareCircularDmaRxForCpu(uint8_t* data, size_t size)
  {
    STM32_InvalidateDCacheByAddr(data, size);
  }

  UartDmaTxStartResult StartDmaTx(uint8_t* data, size_t size, int block, bool in_isr);

  bool stop_active_ = false;
  bool tx_evidence_captured_ = false;
  bool tx_payload_complete_ = false;
  bool tx_dma_error_ = false;
  bool waiting_for_uart_tc_ = false;
  bool tx_replay_required_ = false;
  RxArmResult rx_arm_result_ = RxArmResult::STARTED;
};

}  // namespace LibXR

#endif
