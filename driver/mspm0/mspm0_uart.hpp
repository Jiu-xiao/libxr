#pragma once

#include <ti/driverlib/dl_dma.h>
#include <ti/driverlib/dl_uart_main.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <type_traits>
#include <utility>

#include "driver/common/serialized_service.hpp"
#include "mspm0_dma_dispatcher.hpp"
#include "ti_msp_dl_config.h"
#include "uart.hpp"
#include "uart/uart_dma_model.hpp"

namespace LibXR
{

/**
 * @brief 仅由 UART IRQ 推进的 MSPM0 UART 执行策略 / UART-IRQ-only MSPM0 UART execution
 * policy
 *
 * 普通 publisher 只保存持久事件并挂起 UART IRQ，不调用也不保留传入的栈上 handler。UART
 * ISR 在首次访问受保护硬件前取得 service 所有权。 / Ordinary publishers only store
 * durable events and pend the UART IRQ; they neither call nor retain the supplied
 * stack-local handler. The UART ISR claims the service before its first protected
 * hardware access.
 */
class MSPM0UartIrqPolicy
{
 public:
  /**
   * @brief 绑定作为唯一 service carrier 的 UART IRQ / Bind the UART IRQ used as the sole
   * service carrier
   * @param irqn UART 中断编号 / UART interrupt number
   */
  explicit MSPM0UartIrqPolicy(IRQn_Type irqn) : irqn_(irqn) {}

  /**
   * @brief 发布事件并挂起 UART IRQ / Publish events and pend the UART IRQ
   * @tparam Handler 栈上 handler 类型；本路径不会调用或保留 / Stack-local handler type;
   *                 this path neither calls nor retains it
   * @param events 要合并的事件位 / Event bits to merge
   * @param handler 仅满足通用 policy 接口的栈上 handler / Stack-local handler supplied by
   *                the common policy interface
   * @return 始终返回 `false`，表示 publisher 未同步取得 owner / Always `false` because
   * the publisher never acquires the owner synchronously
   */
  template <typename Handler>
  bool Invoke(uint32_t events, Handler&& handler) noexcept
  {
    static_cast<void>(handler);
    service_.Publish(events);
    __DMB();
    NVIC_SetPendingIRQ(irqn_);
    return false;
  }

  /**
   * @brief 在 UART IRQ 中取得 service 并处理 IRQ 快照 / Claim the service and process an
   * IRQ snapshot in the UART ISR
   * @tparam Source IRQ source 函数类型 / IRQ-source function type
   * @tparam Handler owner 处理器类型 / Owner-handler type
   * @param source 读取并确认 UART IRQ 快照的函数 / Function that reads and acknowledges
   *               the UART IRQ snapshot
   * @param handler 处理快照并返回 continuation 事件的函数 / Function that processes the
   *                snapshot and returns continuation events
   * @return 本次调用取得并推进 owner 时返回 `true` / `true` when this call acquires and
   *         advances the owner
   */
  template <typename Source, typename Handler>
  bool InvokeIrq(Source&& source, Handler&& handler) noexcept
  {
    return service_.ClaimAndInvoke(std::forward<Source>(source),
                                   [this, &handler](uint32_t snapshot) noexcept
                                   {
                                     const uint32_t continuation = handler(snapshot);
                                     service_.Publish(continuation);
                                   });
  }

  MSPM0UartIrqPolicy(const MSPM0UartIrqPolicy&) = delete;
  MSPM0UartIrqPolicy& operator=(const MSPM0UartIrqPolicy&) = delete;

 private:
  IRQn_Type irqn_;
  SerializedService service_{};
};

/**
 * @brief 通用 UART TX 双缓冲使用的对齐连续存储 / Aligned contiguous storage for the
 * common UART TX double buffer
 *
 * 总容量被拆分为两个相等的 DMA block。由存储类型直接保证对齐，可防止普通字节数组通过公共
 * 构造 helper 后才在运行时触发 `DoubleBufferStorage` 校验失败。 / The total extent is
 * split into two equal DMA blocks. Keeping the alignment in the storage type prevents a
 * plain byte array from passing the public construction helper and then failing
 * `DoubleBufferStorage` validation at runtime.
 *
 * @tparam TotalSize 总存储字节数；必须为非零偶数、满足双缓冲对齐，且每个半块不超过
 *                   65535 字节 / Total storage extent in bytes; it must be nonzero,
 *                   even, double-buffer aligned, and at most 65535 bytes per half
 */
template <size_t TotalSize>
class alignas(size_t) MSPM0UARTTxBuffer
{
  static_assert(TotalSize > 0U, "MSPM0 UART TX storage must not be empty");
  static_assert((TotalSize % 2U) == 0U,
                "MSPM0 UART TX storage must contain two equal blocks");
  static_assert((TotalSize % (2U * alignof(size_t))) == 0U,
                "MSPM0 UART TX storage must satisfy common double-buffer alignment");
  static_assert((TotalSize / 2U) <= 0xFFFFU, "MSPM0 UART TX block exceeds DMA size");

 public:
  /**
   * @brief 取得 DMA 可写存储起始地址 / Get the DMA-writable storage address
   * @return 指向连续 `TotalSize` 字节的指针 / Pointer to `TotalSize` contiguous bytes
   */
  [[nodiscard]] uint8_t* Data() { return storage_.data(); }

