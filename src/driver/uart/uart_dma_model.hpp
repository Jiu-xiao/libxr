#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

#include "double_buffer_storage.hpp"
#include "libxr_assert.hpp"
#include "libxr_mem.hpp"
#include "libxr_rw.hpp"
#include "uart.hpp"
#include "uart_rx_config_gate.hpp"

namespace LibXR
{

/**
 * @brief 后端单次 DMA start 的结果 / Result of one backend DMA-start attempt
 *
 * `STARTED` 将 active buffer 所有权交给硬件。`FAILED` 保证没有活跃硬件传输，且本次
 * 尝试以后不会发布 terminal callback；模型丢弃该已接纳 slot，并停止当前 generation
 * 进入 recovery。 / `STARTED` transfers the active buffer to hardware. `FAILED`
 * guarantees no live transfer and no later terminal callback from this attempt; the
 * model discards that accepted slot and stops the current generation for recovery.
 */
enum class UartDmaTxStartResult : uint32_t
{
  STARTED = 0U,
  FAILED = 1U,
};

/** @brief 非阻塞 control hook 的进度 / Progress of a non-blocking control hook. */
enum class UartDmaControlProgress : uint8_t
{
  COMPLETED = 0U,
  PENDING = 1U,
};

/**
 * @brief 已停止 TX generation 的权威 terminal 分类 / Authoritative terminal
 * classification for the stopped TX generation
 */
enum class UartOldTxTerminal : uint8_t
{
  NONE = 0U,
  COMPLETE = 1U,
  ERROR = 2U,
};

/**
 * @brief 后端推进 CONFIG 或 runtime recovery 的结果 / Result of advancing CONFIG or
 * runtime recovery
 *
 * `PENDING` 不携带 terminal 分类，并必须安排未来的 CONTROL_READY、COMPLETE
 * 或等价 service carrier。`COMPLETED` 是硬件静止的线性化点：分类已确定、破坏性清理已
 * 完成，且以后不会发布旧 terminal callback。模型在下一次硬件静止快照消费 COMPLETE；
 * NONE 和 ERROR 丢弃已经启动但无法证明完整发送的 active payload，未启动的 READY
 * payload 始终保留。 / `PENDING` carries no terminal
 * classification and must arrange a future service carrier. `COMPLETED` is the hardware
 * quiescence linearization point: classification and cleanup are final, and no old
 * terminal callback may appear. COMPLETE is consumed next; NONE and ERROR discard an
 * already-started active payload whose complete delivery is unproven. Unstarted READY
 * payload remains retained.
 */
struct UartDmaControlResult
{
  UartDmaControlProgress progress;    ///< control hook 进度 / Control-hook progress
  UartOldTxTerminal old_tx_terminal;  ///< 旧 TX 的 terminal 分类 / Old-TX terminal

  /** @brief 构造等待未来 carrier 的结果 / Construct a result awaiting a carrier. */
  [[nodiscard]] static constexpr UartDmaControlResult Pending()
  {
    return {UartDmaControlProgress::PENDING, UartOldTxTerminal::NONE};
  }

  /**
   * @brief 构造硬件已静止的结果 / Construct a hardware-quiescent result
   * @param terminal 已停止旧 TX 的最终分类 / Final classification of the stopped old TX
   * @return 已完成的 control result / Completed control result
   */
  [[nodiscard]] static constexpr UartDmaControlResult Completed(
      UartOldTxTerminal terminal = UartOldTxTerminal::NONE)
  {
    return {UartDmaControlProgress::COMPLETED, terminal};
  }

