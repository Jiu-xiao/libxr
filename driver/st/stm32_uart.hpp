#pragma once

#include <cstddef>
#include <cstdint>

#include "main.h"

#ifdef HAL_UART_MODULE_ENABLED

#include "stm32_uart_gpdma.hpp"

#if defined(DMA_IT_SUSP) && defined(DMA_FLAG_SUSP) && !defined(LIBXR_STM32_UART_GPDMA)
#error \
    "LibXR STM32UART does not support suspend/linked-list STM32 DMA; no STM32 linked-list RX backend is currently provided"
#endif

#ifdef UART
#undef UART
#endif

#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "uart/uart_dma_model.hpp"
#include "uart/uart_execution_policy.hpp"
#if defined(LIBXR_STM32_UART_GPDMA)
#include "uart/uart_linked_list_dma_rx_model.hpp"
#else
#include "uart/uart_circular_dma_rx_model.hpp"
#endif
#include "stm32_dcache.hpp"
#include "uart.hpp"

/**
 * @brief 可由 STM32 UART 后端接管的外设编号 / Peripheral identifiers supported by the
 * STM32 UART backend
 */
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

/**
 * @brief 将 UART 寄存器实例映射为 STM32 UART 编号 / Map a UART register instance to
 * its STM32 UART identifier
 * @param addr UART 寄存器实例 / UART register instance
 * @return 匹配的编号；不支持时为 `STM32_UART_ID_ERROR` / Matching identifier or
 * `STM32_UART_ID_ERROR` when unsupported
 */
stm32_uart_id_t stm32_uart_get_id(USART_TypeDef* addr);