  /**
   * @brief 取得只读存储起始地址 / Get the read-only storage address
   * @return 指向连续 `TotalSize` 只读字节的指针 / Pointer to `TotalSize` contiguous
   *         read-only bytes
   */
  [[nodiscard]] const uint8_t* Data() const { return storage_.data(); }

  /**
   * @brief 取得总存储字节数 / Get the total storage extent in bytes
   * @return 模板参数 `TotalSize` / Template argument `TotalSize`
   */
  [[nodiscard]] static constexpr size_t Size() { return TotalSize; }

 private:
  alignas(size_t) std::array<uint8_t, TotalSize> storage_{};
};

/**
 * @brief 共用 TX DMA owner 且显式区分 RX 能力的 MSPM0 UART 后端 / MSPM0 UART backend
 * with common TX DMA ownership and explicit RX capability
 *
 * TX 与 control 语义全部来自 `UartDmaModel`。Main RX 使用阈值为一的逐字节中断；每个
 * Extend RX 使用一个 full-channel repeated `2N` DMA ring。FULL 完成事件是必需条件；只有
 * binding 选中支持 Pre-IRQ 的通道时才启用 DMA HALF。 / All TX and control semantics come
 * from `UartDmaModel`. Main RX uses threshold-one byte interrupts. Every Extend RX uses
 * one full-channel repeated `2N` DMA ring. FULL completion is mandatory; DMA HALF
 * Pre-IRQ is enabled only when the binding selects a channel that provides it.
 *
 * 使用 HALF 时，BSP 必须留出硬件余量并证明
 * `T_broker + T_uart_irq + T_copy < N * frame_bits / baud`：`T_broker` 是共享 DMA IRQ
 * 到 broker callback 的延迟，`T_uart_irq` 是 callback 挂起 UART IRQ 后到 UART owner 运行
 * 的延迟，`T_copy` 是校验并复制一个半 ring 的时间，`N` 是半 ring 字节数，`frame_bits`
 * 是每个 UART 字符的 start、data、parity 和 stop bit 总数。只有 FULL 的 binding 具有更紧
 * 的 wrap service deadline，任何已观察到的覆盖都必须视为数据丢失。 / With HALF
 * available, the BSP must prove
 * `T_broker + T_uart_irq + T_copy < N * frame_bits / baud` with hardware margin:
 * `T_broker` is the latency from the shared DMA IRQ to the broker callback, `T_uart_irq`
 * is the latency from that callback pending the UART IRQ until the UART owner runs,
 * `T_copy` is the time to validate and copy one half-ring, `N` is the half-ring size in
 * bytes, and `frame_bits` is the start, data, parity, and stop-bit count per UART
 * character. A FULL-only binding has a tighter wrap-service deadline and must treat any
 * observed overwrite as loss.
 *
 * 驱动根据当前时钟和 UART 帧格式计算 LIN compare；match 只把稳定的 partial tail 刷入
 * 字节流，不等价于 UART IDLE，也不发布帧边界。G3507 的固定 UART TX DMA 还因
 * `DMA_ERR_01` 要求 BSP 满足 `MCLK == ULPCLK`；驱动始终同时使能 TX 和 RX，以按
 * `UART_ERR_02` 要求在等待 EOT 时保持 RXE。 / The driver derives LIN compare from the
 * current clock and UART framing; a match only flushes a stable partial tail into the
 * byte stream. It is not UART IDLE and does not publish a frame boundary. G3507 fixed
 * UART TX DMA also requires the BSP clock contract `MCLK == ULPCLK` because of
 * `DMA_ERR_01`. The driver always enables both TX and RX, keeping RXE set while G3507
 * waits for EOT as required by `UART_ERR_02`.
 */
class MSPM0UART : public UART
{
  friend class UartDmaModel<MSPM0UART, MSPM0UartIrqPolicy>;

 public:
  /** @brief MSPM0 UART 接收数据路径 / MSPM0 UART receive data path */
  enum class RxMode : uint8_t
  {
    MAIN_BYTE_IRQ,  ///< Main UART 逐字节 IRQ / Main UART byte IRQ
    EXTEND_DMA,     ///< Extend UART 重复 DMA ring / Extend UART repeated DMA ring
  };

  /**
   * @brief 由公共 INIT 宏填充的底层 SysConfig 描述 / Low-level SysConfig descriptor
   * populated by the public INIT macros
   *
   * 普通调用方应使用 `MSPM0_UART_MAIN_INIT` 或 `MSPM0_UART_EXTEND_INIT`，不应逐字段填写。
   * / Ordinary callers use `MSPM0_UART_MAIN_INIT` or `MSPM0_UART_EXTEND_INIT` rather than
   * populating fields individually.
   */
  struct Resources
  {
    UART_Regs* instance;     ///< UART 寄存器实例 / UART register instance
    IRQn_Type irqn;          ///< UART 中断编号 / UART interrupt number
    uint32_t clock_freq;     ///< UART 功能时钟频率，单位 Hz / UART clock frequency in Hz
    uint8_t index;           ///< UART instance map 索引 / UART instance-map index
    RxMode rx_mode;          ///< 接收数据路径 / Receive data path
    bool rx_half_interrupt;  ///< 是否使用 RX DMA HALF Pre-IRQ / Whether RX uses HALF
    uint8_t dma_tx_channel;  ///< TX DMA 通道编号 / TX DMA channel number
    uint8_t dma_tx_trigger;  ///< TX DMA trigger / TX DMA trigger
    uint8_t dma_rx_channel;  ///< RX DMA 通道编号 / RX DMA channel number
    uint8_t dma_rx_trigger;  ///< RX DMA trigger / RX DMA trigger
  };