  /** @return control hook 已完成时为 true / True when the control hook completed. */
  [[nodiscard]] constexpr bool IsCompleted() const
  {
    return progress == UartDmaControlProgress::COMPLETED;
  }
};

/**
 * @brief 由同一 UART serialized service 消费的可合并事实 / Coalescible facts consumed
 * by one UART serialized service
 */
enum class UartDmaEvent : uint32_t
{
  WRITE = 1U << 0U,
  COMPLETE = 1U << 1U,
  ERROR = 1U << 2U,
  CONFIG = 1U << 3U,
  CONTROL_READY = 1U << 4U,
};

/**
 * @brief 串行化 UART DMA data path 与配置的通用模型 / Serialized UART DMA data-path
 * and configuration model
 *
 * WRITE、COMPLETE、ERROR、CONFIG 和异步 stop completion 进入同一个
 * `SerializedService`；只有其 owner 可以修改 TX 状态、从 WritePort 出队、启动 DMA、
 * 执行 recovery/configuration 或完成记录。 / All lifecycle facts enter one
 * `SerializedService`. Only its owner mutates TX state, dequeues WritePort, starts DMA,
 * advances control, or completes records.
 *
 * RX 字节仍在 service 外作为单一 SPSC producer。独立 RX callback 在确认 IRQ 后、首次
 * 读取 DMA 位置/descriptor 前调用 `ProcessRx()`；组合 IRQ scanner 使用
 * `ProcessRxInIrqSource()`，使 control wakeup 与同一快照中的旧 COMPLETE/ERROR 一起
 * 发布。未取得 gate 的 RX 片段可以丢弃，已取得 gate 的片段在 control 前完成。 / RX
 * remains a direct SPSC producer. Gate RX before its first DMA position/descriptor
 * access; a combined scanner publishes control wakeup with terminal facts from the same
 * snapshot. Rejected fragments may be dropped, while admitted fragments finish first.
 *
 * 每个 owner scope 构造一个 `WritePort::WriteQueue`，最多把 front 和 next 完整复制进
 * ACTIVE + optional READY block；该 ownership transfer 是唯一 operation completion
 * 点。提升时先发布 active 状态，再调用 `StartDmaTx()`。该调用返回前发生的 terminal
 * callback 只合并事件，并在 owner 提交 STARTED/FAILED 后处理。CONFIG/control 先于排队
 * TX 推进。 / Each owner scope creates one `WritePort::WriteQueue` and copies at most
 * front plus next into ACTIVE plus an optional READY block. That ownership transfer is
 * the sole operation-completion point. Promotion publishes active state before
 * `StartDmaTx()`, so an early terminal callback is deferred until STARTED/FAILED is
 * committed. CONFIG/control completes before queued TX progression.
 *
 * `StartDmaTx() == FAILED` 不撤销已经完成的 operation，而是丢弃该 slot、停止当前 backend
 * generation 并进入 recovery；不得 retry/replay。predictable unavailable 状态必须在
 * 接纳前由 TX/config gate 拦住。runtime ERROR 和 CONFIG 在硬件静止后同样丢弃无法证明完整
 * 发送的 active；未启动 READY 与公共队列数据继续保留。 / A start failure does not
 * revoke the completed operation. It discards that slot, stops the current backend
 * generation, and enters recovery without retry or replay. Predictable unavailability
 * must be rejected by the TX/config gate before acceptance. Runtime ERROR and CONFIG
 * likewise discard an active payload whose complete delivery is unproven, while an
 * unstarted READY slot and public queued data remain retained.
 *
 * `ValidateConfig()` 在 CONFIG reservation 和 owner acquisition 前运行，必须无副作用、
 * 支持 task/ISR 并发调用，且不得访问需要 owner 的 UART/DMA 状态。每条 PENDING 路径必须
 * 幂等并保证未来 carrier；`in_isr == true` 可达的 hook 必须 ISR-safe 且非阻塞。 / Config
 * validation runs before admission, is side-effect-free and concurrency-safe, and must
 * not touch owner-protected hardware. Every PENDING path is idempotent and guarantees a
 * future carrier; hooks reached from ISR are ISR-safe and non-blocking.
 *
 * `active_tx` 是 LibXR 软件所有权的权威事实；后端不得根据 DMA/UART 寄存器空闲状态
 * 反推记录所有权。模型会把已确定的 `in_isr` 传给每个 owner 侧硬件
 * hook，后端不得再次探测执行 上下文。 / `active_tx` is the authoritative LibXR
 * software-ownership fact; a backend must not infer record ownership from idle DMA/UART
 * registers. The model supplies the resolved `in_isr` value to each owner-side hardware
 * hook, so the backend must not rediscover execution context.
 *
 * Advance hook 返回 COMPLETED 后，模型发布 CONTROL_READY，并在下一硬件静止快照退休旧
 * generation。Complete hook 重启 RX；未启动的 READY TX 在 gate 释放后继续推进，旧
 * active 只在已证明 COMPLETE 时退休，不会从头重放未知前缀；
 * 该 continuation 不依赖后续 Write、IRQ 或外部 scheduler。 / COMPLETED schedules a
 * hardware-quiescent snapshot to retire the old generation. The Complete hook restarts
 * RX; unstarted READY TX progresses after the gate reopens, while an old active payload
 * is retired only when COMPLETE was proven and never replayed from an unknown prefix.
 * This continuation does not rely on an external carrier.
 *
 * recovery 的 Advance 尚 pending 时，CONFIG 可将其升级并复用同一次 stop；若
 * `CompleteRecovery()` 已开始，restart 已跨过线性化点，CONFIG 保留为下一事务。 /
 * CONFIG may upgrade a recovery whose Advance hook is pending. Once `CompleteRecovery()`
 * starts, restart has crossed its linearization point and CONFIG becomes the next
 * transaction.
 *
 * @tparam Backend 静态绑定的平台后端 / Statically bound platform backend
 * @tparam Policy 每实例的串行执行策略 / Per-instance serialized execution policy
 */
template <typename Backend, typename Policy>
class UartDmaModel
{
 public:
  /**
   * @brief 绑定后端、执行策略、写端口和双缓冲存储 / Bind backend, execution policy,
   * write port, and double-buffer storage
   * @param backend 生命周期必须覆盖本模型的平台后端 / Platform backend that must
   * outlive the model
   * @param policy 生命周期必须覆盖本模型的执行策略 / Execution policy that must
   * outlive the model
   * @param port 生命周期必须覆盖本模型的写端口 / Write port that must outlive the model
   * @param storage 两个等长 TX block 的连续存储；空写端口可使用空存储 / Contiguous
   * storage for two equal TX blocks; empty storage is allowed for a disabled write port
   */
  UartDmaModel(Backend& backend, Policy& policy, WritePort& port, RawData storage)
      : backend_(backend), policy_(policy), port_(port), buffers_(storage)
  {
    REQUIRE(port_.Capacity() <= buffers_.Size());

    // The first staged payload uses block 0, then promotion flips it to active.
    buffers_.SetActiveBlock(true);
  }