namespace LibXR
{

#if defined(LIBXR_STM32_UART_GPDMA)
/**
 * @brief 当前 STM32 系列使用的 linked-list RX DMA 模型 / Linked-list RX DMA model for
 * the current STM32 family
 */
using STM32RxDmaModel = UartLinkedListDmaRxModel<STM32GpdmaUartAdapter::RX_NODE_COUNT>;
#else
/**
 * @brief 当前 STM32 系列使用的循环 RX DMA 模型 / Circular RX DMA model for the current
 * STM32 family
 */
using STM32RxDmaModel = UartCircularDmaRxModel;
#endif

/**
 * @brief 使用 HAL callback 边界的 STM32 UART 后端 / STM32 UART backend using the HAL
 * callback boundary
 *
 * HAL IRQ handler 拥有 HAL flag 和 handle 状态；其 callback 将 TX 完成或错误事实发布给
 * `UartDmaModel`，并通过选定的循环或 linked-list RX 模型提交 RX 数据。CONFIG 和 runtime
 * recovery 通过 DMA abort 完成 callback 停止两个 DMA 方向，再向同一 serialized service
 * 发布 `STOP_DONE`。 / HAL IRQ handlers own HAL flags and handle state. Their callbacks
 * publish TX completion/error facts to `UartDmaModel` and push RX data through the
 * selected circular or linked-list RX model. CONFIG and runtime recovery stop both DMA
 * directions through DMA abort completion callbacks, which publish `STOP_DONE` into the
 * same serialized service.
 *
 * Stream-DMA abort admission 仅短暂屏蔽该 stream 的 NVIC vector，以串行化 HAL 从
 * `BUSY` 到 `ABORT` 的转换；它不修改 active Stream control register、不轮询完成，也不
 * 清除 pending terminal flag。异步 abort 开始前，选定 NVIC vector 和 DMA terminal
 * interrupt 必须已使能；LibXR 发起的 control stop 会在 abort 边界检查两者。若 vendor
 * error abort 与首次 RX arm 竞争，该过程会在 LibXR 恢复控制前发生于 HAL 内，因此 BSP
 * 必须在构造前满足相同条件。BSP 还必须把该 vector 接到 `HAL_DMA_IRQHandler()`，后端
 * 无法检查 handler 接线。UART vector 同样必须保持使能并分发
 * `HAL_UART_IRQHandler()`；普通 TX 完成及等待 UART TC 的已停止 active TX 都以它作为
 * 最终的非阻塞 carrier。 / Stream-DMA abort admission briefly masks that stream's NVIC
 * vector to serialize HAL's `BUSY`-to-`ABORT` transition without modifying an active
 * Stream control register; it never polls for completion or clears a pending terminal
 * flag. The selected NVIC vector and DMA terminal interrupt must be enabled before an
 * asynchronous abort starts, and LibXR-initiated control stops check both at the abort
 * boundary. A vendor error abort racing the initial RX arm occurs inside HAL before
 * LibXR regains control, so the BSP must establish the same conditions before
 * construction. The BSP must also wire that vector to `HAL_DMA_IRQHandler()`; this
 * backend cannot check handler wiring. The UART vector must remain enabled and dispatch
 * `HAL_UART_IRQHandler()`, because normal TX completion and stopped active TX awaiting
 * UART TC both use it as their final non-blocking carrier.
 *
 * 当前支持传统 STM32 Stream、Channel、BDMA 循环 RX，以及 STM32H5/U5/U3/N6/H7RS
 * 循环 linked-list GPDMA RX。其他 suspend/linked-list 系列和非 GPDMA 控制器保持拒绝，
 * 直到其 HAL 与硬件契约被独立审核。GPDMA adapter 将 `DMAT`/`DMAR` 交给系列 HAL，并
 * 仅在 HAL 发布 BUSY-to-ABORT 的短窗口屏蔽受影响的 channel vector。启用 D-cache 的
 * 目标上，RX 存储区首尾必须按 cache line 对齐，以免 invalidation 丢弃无关 dirty data。
 * / This backend supports traditional STM32 Stream, Channel, and BDMA circular RX, plus
 * the STM32H5/U5/U3/N6/H7RS circular linked-list GPDMA RX path. Other
 * suspend/linked-list families and non-GPDMA controllers remain rejected until their
 * HAL and hardware contracts are reviewed independently. The GPDMA adapter leaves
 * `DMAT`/`DMAR` handling to the family HAL and masks only the affected channel vector
 * around HAL's BUSY-to-ABORT publication. On D-cache targets, enabled RX storage must
 * start and end on cache-line boundaries so invalidating DMA-written bytes cannot
 * discard unrelated dirty data.
 *
 * pending RX line error 导致的已知 `HAL_ERROR` 会作为 pending control step 返回；UART
 * handler 已发布 error callback，或安排 DMA-abort callback 作为专用 retry carrier。首次
 * 构造 arm 与 CONFIG/recovery 均接受该瞬态；其他 RX-arm 失败仍会 fail-fast，避免把停止
 * 的接收路径误报为成功。 / A documented `HAL_ERROR` caused by a pending RX line error
 * is reported as a pending control step; the UART handler has already published the
 * error callback or arranged its DMA-abort callback as the dedicated retry carrier.
 * This transient is accepted during the initial constructor arm and CONFIG/recovery.
 * Other RX-arm failures remain fail-fast so a stopped receive path cannot be mistaken
 * for success.
 *
 * 构造完成后，本后端独占 UART/DMA data path 及其 HAL handle。应用不得直接对这些
 * handle 调用 HAL UART transmit、DMA start/stop 或 UART/DMA abort API；这些调用可能在
 * 未发布 `HAL_UART_TxCpltCallback()` 时关闭 TCIE，从而破坏 CONFIG/recovery 使用的旧
 * generation retirement 证明。 / After construction, this backend exclusively owns
 * the UART/DMA data path and their HAL handles. Application code must not directly call
 * HAL UART transmit, DMA start/stop, or UART/DMA abort APIs on those handles. Such calls
 * could disable TCIE without publishing `HAL_UART_TxCpltCallback()` and invalidate the
 * old-generation retirement proof used by CONFIG and recovery.
 *
 * BSP 必须为本 UART 的 UART、TX-DMA、RX-DMA IRQ 配置相同 NVIC 抢占优先级，使 vendor
 * HAL handler 不能互相嵌套；subpriority 可以不同。LibXR 仅从 HAL callback 边界开始
 * 串行化，无法修复相关 IRQ 嵌套已造成的 HAL-handle 竞争。 / The BSP must assign this
 * UART's UART, TX-DMA, and RX-DMA IRQs the same NVIC preemption priority so their vendor
 * HAL handlers cannot nest each other; their subpriorities may differ. LibXR starts
 * serialization only at the HAL callback boundary and cannot repair HAL-handle races
 * caused by nested related IRQs.
 *
 * 不得从本 UART 的 HAL callback，或能抢占其 UART、TX-DMA、RX-DMA IRQ 的 ISR 调用
 * `SetConfig()`。vendor handler 已触碰硬件状态后，仅 callback 边界无法使该调用安全。
 * / Do not call `SetConfig()` from this UART's HAL callbacks or from an ISR that can
 * preempt its UART, TX-DMA, or RX-DMA IRQ. The callback-only HAL boundary cannot make
 * such a caller safe after the vendor handler has touched hardware state.
 */
class STM32UART : public UART
{
#if defined(LIBXR_STM32_UART_GPDMA)
  friend class UartLinkedListDmaRxModel<STM32GpdmaUartAdapter::RX_NODE_COUNT>;
#else
  friend class UartCircularDmaRxModel;
#endif
  friend class UartDmaModel<STM32UART, UartDirectPolicy>;
  friend void ::HAL_UARTEx_RxEventCallback(UART_HandleTypeDef* huart, uint16_t size);
  friend void ::HAL_UART_TxCpltCallback(UART_HandleTypeDef* huart);
  friend void ::HAL_UART_ErrorCallback(UART_HandleTypeDef* huart);

