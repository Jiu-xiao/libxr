#pragma once

#include <cstddef>
#include <cstdint>

#include "main.h"

#if defined(STM32H5) || defined(STM32U5) || defined(STM32U3) || defined(STM32N6) || \
    defined(STM32H7RS)
#define LIBXR_STM32_UART_GPDMA 1
#endif

#if defined(LIBXR_STM32_UART_GPDMA) && defined(HAL_UART_MODULE_ENABLED)

#if !defined(HAL_DMA_MODULE_ENABLED)
#error "LibXR STM32 GPDMA UART support requires the HAL DMA module"
#endif

#if !defined(DMA_LINKEDLIST_CIRCULAR) || !defined(DMA_GPDMA_LINEAR_NODE) || \
    !defined(DMA_IT_SUSP) || !defined(DMA_FLAG_SUSP) || !defined(IS_GPDMA_INSTANCE)
#error "LibXR STM32 GPDMA UART support requires the HAL linked-list GPDMA API"
#endif

#if defined(STM32H503xx) || defined(STM32H523xx) || defined(STM32H533xx) || \
    defined(STM32H562xx) || defined(STM32H563xx) || defined(STM32H573xx)
#define LIBXR_STM32_UART_REQUIRES_DMA_REQUEST_WA 1
#elif defined(STM32U575xx) || defined(STM32U585xx)
#define LIBXR_STM32_UART_REQUIRES_DMA_REQUEST_WA 1
#endif

#if defined(LIBXR_STM32_UART_REQUIRES_DMA_REQUEST_WA)
#if !defined(USART_DMAREQUESTS_SW_WA)
#error "This STM32 HAL must preserve UART DMA request bits for its USART erratum"
#endif
#endif

namespace LibXR
{

/**
 * @brief 通用 UART DMA 模型使用的 STM32 GPDMA adapter / STM32 GPDMA adapter used by
 * the common UART DMA model
 *
 * adapter 拥有私有的四节点 linear-addressing RX queue，但不拥有 payload 存储区。它在
 * 首次静止的 RX start 时替换 CubeMX seed queue，仅通过
 * `GetLinkedListDmaRxProducer()` 采样实时 producer，并在不修改 HAL handle 状态的前提
 * 下 join 各 channel 的异步 abort。只接受 GPDMA handle；LPDMA 和 HPDMA 需要独立的
 * controller-specific 验证。UART `DMAT` 与 `DMAR` 仍由系列 HAL 按 erratum-aware abort
 * 规则处理。 / The adapter owns a private four-node linear-addressing RX queue but not
 * its payload storage. It replaces the CubeMX seed queue at the first stopped RX start,
 * samples the live producer only through `GetLinkedListDmaRxProducer()`, and joins
 * asynchronous per-channel aborts without changing HAL handle state. Only GPDMA handles
 * are accepted; LPDMA and HPDMA require separate controller-specific validation. UART
 * `DMAT` and `DMAR` remain under the family HAL's erratum-aware abort behavior.
 */
class STM32GpdmaUartAdapter
{
 public:
  /**
   * @brief 私有循环 RX queue 的 descriptor 数量 / Descriptor count of the private
   * circular RX queue
   */
  static constexpr size_t RX_NODE_COUNT = 4U;

  /**
   * @brief HAL DMA abort 完成 callback 类型 / HAL DMA-abort completion callback type
   */
  using AbortCallback = void (*)(DMA_HandleTypeDef* dma_handle);

  /**
   * @brief 绑定由 STM32 UART 后端独占的 HAL handle / Bind the HAL handle exclusively
   * owned by an STM32 UART backend
   * @param uart_handle 已初始化且生命周期覆盖本 adapter 的 UART handle / Initialized
   * UART handle that outlives this adapter
   */
  explicit STM32GpdmaUartAdapter(UART_HandleTypeDef* uart_handle);

  /**
   * @brief 构建、挂接并启动私有循环 linked-list RX queue / Build, attach, and start
   * the private circular linked-list RX queue
   *
   * `total_size` 被分成四个等长 descriptor block；后续调用必须继续提供同一个 queue 与
   * payload。返回的 HAL status 会保留 UART line-error 情况，供所属后端把它分类为 deferred
   * recovery carrier。 / `total_size` is split into four equal descriptor blocks. On
   * later calls the same queue and payload must be supplied. The returned HAL status
   * preserves the UART line-error case for the owning backend to classify as a deferred
   * recovery carrier.
   *
   * @param buffer DMA 可写的 RX payload 存储区 / DMA-writable RX payload storage
   * @param total_size RX 存储区总字节数 / Total RX storage size in bytes
   * @param descriptor_count 必须等于 `RX_NODE_COUNT` 的 descriptor 数 / Descriptor
   * count, which must equal `RX_NODE_COUNT`
   * @param in_isr 当前调用是否位于 ISR / Whether the call is in an ISR
   * @return HAL RX 启动状态 / HAL RX-start status
   */
  HAL_StatusTypeDef StartLinkedListDmaRx(uint8_t* buffer, size_t total_size,
                                         size_t descriptor_count, bool in_isr);

  /**
   * @brief 精确读取一次实时 GPDMA destination address / Read the live GPDMA
   * destination address exactly once
   * @return 当前 DMA producer 地址 / Current DMA producer address
   */
  [[nodiscard]] uint8_t* GetLinkedListDmaRxProducer() const;