  /**
   * @brief 发布 WRITE doorbell / Publish a WRITE doorbell
   * @param in_isr 是否从 ISR 上下文调用 / Whether called from ISR context
   */
  void Submit(bool in_isr) { Invoke(UartDmaEvent::WRITE, in_isr); }

  /**
   * @brief 校验、保留、存储并发布一个完整 CONFIG transaction / Validate, reserve,
   * store, and publish one CONFIG transaction
   *
   * 校验先于所有 model admission；后端 `ValidateConfig()` 必须遵守类注释中的纯函数
   * 契约。 / Validation precedes all model admission; the backend validator follows the
   * side-effect-free contract in the class documentation.
   *
   * @param config 待应用的完整 UART 配置 / Complete UART configuration to apply
   * @param in_isr 是否从 ISR 上下文调用 / Whether called from ISR context
   * @return 接纳时为 `OK`，已有 CONFIG 时为 `BUSY`，否则为校验错误 / `OK` when
   * admitted, `BUSY` while CONFIG is outstanding, otherwise a validation error
   */
  ErrorCode SetConfig(UART::Configuration config, bool in_isr)
  {
    const ErrorCode validation = backend_.ValidateConfig(config);
    if (validation != ErrorCode::OK)
    {
      return validation;
    }
    if (!rx_config_gate_.TryReserveConfig())
    {
      return ErrorCode::BUSY;
    }

    requested_config_ = config;
    rx_config_gate_.PublishConfig();
    Invoke(UartDmaEvent::CONFIG, in_isr);
    return ErrorCode::OK;
  }

  /**
   * @brief 发布一次权威的整笔传输完成 / Publish one authoritative whole-transfer
   * completion
   *
   * 后端不得仅因部分 prefix 已排空而发布。control stop 期间，只有证明旧 DMA payload
   * 已完整完成且无 TX error 后才有效。COMPLETE 同时也是 control carrier。 / A backend
   * must not publish this for a partial prefix. During control stop it is valid only
   * after proving complete old-DMA delivery without TX error. COMPLETE is also a control
   * carrier.
   *
   * @param in_isr 是否从 ISR 上下文发布 / Whether published from ISR context
   */
  void OnTransferDone(bool in_isr) { Invoke(UartDmaEvent::COMPLETE, in_isr); }