  /** @brief Main RX 不使用 RX DMA 时的无效通道值 / Invalid RX DMA channel for Main RX */
  static constexpr uint8_t INVALID_DMA_CHANNEL = 0xFFU;

  /**
   * @brief 构造并接管一个 UART 及其命名 DMA 资源 / Construct and take ownership of one
   * UART and its named DMA resources
   * @param res 构造宏产生的底层资源 / Low-level resources produced by a construction
   * macro
   * @param tx_dma_storage 可写连续 `2N` 通用 TX 存储 / Writable contiguous `2N` common TX
   *                       storage
   * @param rx_dma_storage Main RX 传空存储；Extend RX 传可写连续 `2N` ring / Empty for
   * Main RX; writable contiguous `2N` ring for Extend RX
   * @param tx_queue_size 待发送记录队列深度 / Pending TX record queue depth
   * @param rx_queue_capacity 软件接收字节队列容量 / Software RX byte queue capacity
   * @param config 初始帧格式和波特率 / Initial framing and baud rate
   * @pre SysConfig 必须已为 UART 和 DMA 寄存器块供电并提供时钟；不得在
   *      `SYSCFG_DL_init()` 之前的全局初始化阶段构造 / SysConfig initialization must
   *      already have powered and clocked the UART and DMA register blocks; do not
   *      construct during global initialization before `SYSCFG_DL_init()`
   * @pre DMA 存储必须保持可写、DMA 可访问，并与 UART 实例具有相同的应用生命周期 /
   *      DMA storage must remain writable, DMA-accessible, and alive for the same
   *      application lifetime as the UART instance
   * @note 构造成功后实例会永久占有 UART 与 DMA 资源；不支持析构后重新绑定 / After
   *       construction, the instance owns its UART and DMA resources permanently;
   *       teardown and rebinding are not supported
   */
  MSPM0UART(Resources res, RawData tx_dma_storage, RawData rx_dma_storage,
            uint32_t tx_queue_size, uint32_t rx_queue_capacity,
            UART::Configuration config = {115200, UART::Parity::NO_PARITY, 8, 1});

  /**
   * @brief 异步接纳完整 UART 配置 / Asynchronously admit a complete UART configuration
   * @param config 请求的波特率和帧格式 / Requested baud rate and framing
   * @return 接纳后返回 `OK`，已有未完成 CONFIG 时返回 `BUSY`，否则返回校验错误；`OK` 不
   *         表示硬件修改已经完成 / `OK` once admitted, `BUSY` while another CONFIG is
   *         outstanding, or a validation error; `OK` does not mean the hardware change
   *         is already complete
   * @note 可从 task 或普通可屏蔽 ISR 调用；本调用只发布工作，UART IRQ 仍是硬件 owner /
   *       May be called from task context or an ordinary maskable ISR; the call only
   *       publishes work and UART IRQ remains the hardware owner
   * @pre 仅支持 NMI 和 HardFault 之外的单核执行；SMP 调用方需要独立平台契约 /
   *      Single-core execution outside NMI and HardFault; SMP callers require a separate
   *      platform contract
   */
  ErrorCode SetConfig(UART::Configuration config) override;

  /**
   * @brief WritePort 提交入口 / WritePort submission entry
   * @param port 发起提交的写端口 / Write port issuing the submission
   * @param in_isr 当前调用是否位于 ISR / Whether the call is in an ISR
   * @return 始终返回 `PENDING`；记录已发布给 UART IRQ owner 异步处理 / Always
   *         `PENDING` after publishing the record for asynchronous UART-IRQ processing
   */
  static ErrorCode WriteFun(WritePort& port, bool in_isr);

  /**
   * @brief 分发指定 MSPM0 UART 实例的中断 / Dispatch one MSPM0 UART instance interrupt
   * @param index `ResolveIndex()` 产生的 UART instance map 索引 / UART instance-map index
   *              produced by `ResolveIndex()`
   * @note 越界或尚未绑定的索引会被忽略 / Out-of-range or unbound indices are ignored
   */
  static void OnInterrupt(uint8_t index);

  /**
   * @brief 检查当前执行是否位于 exception 上下文 / Check for exception context
   * @return `IPSR` 非零时返回 `true`，包括普通 IRQ、NMI、HardFault 及其他 exception /
   *         `true` when `IPSR` is nonzero, including ordinary IRQs, NMI, HardFault, and
   *         other exceptions
   */
  static bool InIsr();

  /**
   * @brief 从 SysConfig 初始化后的寄存器构造 UART 配置 / Build a UART configuration from
   * initialized SysConfig registers
   * @param instance UART 寄存器实例 / UART register instance
   * @param baudrate SysConfig 生成的初始波特率 / Initial baud rate generated by SysConfig
   * @return 当前字长、校验、停止位和指定波特率组成的配置 / Configuration containing the
   *         current word length, parity, stop bits, and supplied baud rate
   * @pre `instance` 非空且 `baudrate` 非零 / `instance` is non-null and `baudrate` is
   *      nonzero
   */
  static UART::Configuration BuildConfigFromSysCfg(UART_Regs* instance,
                                                   uint32_t baudrate);

  /**
   * @brief 取得当前 RX 数据路径 / Get the current RX data path
   * @return 构造时固定的 RX 模式 / RX mode fixed at construction
   */
  [[nodiscard]] RxMode GetRxMode() const { return res_.rx_mode; }