  /**
   * @brief 使一段 DMA 写入的 RX 数据对 CPU 可见 / Make one DMA-written RX span visible
   * to the CPU
   * @param data RX 数据段起始地址 / RX span start address
   * @param size RX 数据段字节数 / RX span size in bytes
   */
  static void PrepareLinkedListDmaRxForCpu(uint8_t* data, size_t size);

  /**
   * @brief 仅关闭 UART TC 通知，并将 DMA request 处理留给系列 HAL / Disable only UART
   * TC notification and leave DMA request handling to the family HAL
   */
  void CloseTxTerminalSource() const;

  /**
   * @brief 启动或 join 一次非阻塞 GPDMA channel abort / Start or join one non-blocking
   * GPDMA channel abort
   *
   * BUSY channel 要求进入时其 NVIC vector 已使能。该 vector 仅在 HAL 发布
   * BUSY-to-ABORT 的窗口内被屏蔽，并在不清除 pending 状态的前提下恢复到原先的精确
   * enable 状态。 / A BUSY channel requires its NVIC vector to be enabled on entry. The
   * vector is masked only across HAL's BUSY-to-ABORT publication and restored to its
   * exact prior enable state without clearing pending status.
   *
   * @param dma_handle 要停止的 GPDMA channel handle / GPDMA channel handle to stop
   * @param callback HAL abort 完成 callback / HAL abort-completion callback
   * @param in_isr 当前调用是否位于 ISR / Whether the call is in an ISR
   * @return abort 已启动、已成功 join 或 channel 已停止时为 true / True when abort was
   * started, successfully joined, or the channel is already stopped
   */
  [[nodiscard]] bool LaunchStop(DMA_HandleTypeDef* dma_handle, AbortCallback callback,
                                bool in_isr);

  /**
   * @brief 检查 HAL 与 GPDMA channel 是否都已停止 / Check whether HAL and the GPDMA
   * channel both reached the stopped state
   * @param dma_handle 待检查的 DMA handle，可为空 / DMA handle to inspect; may be null
   * @return 两层状态均已停止时为 true / True only when both layers are stopped
   */
  [[nodiscard]] static bool StopComplete(DMA_HandleTypeDef* dma_handle);

  /**
   * @brief 检查 UART 的两个 DMA 方向是否均已停止或不存在 / Check whether both UART
   * DMA directions are stopped or absent
   * @return 两个方向都静止时为 true / True when both directions are quiescent
   */
  [[nodiscard]] bool AllStopsComplete() const;

  /**
   * @brief 采集已停止普通 TX 的证据，但不从 CBR1 推断完成 / Capture stopped normal-TX
   * evidence without inferring completion from CBR1
   *
   * RESET 后的 GPDMA block-count 回读不作为完成证据。adapter 会记录错误，但保守地保持
   * `payload_complete == false`，使尚未退休的 active record 从 byte zero 重发。 /
   * Post-RESET GPDMA block-count readback is not used as completion evidence. The
   * adapter records errors but conservatively leaves `payload_complete` false so an
   * unretired active record is replayed from byte zero.
   *
   * @param dma_handle 已停止的 TX DMA handle / Stopped TX DMA handle
   * @param evidence_captured 已采集 terminal 证据的累计标志 / Accumulated terminal
   * evidence flag
   * @param payload_complete payload 已完整传输的累计标志 / Accumulated payload-complete
   * flag
   * @param error DMA 错误累计标志 / Accumulated DMA-error flag
   */
  void CaptureStoppedTx(DMA_HandleTypeDef* dma_handle, bool& evidence_captured,
                        bool& payload_complete, bool& error) const;

  /**
   * @brief 验证停止后置条件、复位 linked-list 进度并清除 IRQ 源 / Verify stop
   * postconditions, reset linked-list progress, and clear IRQ sources
   * @param dma_handle 已停止的 DMA handle / Stopped DMA handle
   * @param in_isr 当前调用是否位于 ISR / Whether the call is in an ISR
   */
  static void FinalizeStopped(DMA_HandleTypeDef* dma_handle, bool in_isr);

 private:
  // Keeping all state in the descriptor-aligned block avoids rounding the adapter up
  // to multiple 256-byte allocation units while retaining the GPDMA address-window
  // guarantee for the four nodes at the beginning of the block.
  struct alignas(256) StateBlock
  {
    DMA_NodeTypeDef nodes_[RX_NODE_COUNT];
    DMA_QListTypeDef rx_queue_{};
    UART_HandleTypeDef* uart_handle_ = nullptr;
    uint8_t* rx_buffer_ = nullptr;
    size_t rx_total_size_ = 0U;
    size_t rx_node_size_ = 0U;
    bool rx_queue_built_ = false;
  };

  static_assert(offsetof(StateBlock, nodes_) == 0U);
  static_assert(sizeof(StateBlock) == 256U);

  [[nodiscard]] static bool IsStopped(DMA_HandleTypeDef* dma_handle);
  [[nodiscard]] static bool HasTransferError(DMA_HandleTypeDef* dma_handle);
  static void DisableInterrupts(DMA_HandleTypeDef* dma_handle);
  static void ClearFlags(DMA_HandleTypeDef* dma_handle);

  void BuildRxQueue(uint8_t* buffer, size_t total_size, size_t descriptor_count,
                    bool in_isr);

  StateBlock state_{};
};

static_assert(sizeof(STM32GpdmaUartAdapter) == 256U);

}  // namespace LibXR

#endif