  /**
   * @brief 发布一次权威 runtime UART/DMA error / Publish one authoritative runtime
   * UART/DMA error
   * @param in_isr 是否从 ISR 上下文发布 / Whether published from ISR context
   */
  void OnTransferError(bool in_isr) { Invoke(UartDmaEvent::ERROR, in_isr); }

  /**
   * @brief 取得完整 RX/CONFIG gate 后运行一个 RX 片段 / Run one RX fragment after
   * acquiring the complete RX/CONFIG gate
   *
   * 后端可在本调用前读清中断状态。`handler` 必须包含首次 DMA 位置/descriptor 访问和全部
   * 字节交付。被拒绝的片段按约定丢弃，同时为等待中的 control 提供 carrier。 / The
   * backend may acknowledge interrupt status first. `handler` contains the first DMA
   * position or descriptor access and all byte delivery. A rejected fragment is dropped
   * and carries waiting control work.
   *
   * @tparam Handler RX 片段处理器类型 / RX-fragment handler type
   * @param in_isr 是否从 ISR 上下文调用 / Whether called from ISR context
   * @param handler gate 内执行的 RX 硬件读取和字节搬运 / RX hardware access and byte
   * movement performed inside the gate
   * @return `handler` 已运行时为 true；CONFIG/recovery 关闭 RX admission 时为 false /
   * True when `handler` ran; false when CONFIG or recovery closed RX admission
   */
  template <typename Handler>
  bool ProcessRx(bool in_isr, Handler&& handler)
  {
    uint32_t events = 0U;
    const bool admitted = ProcessRxInIrqSource(events, std::forward<Handler>(handler));
    InvokeEvents(events, in_isr);
    return admitted;
  }

  /**
   * @brief 在 `InvokeIrq()` source 内 gate RX，且不嵌套 dispatch control / Gate RX
   * inside an `InvokeIrq()` source without nested control dispatch
   *
   * 所需 CONTROL_READY 会 OR 到 `source_events`。IRQ source 必须将该掩码与同一硬件
   * 快照中的全部 COMPLETE/ERROR 一起返回，避免 CONFIG 在 RX 处理与该快照后续 terminal
   * 之间退休旧 generation。 / CONTROL_READY is ORed into `source_events`, which the
   * IRQ source returns with all terminal facts from the same hardware snapshot. This
   * prevents CONFIG from retiring the old generation between those facts.
   *
   * @tparam Handler RX 片段处理器类型 / RX-fragment handler type
   * @param source_events 同一 IRQ 快照的输入/输出事件掩码 / Input/output event mask for
   * the same IRQ snapshot
   * @param handler gate 内执行的 RX 硬件读取和字节搬运 / RX hardware access and byte
   * movement performed inside the gate
   * @return `handler` 已运行时为 true，否则为 false / True when `handler` ran
   */
  template <typename Handler>
  bool ProcessRxInIrqSource(uint32_t& source_events, Handler&& handler)
  {
    ASSERT((source_events & ~ALL_EVENTS) == 0U);
    if (!rx_config_gate_.TryEnterRx())
    {
      source_events |= EventMask(UartDmaEvent::CONTROL_READY);
      return false;
    }

    Handler& handler_ref = handler;
    handler_ref();

    if (rx_config_gate_.LeaveRx())
    {
      source_events |= EventMask(UartDmaEvent::CONTROL_READY);
    }
    return true;
  }

  /**
   * @brief 发布真实的后端 stop-completion carrier / Publish a real backend
   * stop-completion carrier
   * @param in_isr 是否从 ISR 上下文发布 / Whether published from ISR context
   */
  void OnStopDone(bool in_isr) { Invoke(UartDmaEvent::CONTROL_READY, in_isr); }