  /**
   * @brief 检查 Extend RX 是否启用 HALF Pre-IRQ / Check whether RX HALF is enabled
   * @return binding 启用 HALF Pre-IRQ 时返回 `true` / `true` when the binding enables
   *         HALF Pre-IRQ
   */
  [[nodiscard]] bool RxHalfInterruptEnabled() const { return res_.rx_half_interrupt; }

  /**
   * @name 接收与恢复诊断 / RX and recovery diagnostics
   *
   * getter 分别执行 relaxed load，不构成跨字段一致快照。计数器不提供 reset，为不饱和
   * `uint32_t` 并会自然回绕。 / Each getter performs an independent relaxed load, not a
   * consistent multi-field snapshot. Counters have no reset operation, are non-saturating
   * `uint32_t` values, and wrap naturally.
   * @{
   */

  /**
   * @brief 取得软件确认丢弃的 RX 字节数 / Get software-confirmed dropped RX bytes
   * @return 未入队、错误拒收或控制期清除的累计字节；不估算 overrun 丢失量或不可见的完整
   *         DMA ring 覆盖 / Cumulative bytes rejected, not enqueued, or discarded during
   *         control; excludes estimated overrun loss and invisible whole-ring overwrites
   */
  [[nodiscard]] uint32_t GetRxDropCount() const;

  /**
   * @brief 取得 RX 数据损失 generation / Get the RX loss generation
   * @return 软件标记的 loss window 或 DMA epoch 失效次数，不是丢失字节数 / Number of
   *         software-marked loss windows or DMA epoch invalidations, not bytes lost
   */
  [[nodiscard]] uint32_t GetRxLossGeneration() const;

  /**
   * @brief 取得 RX ring service deadline 违规次数 / Get RX ring deadline violations
   * @return Extend RX 无法把 DMA fact、位置和边界状态判定为一致时的累计次数 / Number of
   *         Extend RX samples whose DMA fact, position, and boundary state were
   * inconsistent
   */
  [[nodiscard]] uint32_t GetRxDeadlineViolationCount() const;

  /**
   * @brief 取得丢弃的陈旧 RX DMA 工作批次数 / Get discarded stale RX DMA work batches
   * @return 被丢弃的陈旧 RX DMA 工作批次，包括合并 DMA fact 或被 RX admission gate
   *         拒绝的 deferred partial flush；不保证逐硬件事件计数 / Discarded stale RX DMA
   *         work batches, including coalesced DMA facts or a deferred partial flush
   *         rejected by the RX admission gate; not one count per hardware event
   */
  [[nodiscard]] uint32_t GetRxStaleEventCount() const;

  /**
   * @brief 取得观察到的 UART RX overrun 指示数 / Get observed UART RX overrun indications
   * @return 观察次数，不是估算丢失字节数 / Observation count, not estimated bytes lost
   */
  [[nodiscard]] uint32_t GetRxOverrunCount() const;

  /**
   * @brief 取得观察到的 UART RX framing 错误指示数 / Get observed RX framing indications
   * @return framing 错误类别的观察次数 / Framing-error category observation count
   */
  [[nodiscard]] uint32_t GetRxFramingErrorCount() const;

  /**
   * @brief 取得观察到的 UART RX parity 错误指示数 / Get observed RX parity indications
   * @return parity 错误类别的观察次数 / Parity-error category observation count
   */
  [[nodiscard]] uint32_t GetRxParityErrorCount() const;

  /**
   * @brief 取得观察到的 UART RX break 错误指示数 / Get observed RX break indications
   * @return break 错误类别的观察次数 / Break-error category observation count
   */
  [[nodiscard]] uint32_t GetRxBreakErrorCount() const;

  /**
   * @brief 取得观察到的 UART RX noise 错误指示数 / Get observed RX noise indications
   * @return noise 错误类别的观察次数 / Noise-error category observation count
   */
  [[nodiscard]] uint32_t GetRxNoiseErrorCount() const;

  /**
   * @brief 取得 UART owner 消费的共享 DMA 错误通知数 / Get consumed shared DMA errors
   * @return owner 消费的合并通知次数，不是 DMA 控制器原始错误数 / Number of coalesced
   *         notifications consumed by the owner, not raw DMA controller errors
   */
  [[nodiscard]] uint32_t GetDmaErrorCount() const;

  /**
   * @brief 取得启动 runtime recovery 的累计次数 / Get started runtime recoveries
   * @return 因错误进入 control-stop recovery 的次数 / Number of error-driven control-stop
   *         recoveries started
   */
  [[nodiscard]] uint32_t GetRecoveryCount() const;

  /** @} */

  /** @brief UART 接收端口 / UART receive port */
  ReadPort _read_port;  // NOLINT(readability-identifier-naming)

  /** @brief UART 发送端口 / UART transmit port */
  WritePort _write_port;  // NOLINT(readability-identifier-naming)