 public:
  /**
   * @brief WritePort 提交入口 / WritePort submission entry
   * @param port 发起提交的写端口 / Write port issuing the submission
   * @param in_isr 当前调用是否位于 ISR / Whether the call is in an ISR
   * @return 提交结果 / Submission result
   */
  static ErrorCode WriteFun(WritePort& port, bool in_isr);

  /**
   * @brief ReadPort 读取入口 / ReadPort read entry
   * @param port 发起读取的读端口 / Read port issuing the read
   * @param in_isr 当前调用是否位于 ISR / Whether the call is in an ISR
   * @return 始终为 `PENDING`；RX producer 后续完成读取 / Always `PENDING`; the RX
   * producer completes the read later
   */
  static ErrorCode ReadFun(ReadPort& port, bool in_isr);

  /**
   * @brief 构造并接管 STM32 UART/DMA data path / Construct and take ownership of an
   * STM32 UART/DMA data path
   * @param uart_handle 已由 CubeMX/HAL 初始化的 UART handle / UART handle initialized by
   * CubeMX/HAL
   * @param dma_buff_rx DMA 可写的 RX 存储区 / DMA-writable RX storage
   * @param dma_buff_tx TX DMA 双缓冲存储区 / TX DMA double-buffer storage
   * @param tx_queue_size 待发送记录队列深度 / Pending TX record queue depth
   */
  STM32UART(UART_HandleTypeDef* uart_handle, RawData dma_buff_rx, RawData dma_buff_tx,
            uint32_t tx_queue_size = 5);

  /**
   * @brief 提交一次串行化 UART 配置 / Submit one serialized UART configuration
   * @param config 新的帧格式和波特率 / New framing and baud rate
   * @return 前一个配置仍未完成时返回 `BUSY` / `BUSY` while an earlier configuration
   * request is outstanding
   * @warning 不得从本 UART 的 HAL callback，或能抢占其 UART/TX-DMA/RX-DMA IRQ 域的
   * ISR 调用 / Do not call from this UART's HAL callbacks or from an ISR that can preempt
   * its UART/TX-DMA/RX-DMA IRQ domain
   */
  ErrorCode SetConfig(UART::Configuration config);

  /**
   * @brief 在 runtime CONFIG 状态机外重新启动既有 RX DMA / Re-arm the configured RX DMA
   * outside the runtime CONFIG state machine
   * @warning 这是兼容接口；整个调用期间，调用者必须保证 UART/RX-DMA callback、
   * CONFIG/recovery 与 RX producer 均静止。普通运行时重配置应使用 `SetConfig()`。 /
   * Compatibility hook. The caller must keep UART/RX-DMA callbacks, CONFIG/recovery,
   * and the RX producer quiescent for the entire call. Use `SetConfig()` for ordinary
   * runtime reconfiguration.
   */
  void SetRxDMA();

  /**
   * @brief 从 HAL RX callback 提交新产生的 DMA 数据 / Submit newly produced DMA data
   * from a HAL RX callback
   * @param in_isr 当前 callback 是否位于 ISR / Whether the callback is in an ISR
   * @note RX/config gate 会在首次读取 DMA producer 位置前取得 / The RX/config gate is
   * acquired before the first DMA producer-position read
   */
  void OnRxDataAvailable(bool in_isr);

  ReadPort _read_port;
  WritePort _write_port;

  UartDirectPolicy execution_policy_;
  STM32RxDmaModel rx_dma_model_;
#if defined(LIBXR_STM32_UART_GPDMA)
  STM32GpdmaUartAdapter gpdma_adapter_;
#endif
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
  void CaptureStoppedTx();
  void FinalizeStopped(DMA_HandleTypeDef* dma_handle, bool in_isr);

#if defined(LIBXR_STM32_UART_GPDMA)
  RxArmResult StartLinkedListDmaRx(uint8_t* data, size_t size, size_t descriptor_count);
  [[nodiscard]] uint8_t* GetLinkedListDmaRxProducer() const;
  void PrepareLinkedListDmaRxForCpu(uint8_t* data, size_t size);
#else
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
#endif

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