  /**
   * @brief 在首次访问受保护状态前 admit 自有 raw IRQ / Admit an owned raw IRQ before
   * its first protected status access
   *
   * `source` 可读清硬件并返回 `UartDmaEvent`。其中任何 RX 位置/descriptor 消费都必须
   * 使用 `ProcessRxInIrqSource()`，使等待中的 control 加入同一返回快照。 / `source`
   * may acknowledge hardware and return events. RX position or descriptor consumption
   * inside it must use `ProcessRxInIrqSource()` so waiting control joins that snapshot.
   *
   * @tparam Source 首次读清受保护 IRQ 状态的可调用对象 / Callable performing the first
   * protected IRQ-status access
   * @param source raw IRQ source 操作 / Raw IRQ source operation
   * @param in_isr 是否从 ISR 上下文调用 / Whether called from ISR context
   * @return 本调用取得并释放 service owner 时为 true，否则为 false / True when this
   * call acquired and released the service owner; false otherwise
   */
  template <typename Source>
  bool InvokeIrq(Source&& source, bool in_isr)
  {
    return policy_.InvokeIrq(std::forward<Source>(source),
                             [this, in_isr](uint32_t events) noexcept
                             { return ServiceEvents(events, in_isr); });
  }

  /**
   * @brief 返回指定 TX storage block / Return a selected TX storage block
   * @param block block 索引，只允许 0 或 1 / Block index, either 0 or 1
   * @return DMA 可读 block 地址 / DMA-readable block address
   */
  [[nodiscard]] uint8_t* Buffer(int block) const { return buffers_.Buffer(block); }

  /**
   * @brief 将 UART DMA 事件转换为位掩码 / Convert a UART DMA event to its bit mask
   * @param event 单个事件值 / Single event value
   * @return 对应事件位 / Corresponding event bit
   */
  static constexpr uint32_t EventMask(UartDmaEvent event)
  {
    return static_cast<uint32_t>(event);
  }

 private:
  enum class ControlState : uint8_t
  {
    NORMAL = 0U,
    CONTROL_STOPPING = 1U,
    CONTROL_RESTARTING = 2U,
    CONTROL_COMPLETING = 3U,
  };

  enum class ControlIntent : uint8_t
  {
    RECOVERY = 0U,
    CONFIG = 1U,
  };

  /** Backend Advance hook that initiated the current stop, not an execution owner. */
  enum class ControlStopOrigin : uint8_t
  {
    RECOVERY = 0U,
    CONFIG = 1U,
  };

  enum class StartPendingResult : uint32_t
  {
    FAILED = 0U,
    STARTED = 1U,
  };

  static constexpr uint32_t ALL_EVENTS =
      EventMask(UartDmaEvent::WRITE) | EventMask(UartDmaEvent::COMPLETE) |
      EventMask(UartDmaEvent::ERROR) | EventMask(UartDmaEvent::CONFIG) |
      EventMask(UartDmaEvent::CONTROL_READY);

  static constexpr bool HasEvent(uint32_t events, UartDmaEvent event)
  {
    return (events & EventMask(event)) != 0U;
  }

  static constexpr bool IsControlCarrier(uint32_t events)
  {
    return HasEvent(events, UartDmaEvent::CONTROL_READY) ||
           HasEvent(events, UartDmaEvent::COMPLETE) ||
           HasEvent(events, UartDmaEvent::ERROR);
  }

  static uint32_t ControlContinuation(UartDmaControlResult result)
  {
    ASSERT(result.IsCompleted());
    uint32_t events = EventMask(UartDmaEvent::CONTROL_READY);
    if (result.old_tx_terminal == UartOldTxTerminal::COMPLETE)
    {
      events |= EventMask(UartDmaEvent::COMPLETE);
    }
    return events;
  }

  void Invoke(UartDmaEvent event, bool in_isr) { InvokeEvents(EventMask(event), in_isr); }

  void InvokeEvents(uint32_t events, bool in_isr)
  {
    if (events == 0U)
    {
      return;
    }
    ASSERT((events & ~ALL_EVENTS) == 0U);

    (void)policy_.Invoke(events, [this, in_isr](uint32_t snapshot) noexcept
                         { return ServiceEvents(snapshot, in_isr); });
  }