  /**
   * @brief 将 UART IRQ 编号转换为 instance map 索引 / Map a UART IRQ number to an
   * instance-map index
   * @param irqn UART 中断编号 / UART interrupt number
   * @return 匹配的索引；不支持时返回 `0xFF` / Matching index, or `0xFF` when unsupported
   */
  static constexpr uint8_t ResolveIndex(IRQn_Type irqn)
  {
    switch (irqn)
    {
#if defined(UART0_BASE)
      case UART0_INT_IRQn:
        return 0U;
#endif
#if defined(UART1_BASE)
      case UART1_INT_IRQn:
        return 1U;
#endif
#if defined(UART2_BASE)
      case UART2_INT_IRQn:
        return 2U;
#endif
#if defined(UART3_BASE)
      case UART3_INT_IRQn:
        return 3U;
#endif
#if defined(UART4_BASE)
      case UART4_INT_IRQn:
        return 4U;
#endif
#if defined(UART5_BASE)
      case UART5_INT_IRQn:
        return 5U;
#endif
#if defined(UART6_BASE)
      case UART6_INT_IRQn:
        return 6U;
#endif
#if defined(UART7_BASE)
      case UART7_INT_IRQn:
        return 7U;
#endif
      default:
        return INVALID_INSTANCE_INDEX;
    }
  }

 private:
  static constexpr uint8_t MAX_UART_INSTANCES = 8U;
  static constexpr uint8_t INVALID_INSTANCE_INDEX = 0xFFU;
  static constexpr uint32_t RX_DMA_FACT_BITS = 3U;
  static constexpr uint32_t RX_DMA_FACT_MASK = (1U << RX_DMA_FACT_BITS) - 1U;
  static constexpr uint32_t RX_DMA_EPOCH_SHIFT = RX_DMA_FACT_BITS;

  enum class RxDmaFact : uint32_t
  {
    HALF = 1U << 0U,
    FULL = 1U << 1U,
    CONFLICT = 1U << 2U,
  };

  enum class RxDmaPhase : uint8_t
  {
    HALF,
    FULL,
  };

  enum class RxDmaSample : uint8_t
  {
    STABLE,
    WAIT_BOUNDARY,
    RETRY,
    INVALID,
  };

  enum class ControlPhase : uint8_t
  {
    IDLE,
    WAIT_EOT,
    WAIT_UART_IDLE,
    QUIESCENT,
  };

  struct Counters
  {
    std::atomic<uint32_t> rx_drop_{0U};
    std::atomic<uint32_t> rx_loss_generation_{0U};
    std::atomic<uint32_t> rx_deadline_violation_{0U};
    std::atomic<uint32_t> rx_stale_event_{0U};
    std::atomic<uint32_t> rx_overrun_{0U};
    std::atomic<uint32_t> rx_framing_{0U};
    std::atomic<uint32_t> rx_parity_{0U};
    std::atomic<uint32_t> rx_break_{0U};
    std::atomic<uint32_t> rx_noise_{0U};
    std::atomic<uint32_t> dma_error_{0U};
    std::atomic<uint32_t> recovery_{0U};
  };

  static constexpr uint32_t FactMask(RxDmaFact fact)
  {
    return static_cast<uint32_t>(fact);
  }

  void HandleInterrupt();
  uint32_t CaptureIrqEvents();
  void CaptureMainRx(uint32_t pending, uint32_t& events);
  void CaptureExtendRx(uint32_t& events);
  void DrainMainRx();
  bool ConsumeRxDmaFacts(uint32_t facts);
  bool FlushPartialRx();
  RxDmaSample SampleRxDmaPosition(size_t& position);
  void PublishRxRange(size_t begin, size_t end);
  void DiscardRxFifo();
  void CountUartInterruptErrors(uint32_t pending);
  static uint32_t RxWordInterruptErrors(uint32_t word);
  void InvalidateRxEpoch();
  void StartRxEpoch();

  [[nodiscard]] ErrorCode ValidateConfig(UART::Configuration config) const;
  void ValidateResources() const;
  void ApplyInitialConfig(UART::Configuration config);
  void ApplyDisabledConfig(UART::Configuration config);
  void ConfigureTxDma();
  void ConfigureRxDma();
  [[nodiscard]] uint32_t RxDmaEventMask() const;
  [[nodiscard]] uint32_t RxDmaBoundaryRawMask() const;
  void StartDataPath();
  void StopDataPathInterrupts();

  UartDmaControlResult AdvanceConfig(UART::Configuration config, bool active_tx,
                                     bool in_isr);
  UartDmaControlProgress CompleteConfig(bool in_isr);
  UartDmaControlResult AdvanceRecovery(bool active_tx, bool in_isr);
  UartDmaControlProgress CompleteRecovery(bool in_isr);
  UartDmaControlResult AdvanceControlStop(bool active_tx, bool error_stop, bool in_isr);
  void BeginControlStop(bool active_tx, bool error_stop);
  void EnterWaitUartIdle();
  bool AdvanceToQuiescence();
  void FinishControl();
  UartDmaTxStartResult StartDmaTx(uint8_t* data, size_t size, int block, bool in_isr);

  static void TxDmaCallback(void* context, uint32_t events);
  static void RxDmaCallback(void* context, uint32_t events);
  void PublishRxDmaFacts(uint32_t facts);
  static uint32_t NextEpoch(uint32_t epoch);