  uint32_t ServiceEvents(uint32_t events, bool in_isr) noexcept
  {
    ASSERT((events & ~ALL_EVENTS) == 0U);

    const bool write_seen = HasEvent(events, UartDmaEvent::WRITE);

    const bool complete_seen = HasEvent(events, UartDmaEvent::COMPLETE);
    if (complete_seen)
    {
      ClearActive();
    }

    const bool config_seen = HasEvent(events, UartDmaEvent::CONFIG);
    if (control_state_ == ControlState::CONTROL_STOPPING)
    {
      if (config_seen)
      {
        control_intent_ = ControlIntent::CONFIG;
      }
      if (config_seen || IsControlCarrier(events))
      {
        return AdvanceControl(in_isr);
      }
      return 0U;
    }

    if (control_state_ == ControlState::CONTROL_RESTARTING)
    {
      if (config_seen && control_intent_ == ControlIntent::RECOVERY)
      {
        control_intent_ = ControlIntent::CONFIG;
        control_stop_origin_ = ControlStopOrigin::CONFIG;
        control_state_ = ControlState::CONTROL_STOPPING;
        return AdvanceControl(in_isr);
      }
      return IsControlCarrier(events) ? CompleteControl(in_isr) : 0U;
    }

    if (control_state_ == ControlState::CONTROL_COMPLETING)
    {
      return IsControlCarrier(events) ? CompleteControl(in_isr) : 0U;
    }

    if (config_seen)
    {
      return BeginConfig(in_isr);
    }

    if (HasEvent(events, UartDmaEvent::ERROR))
    {
      return BeginRecovery(in_isr);
    }

    bool progress = write_seen;
    if (complete_seen)
    {
      progress = true;
    }

    return progress ? Progress(in_isr) : 0U;
  }

  uint32_t BeginConfig(bool in_isr)
  {
    control_state_ = ControlState::CONTROL_STOPPING;
    control_intent_ = ControlIntent::CONFIG;
    control_stop_origin_ = ControlStopOrigin::CONFIG;
    return AdvanceControl(in_isr);
  }

  uint32_t BeginRecovery(bool in_isr)
  {
    control_state_ = ControlState::CONTROL_STOPPING;
    control_intent_ = ControlIntent::RECOVERY;
    control_stop_origin_ = ControlStopOrigin::RECOVERY;
    return AdvanceControl(in_isr);
  }

  uint32_t AdvanceControl(bool in_isr)
  {
    const bool admitted = control_intent_ == ControlIntent::CONFIG
                              ? rx_config_gate_.TryEnterConfig()
                              : rx_config_gate_.TryEnterRecovery();
    if (!admitted)
    {
      return 0U;
    }

    const UartDmaControlResult result =
        control_stop_origin_ == ControlStopOrigin::CONFIG
            ? backend_.AdvanceConfig(requested_config_, active_length_ != 0U, in_isr)
            : backend_.AdvanceRecovery(active_length_ != 0U, in_isr);
    if (!result.IsCompleted())
    {
      return 0U;
    }

    if ((active_length_ != 0U) && (result.old_tx_terminal != UartOldTxTerminal::COMPLETE))
    {
      ClearActive();
    }

    if (control_intent_ == ControlIntent::CONFIG &&
        control_stop_origin_ == ControlStopOrigin::RECOVERY)
    {
      // Preserve the recovery hook's typed terminal as a service snapshot before the
      // CONFIG hook observes active_tx and applies the payload on the same quiescence.
      control_stop_origin_ = ControlStopOrigin::CONFIG;
      return ControlContinuation(result);
    }

    control_state_ = ControlState::CONTROL_RESTARTING;
    return ControlContinuation(result);
  }

  uint32_t CompleteControl(bool in_isr)
  {
    ASSERT(control_state_ == ControlState::CONTROL_RESTARTING ||
           control_state_ == ControlState::CONTROL_COMPLETING);
    control_state_ = ControlState::CONTROL_COMPLETING;
    if (control_intent_ == ControlIntent::CONFIG)
    {
      return backend_.CompleteConfig(in_isr) == UartDmaControlProgress::COMPLETED
                 ? FinishConfig(in_isr)
                 : 0U;
    }
    return backend_.CompleteRecovery(in_isr) == UartDmaControlProgress::COMPLETED
               ? FinishRecovery()
               : 0U;
  }

  uint32_t FinishRecovery()
  {
    control_state_ = ControlState::NORMAL;
    control_intent_ = ControlIntent::RECOVERY;
    control_stop_origin_ = ControlStopOrigin::RECOVERY;
    rx_config_gate_.LeaveRecovery();
    if (rx_config_gate_.ConfigRequested())
    {
      // CONFIG may have been consumed while CompleteRecovery() was pending. It becomes
      // a separate transaction only after the current recovery has fully completed. Its
      // normal FinishConfig() WRITE also resumes every retained READY block.
      return EventMask(UartDmaEvent::CONFIG);
    }

    // Recovery opens a fresh hardware generation. Always grant one bounded progress turn:
    // a discarded start-failure slot has no active/pending state and cannot replay, while
    // an already-published queue suffix may no longer have a WRITE carrier.
    return EventMask(UartDmaEvent::WRITE);
  }

  uint32_t FinishConfig(bool in_isr)
  {
    (void)in_isr;
    control_state_ = ControlState::NORMAL;
    control_intent_ = ControlIntent::RECOVERY;
    control_stop_origin_ = ControlStopOrigin::RECOVERY;
    rx_config_gate_.LeaveConfig();
    return EventMask(UartDmaEvent::WRITE);
  }

  uint32_t Progress(bool in_isr)
  {
    if ((active_length_ != 0U) && (pending_length_ != 0U))
    {
      return 0U;
    }
    if (!rx_config_gate_.TryEnterTx())
    {
      return 0U;
    }

    bool start_failed = false;
    {
      auto queue = port_.GetWriteQueue(in_isr);
      bool staged_front = false;

      if (active_length_ == 0U)
      {
        if ((pending_length_ == 0U) && (queue.front_size != 0U))
        {
          StageNextPending(queue, queue.front_size, in_isr);
          staged_front = true;
        }

        if (pending_length_ != 0U)
        {
          start_failed = TryStartPending(in_isr) == StartPendingResult::FAILED;
        }
      }

      if (!start_failed && (active_length_ != 0U) && (pending_length_ == 0U))
      {
        const size_t ready_size = staged_front ? queue.next_size : queue.front_size;
        if (ready_size != 0U)
        {
          StageNextPending(queue, ready_size, in_isr);
        }
      }
    }

    rx_config_gate_.LeaveTx();
    return start_failed ? BeginRecovery(in_isr) : 0U;
  }

  void StageNextPending(WritePort::WriteQueue& queue, size_t expected, bool in_isr)
  {
    ASSERT(pending_length_ == 0U);
    REQUIRE_FROM_CALLBACK(expected != 0U && expected <= buffers_.Size(), in_isr);
    const size_t accepted = queue.PopWithWriter(
        expected,
        [this, expected, in_isr](const uint8_t* first, size_t first_size,
                                 const uint8_t* second, size_t second_size) -> size_t
        {
          REQUIRE_FROM_CALLBACK(first_size + second_size == expected, in_isr);
          uint8_t* destination = buffers_.PendingBuffer();
          if (first_size != 0U)
          {
            Memory::FastCopy(destination, first, first_size);
          }
          if (second_size != 0U)
          {
            Memory::FastCopy(destination + first_size, second, second_size);
          }
          return expected;
        });
    REQUIRE_FROM_CALLBACK(accepted == expected, in_isr);

    // Complete READY state before this WriteQueue scope publishes operation callbacks.
    pending_length_ = expected;
  }

  StartPendingResult TryStartPending(bool in_isr)
  {
    ASSERT(pending_length_ != 0U);
    ASSERT(active_length_ == 0U);

    // Publish the complete active state before the backend can synchronously callback.
    const size_t length = pending_length_;
    buffers_.FlipActiveBlock();
    pending_length_ = 0U;
    active_length_ = length;

    const UartDmaTxStartResult result = backend_.StartDmaTx(
        buffers_.ActiveBuffer(), active_length_, buffers_.ActiveBlock(), in_isr);

    if (result == UartDmaTxStartResult::FAILED)
    {
      ClearActive();
      return StartPendingResult::FAILED;
    }

    return StartPendingResult::STARTED;
  }

  void ClearActive() { active_length_ = 0U; }

  Backend& backend_;
  Policy& policy_;
  WritePort& port_;
  DoubleBufferStorage buffers_;
  UartRxConfigGate rx_config_gate_;
  UART::Configuration requested_config_{};
  ControlState control_state_ = ControlState::NORMAL;
  ControlIntent control_intent_ = ControlIntent::RECOVERY;
  ControlStopOrigin control_stop_origin_ = ControlStopOrigin::RECOVERY;
  size_t active_length_ = 0U;
  size_t pending_length_ = 0U;
};

}  // namespace LibXR