  Resources res_;
  RawData rx_dma_storage_;
  size_t rx_dma_half_size_ = 0U;
  MSPM0UartIrqPolicy execution_policy_;
  UartDmaModel<MSPM0UART, MSPM0UartIrqPolicy> dma_model_;
  MSPM0DmaDispatcher::Registration tx_dma_registration_{};
  MSPM0DmaDispatcher::Registration rx_dma_registration_{};
  std::atomic<uint32_t> rx_dma_facts_{0U};
  std::atomic<uint32_t> rx_epoch_{1U};
  std::atomic<uint32_t> dma_error_pending_{0U};
  Counters counters_{};
  UART::Configuration active_config_{};
  ControlPhase control_phase_ = ControlPhase::IDLE;
  UartOldTxTerminal stopped_tx_terminal_ = UartOldTxTerminal::NONE;
  RxDmaPhase rx_dma_phase_ = RxDmaPhase::HALF;
  size_t rx_dma_cursor_ = 0U;
  bool rx_pushed_in_owner_ = false;
  bool rx_epoch_invalid_ = false;
  bool rx_partial_flush_pending_ = false;
  bool tx_line_active_ = false;
  bool control_active_tx_ = false;
  bool control_error_stop_ = false;
  bool tx_complete_observed_ = false;
  static MSPM0UART* instance_map_[MAX_UART_INSTANCES];
};

namespace Detail
{

template <typename T>
concept MSPM0ByteStorageElement =
    !std::is_const_v<T> && !std::is_volatile_v<T> &&
    (std::is_same_v<T, char> || std::is_same_v<T, signed char> ||
     std::is_same_v<T, unsigned char> || std::is_same_v<T, std::byte>);

template <size_t N>
RawData MSPM0UARTTxStorage(MSPM0UARTTxBuffer<N>& storage)
{
  static_assert(alignof(MSPM0UARTTxBuffer<N>) >= alignof(size_t));
  return RawData{static_cast<void*>(storage.Data()), storage.Size()};
}

template <MSPM0ByteStorageElement T, size_t N>
RawData MSPM0UARTRxDmaStorage(T (&storage)[N])
{
  static_assert(N > 0U, "MSPM0 UART RX DMA storage must not be empty");
  static_assert((N % 2U) == 0U, "MSPM0 UART RX DMA storage must contain two halves");
  static_assert(N <= 0xFFFFU, "MSPM0 UART RX DMA ring exceeds DMA size");
  return RawData{static_cast<void*>(std::data(storage)), N};
}

template <MSPM0ByteStorageElement T, size_t N>
RawData MSPM0UARTRxDmaStorage(std::array<T, N>& storage)
{
  static_assert(N > 0U, "MSPM0 UART RX DMA storage must not be empty");
  static_assert((N % 2U) == 0U, "MSPM0 UART RX DMA storage must contain two halves");
  static_assert(N <= 0xFFFFU, "MSPM0 UART RX DMA ring exceeds DMA size");
  return RawData{static_cast<void*>(std::data(storage)), N};
}

template <IRQn_Type UartIrqn, uint32_t TxDmaChannel, IRQn_Type TxDmaOwnerIrqn,
          bool TxBinding, bool ExtendCapable, bool DispatcherAvailable>
MSPM0UART::Resources MakeMSPM0MainUartResources(UART_Regs* instance, uint32_t clock_freq,
                                                uint8_t tx_dma_trigger)
{
  static_assert(TxBinding, "The named DMA resource is not a UART TX binding");
  static_assert(DispatcherAvailable,
                "MSPM0 UART TX requires the shared DMA IRQ dispatcher binding");
  static_assert(!ExtendCapable,
                "A physical Extend UART must use the MSPM0 Extend DMA path");
  static_assert(TxDmaOwnerIrqn == UartIrqn,
                "The named TX DMA resource belongs to another UART");
  static_assert(TxDmaChannel < DMA_SYS_N_DMA_CHANNEL,
                "MSPM0 UART TX DMA channel is outside the device range");
  return {instance,
          UartIrqn,
          clock_freq,
          MSPM0UART::ResolveIndex(UartIrqn),
          MSPM0UART::RxMode::MAIN_BYTE_IRQ,
          false,
          static_cast<uint8_t>(TxDmaChannel),
          tx_dma_trigger,
          MSPM0UART::INVALID_DMA_CHANNEL,
          0U};
}

template <IRQn_Type UartIrqn, uint32_t TxDmaChannel, IRQn_Type TxDmaOwnerIrqn,
          bool TxBinding, uint32_t RxDmaChannel, IRQn_Type RxDmaOwnerIrqn, bool RxBinding,
          bool ExtendCapable, bool FullRxDmaChannel, bool HalfRxInterrupt,
          bool DispatcherAvailable>
MSPM0UART::Resources MakeMSPM0ExtendUartResources(UART_Regs* instance,
                                                  uint32_t clock_freq,
                                                  uint8_t tx_dma_trigger,
                                                  uint8_t rx_dma_trigger)
{
  static_assert(TxBinding, "The named DMA resource is not a UART TX binding");
  static_assert(RxBinding, "The named DMA resource is not a UART RX binding");
  static_assert(TxDmaOwnerIrqn == UartIrqn,
                "The named TX DMA resource belongs to another UART");
  static_assert(RxDmaOwnerIrqn == UartIrqn,
                "The named RX DMA resource belongs to another UART");
  static_assert(TxDmaChannel < DMA_SYS_N_DMA_CHANNEL,
                "MSPM0 UART TX DMA channel is outside the device range");
  static_assert(TxDmaChannel != RxDmaChannel,
                "MSPM0 UART TX and RX require distinct DMA channels");
  static_assert(ExtendCapable,
                "The selected SysConfig/BSP UART binding is not Extend-capable");
  static_assert(FullRxDmaChannel, "MSPM0 Extend RX requires a full-channel DMA binding");
  static_assert(DispatcherAvailable,
                "MSPM0 Extend RX requires the shared DMA IRQ dispatcher binding");
  static_assert(RxDmaChannel < DMA_SYS_N_DMA_FULL_CHANNEL,
                "MSPM0 Extend RX requires a full DMA channel");
  static_assert(
      !HalfRxInterrupt || MSPM0DmaDispatcher::EarlyInterruptSupported(RxDmaChannel),
      "The selected RX DMA binding does not support a half-transfer interrupt");
  return {instance,
          UartIrqn,
          clock_freq,
          MSPM0UART::ResolveIndex(UartIrqn),
          MSPM0UART::RxMode::EXTEND_DMA,
          HalfRxInterrupt,
          static_cast<uint8_t>(TxDmaChannel),
          tx_dma_trigger,
          static_cast<uint8_t>(RxDmaChannel),
          rx_dma_trigger};
}

}  // namespace Detail

/**
 * @name MSPM0 UART BSP binding 辅助宏 / helper macros
 *
 * ownership 标记应放在 BSP 自有 binding header 中，不得编辑会重新生成的
 * `ti_msp_dl_config.h`。SysConfig 继续负责 `<dma>_CHAN_ID`；binding header 额外提供
 * `<dma>_LIBXR_UART_IRQN`、`<dma>_LIBXR_UART_TX` 与 `<dma>_LIBXR_UART_RX` 中恰好一个，
 * 以及全 BSP 确认值 `LIBXR_MSPM0_DMA_DISPATCHER_AVAILABLE`。Extend RX binding 还要定义
 * `<dma>_LIBXR_FULL_CHANNEL`、`<dma>_LIBXR_HALF_INTERRUPT` 和
 * `<uart>_LIBXR_EXTEND_CAPABLE`。 / Keep ownership markers in a BSP-owned binding header
 * rather than editing the regenerated `ti_msp_dl_config.h`. SysConfig remains
 * authoritative for `<dma>_CHAN_ID`; the binding header adds
 * `<dma>_LIBXR_UART_IRQN`, exactly one of `<dma>_LIBXR_UART_TX` or
 * `<dma>_LIBXR_UART_RX`, and the whole-BSP acknowledgement
 * `LIBXR_MSPM0_DMA_DISPATCHER_AVAILABLE`. An Extend RX binding additionally defines
 * `<dma>_LIBXR_FULL_CHANNEL`, `<dma>_LIBXR_HALF_INTERRUPT`, and
 * `<uart>_LIBXR_EXTEND_CAPABLE`.
 *
 * 这些标记声明完整 BSP 的资源 ownership 和方向。构造宏从展开后的 UART instance token
 * 推导物理 trigger，普通调用方不提供数值 channel、IRQ、trigger 或 capability flag。 /
 * These markers assert whole-BSP resource ownership and direction. Construction macros
 * derive the physical trigger from the expanded UART instance token, so ordinary callers
 * never provide a numeric channel, IRQ, trigger, or capability flag.
 * @{
 */
#define LIBXR_MSPM0_UART_CAT_IMPL(left, right) left##right
#define LIBXR_MSPM0_UART_CAT(left, right) LIBXR_MSPM0_UART_CAT_IMPL(left, right)
#define LIBXR_MSPM0_UART_PROPERTY(name, suffix) LIBXR_MSPM0_UART_CAT(name, suffix)
#define LIBXR_MSPM0_UART_DMA_CHANNEL(dma_name) LIBXR_MSPM0_UART_CAT(dma_name, _CHAN_ID)
#define LIBXR_MSPM0_UART_DMA_PROPERTY(dma_name, suffix) \
  LIBXR_MSPM0_UART_CAT(dma_name, suffix)
#define LIBXR_MSPM0_UART_DMA_TRIGGER_IMPL(instance, direction) \
  DMA_##instance##_##direction##_TRIG
#define LIBXR_MSPM0_UART_DMA_TRIGGER(instance, direction) \
  LIBXR_MSPM0_UART_DMA_TRIGGER_IMPL(instance, direction)

/** @} */

/**
 * @brief 展开为 Main RX MSPM0UART 构造参数 / Expand to Main RX MSPM0UART constructor
 * arguments
 * @param name SysConfig UART 资源名，例如 `UART_0` / SysConfig UART resource name, such
 *             as `UART_0`
 * @param tx_dma BSP binding 中命名的 TX DMA 资源 / Named TX DMA resource in the BSP
 *               binding
 * @param tx_storage `MSPM0UARTTxBuffer<2N>` TX 存储对象 / `MSPM0UARTTxBuffer<2N>` TX
 *                   storage object
 * @param tx_queue_size 待发送记录队列深度 / Pending TX record queue depth
 * @param rx_queue_capacity 软件接收字节队列容量 / Software RX byte queue capacity
 * @pre 只能在 `SYSCFG_DL_init()` 初始化所选 UART 和 DMA 后构造 / Construct only after
 *      `SYSCFG_DL_init()` initializes the selected UART and DMA
 */
#define MSPM0_UART_MAIN_INIT(name, tx_dma, tx_storage, tx_queue_size, rx_queue_capacity) \
  ::LibXR::Detail::MakeMSPM0MainUartResources<                                           \
      name##_INST_INT_IRQN, static_cast<uint32_t>(LIBXR_MSPM0_UART_DMA_CHANNEL(tx_dma)), \
      LIBXR_MSPM0_UART_DMA_PROPERTY(tx_dma, _LIBXR_UART_IRQN),                           \
      static_cast<bool>(LIBXR_MSPM0_UART_DMA_PROPERTY(tx_dma, _LIBXR_UART_TX)),          \
      static_cast<bool>(LIBXR_MSPM0_UART_PROPERTY(name, _LIBXR_EXTEND_CAPABLE)),         \
      static_cast<bool>(LIBXR_MSPM0_DMA_DISPATCHER_AVAILABLE)>(                          \
      name##_INST, name##_INST_FREQUENCY,                                                \
      static_cast<uint8_t>(LIBXR_MSPM0_UART_DMA_TRIGGER(name##_INST, TX))),              \
      ::LibXR::Detail::MSPM0UARTTxStorage((tx_storage)), ::LibXR::RawData{},             \
      (tx_queue_size), (rx_queue_capacity),                                              \
      ::LibXR::MSPM0UART::BuildConfigFromSysCfg(name##_INST,                             \
                                                static_cast<uint32_t>(name##_BAUD_RATE))

/**
 * @brief 展开为 repeated full-channel DMA 的 Extend RX MSPM0UART 构造参数 / Expand to
 * Extend RX MSPM0UART constructor arguments using repeated full-channel DMA
 *
 * binding 会在编译期检查 UART 的 Extend 能力、RX full-channel 能力、可选 HALF Pre-IRQ
 * 能力和 TX/RX ownership。RX ring 的 service deadline、不可见整圈覆盖与 LIN partial
 * flush 语义见 `MSPM0UART`。 / The binding compile-time checks the UART Extend
 * capability, RX full-channel capability, optional HALF Pre-IRQ support, and TX/RX
 * ownership. See `MSPM0UART` for the RX-ring service deadline, invisible whole-wrap
 * boundary, and LIN partial-flush semantics.
 * @param name SysConfig UART 资源名，例如 `UART_0` / SysConfig UART resource name, such
 *             as `UART_0`
 * @param tx_dma BSP binding 中命名的 TX DMA 资源 / Named TX DMA resource in the BSP
 *               binding
 * @param rx_dma BSP binding 中命名的 full-channel RX DMA 资源 / Named full-channel RX
 *               DMA resource in the BSP binding
 * @param tx_storage `MSPM0UARTTxBuffer<2N>` TX 存储对象 / `MSPM0UARTTxBuffer<2N>` TX
 *                   storage object
 * @param tx_queue_size 待发送记录队列深度 / Pending TX record queue depth
 * @param rx_dma_storage 可写的一维 byte C array 或
 * `std::array`；必须非零、偶数字节且不超过 65535 字节 / Writable one-dimensional byte C
 * array or `std::array`; it must be nonzero, even-sized, and at most 65535 bytes
 * @param rx_queue_capacity 软件接收字节队列容量 / Software RX byte queue capacity
 * @pre 只能在 `SYSCFG_DL_init()` 初始化所选 UART 和 DMA 后构造 / Construct only after
 *      `SYSCFG_DL_init()` initializes the selected UART and DMA
 * @see MSPM0UART
 */
#define MSPM0_UART_EXTEND_INIT(name, tx_dma, rx_dma, tx_storage, tx_queue_size,          \
                               rx_dma_storage, rx_queue_capacity)                        \
  ::LibXR::Detail::MakeMSPM0ExtendUartResources<                                         \
      name##_INST_INT_IRQN, static_cast<uint32_t>(LIBXR_MSPM0_UART_DMA_CHANNEL(tx_dma)), \
      LIBXR_MSPM0_UART_DMA_PROPERTY(tx_dma, _LIBXR_UART_IRQN),                           \
      static_cast<bool>(LIBXR_MSPM0_UART_DMA_PROPERTY(tx_dma, _LIBXR_UART_TX)),          \
      static_cast<uint32_t>(LIBXR_MSPM0_UART_DMA_CHANNEL(rx_dma)),                       \
      LIBXR_MSPM0_UART_DMA_PROPERTY(rx_dma, _LIBXR_UART_IRQN),                           \
      static_cast<bool>(LIBXR_MSPM0_UART_DMA_PROPERTY(rx_dma, _LIBXR_UART_RX)),          \
      static_cast<bool>(LIBXR_MSPM0_UART_PROPERTY(name, _LIBXR_EXTEND_CAPABLE)),         \
      static_cast<bool>(LIBXR_MSPM0_UART_DMA_PROPERTY(rx_dma, _LIBXR_FULL_CHANNEL)),     \
      static_cast<bool>(LIBXR_MSPM0_UART_DMA_PROPERTY(rx_dma, _LIBXR_HALF_INTERRUPT)),   \
      static_cast<bool>(LIBXR_MSPM0_DMA_DISPATCHER_AVAILABLE)>(                          \
      name##_INST, name##_INST_FREQUENCY,                                                \
      static_cast<uint8_t>(LIBXR_MSPM0_UART_DMA_TRIGGER(name##_INST, TX)),               \
      static_cast<uint8_t>(LIBXR_MSPM0_UART_DMA_TRIGGER(name##_INST, RX))),              \
      ::LibXR::Detail::MSPM0UARTTxStorage((tx_storage)),                                 \
      ::LibXR::Detail::MSPM0UARTRxDmaStorage((rx_dma_storage)), (tx_queue_size),         \
      (rx_queue_capacity),                                                               \
      ::LibXR::MSPM0UART::BuildConfigFromSysCfg(name##_INST,                             \
                                                static_cast<uint32_t>(name##_BAUD_RATE))

}  // namespace LibXR
