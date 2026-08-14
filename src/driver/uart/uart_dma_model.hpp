#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

#include "double_buffer_storage.hpp"
#include "libxr_assert.hpp"
#include "libxr_rw.hpp"
#include "uart.hpp"
#include "uart_rx_config_gate.hpp"

namespace LibXR
{

/**
 * @brief 后端单次 DMA start 的结果 / Result of one backend DMA-start attempt
 *
 * `STARTED` 将 active buffer 所有权交给硬件。`FAILED` 保证没有活跃硬件传输，且本次
 * 尝试以后不会发布 terminal callback，模型可立即复用该 block。 / `STARTED`
 * transfers the active buffer to hardware. `FAILED` guarantees no live transfer and no
 * later terminal callback from this attempt, so the block may be reused immediately.
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
 * NONE 和 ERROR 保留 active payload 以便重启。 / `PENDING` carries no terminal
 * classification and must arrange a future service carrier. `COMPLETED` is the hardware
 * quiescence linearization point: classification and cleanup are final, and no old
 * terminal callback may appear. COMPLETE is consumed next; NONE and ERROR retain active
 * payload for restart.
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
 * pending payload 从公共字节队列复制，metadata 仍留在队首；提升时先发布 active 状态，
 * 再调用 `StartDmaTx()`。该调用返回前发生的 terminal callback 只合并事件，并在 owner
 * 提交 STARTED/FAILED 后处理。owner 观察到 CONFIG 时放弃栈上同步完成捷径，保留记录
 * 通过其持久 Operation 完成。 / Pending data is copied while metadata remains queued.
 * Promotion publishes active state before `StartDmaTx()`, so an early terminal callback
 * is deferred until STARTED/FAILED is committed. CONFIG abandons stack-local synchronous
 * completion; preserved records complete through their durable Operation.
 *
 * `StartDmaTx() == FAILED` 只影响当前记录，不请求 CONFIG。runtime ERROR 和 CONFIG
 * 都在硬件静止后保留 active、pending 与公共队列记录；存在 active 时从 byte 0 重启，
 * 然后恢复队列推进。 / A start failure is record-local. Runtime ERROR and CONFIG retain
 * active, pending, and queued records across quiescence, restart active from byte zero
 * when present, and resume queued work.
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
 * generation。Complete hook 重启 RX 和保留的 active TX，之后才释放 RX/config gate；
 * 该 continuation 不依赖后续 Write、IRQ 或外部 scheduler。 / COMPLETED schedules a
 * hardware-quiescent snapshot to retire the old generation. The Complete hook restarts
 * RX and retained active TX before reopening the gate, without relying on an external
 * carrier.
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
    REQUIRE(port_.QueueInfo() != nullptr);
    if (port_.QueueData() == nullptr)
    {
      REQUIRE(buffers_.Size() == 0U);
    }
    else
    {
      REQUIRE(port_.QueueData()->MaxSize() <= buffers_.Size());
    }

    // The first staged payload uses block 0, then promotion flips it to active.
    buffers_.SetActiveBlock(true);
  }

  /**
   * @brief 发布 WRITE，并在可判定时返回本调用的同步结果 / Publish WRITE and return
   * this call's synchronous result when identifiable
   * @param in_isr 是否从 ISR 上下文调用 / Whether called from ISR context
   * @return 当前记录的同步提交结果；异步接管时为 `PENDING` / Synchronous submission
   * result for this record, or `PENDING` after asynchronous handoff
   */
  ErrorCode Submit(bool in_isr)
  {
    SubmitContext context{};
    (void)policy_.Invoke(EventMask(UartDmaEvent::WRITE),
                         [this, in_isr, &context](uint32_t events) noexcept
                         { return ServiceEvents(events, in_isr, &context); });
    return context.result_;
  }

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
                             { return ServiceEvents(events, in_isr, nullptr); });
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
    BLOCKED = 0U,
    FAILED = 1U,
    STARTED = 2U,
  };

  struct SubmitContext
  {
    ErrorCode result_ = ErrorCode::PENDING;
    bool resolved_ = false;
    bool synchronous_completion_allowed_ = true;
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
                         { return ServiceEvents(snapshot, in_isr, nullptr); });
  }

  uint32_t ServiceEvents(uint32_t events, bool in_isr, SubmitContext* submit) noexcept
  {
    ASSERT((events & ~ALL_EVENTS) == 0U);

    const bool complete_seen = HasEvent(events, UartDmaEvent::COMPLETE);
    if (complete_seen)
    {
      (void)ReleaseActive();
    }

    const bool config_seen = HasEvent(events, UartDmaEvent::CONFIG);
    if (control_state_ == ControlState::CONTROL_STOPPING)
    {
      if (config_seen)
      {
        DisableSynchronousCompletion(submit);
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
        DisableSynchronousCompletion(submit);
        control_intent_ = ControlIntent::CONFIG;
        control_stop_origin_ = ControlStopOrigin::CONFIG;
        control_state_ = ControlState::CONTROL_STOPPING;
        return AdvanceControl(in_isr);
      }
      return IsControlCarrier(events) ? CompleteControl(in_isr) : 0U;
    }

    if (control_state_ == ControlState::CONTROL_COMPLETING)
    {
      if (config_seen)
      {
        DisableSynchronousCompletion(submit);
      }
      return IsControlCarrier(events) ? CompleteControl(in_isr) : 0U;
    }

    if (config_seen)
    {
      return BeginConfig(in_isr, submit);
    }

    if (HasEvent(events, UartDmaEvent::ERROR))
    {
      return BeginRecovery(in_isr);
    }

    bool progress = HasEvent(events, UartDmaEvent::WRITE);
    if (complete_seen)
    {
      progress = true;
    }

    return progress ? Progress(in_isr, submit) : 0U;
  }

  uint32_t BeginConfig(bool in_isr, SubmitContext* submit)
  {
    DisableSynchronousCompletion(submit);
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
               ? FinishRecovery(in_isr)
               : 0U;
  }

  uint32_t FinishRecovery(bool in_isr)
  {
    RestartActiveAfterRecovery(in_isr);
    control_state_ = ControlState::NORMAL;
    control_intent_ = ControlIntent::RECOVERY;
    control_stop_origin_ = ControlStopOrigin::RECOVERY;
    rx_config_gate_.LeaveRecovery();
    uint32_t events = EventMask(UartDmaEvent::WRITE);
    if (rx_config_gate_.ConfigRequested())
    {
      // CONFIG may have been consumed while CompleteRecovery() was pending. It becomes
      // a separate transaction only after the current recovery has fully completed.
      events |= EventMask(UartDmaEvent::CONFIG);
    }
    return events;
  }

  uint32_t FinishConfig(bool in_isr)
  {
    RestartActiveDuringConfig(in_isr);
    control_state_ = ControlState::NORMAL;
    control_intent_ = ControlIntent::RECOVERY;
    control_stop_origin_ = ControlStopOrigin::RECOVERY;
    rx_config_gate_.LeaveConfig();
    return EventMask(UartDmaEvent::WRITE);
  }

  uint32_t Progress(bool in_isr, SubmitContext* submit)
  {
    if (active_length_ == 0U)
    {
      if (!pending_valid_)
      {
        if (!StageNextPending(in_isr))
        {
          return 0U;
        }
      }

      const StartPendingResult start = TryStartPending(in_isr, submit);
      if (start == StartPendingResult::BLOCKED)
      {
        return 0U;
      }
      if (start == StartPendingResult::FAILED)
      {
        // Re-enter through the service snapshot boundary so CONFIG/ERROR published by
        // the failed record's callback keep their priority over the next queued start.
        return EventMask(UartDmaEvent::WRITE);
      }
    }

    if ((active_length_ != 0U) && !pending_valid_)
    {
      (void)StageNextPending(in_isr);
    }
    return 0U;
  }

  bool StageNextPending(bool in_isr)
  {
    if (pending_valid_)
    {
      return true;
    }
    if (!rx_config_gate_.TryEnterTx())
    {
      return false;
    }

    WriteInfoBlock info{};
    if (port_.QueueInfo()->Peek(info) != ErrorCode::OK)
    {
      rx_config_gate_.LeaveTx();
      return false;
    }

    ASSERT(port_.QueueData() != nullptr);
    if (port_.QueueData() == nullptr)
    {
      rx_config_gate_.LeaveTx();
      return false;
    }

    REQUIRE_FROM_CALLBACK(info.data.size_ <= buffers_.Size(), in_isr);
    auto dequeue = port_.BeginDequeue(in_isr);
    const ErrorCode result = dequeue.PopData(buffers_.PendingBuffer(), info.data.size_);
    ASSERT(result == ErrorCode::OK);
    if (result != ErrorCode::OK)
    {
      rx_config_gate_.LeaveTx();
      return false;
    }

    // Payload and metadata length are complete before pending becomes visible.
    pending_valid_ = true;
    rx_config_gate_.LeaveTx();
    return true;
  }

  StartPendingResult TryStartPending(bool in_isr, SubmitContext* submit)
  {
    ASSERT(pending_valid_);
    ASSERT(active_length_ == 0U);
    if (!rx_config_gate_.TryEnterTx())
    {
      return StartPendingResult::BLOCKED;
    }

    WriteInfoBlock info{};
    if (port_.QueueInfo()->Peek(info) != ErrorCode::OK)
    {
      ASSERT(false);
      rx_config_gate_.LeaveTx();
      return StartPendingResult::FAILED;
    }

    const bool synchronous_submission =
        (submit != nullptr) && submit->synchronous_completion_allowed_ &&
        !submit->resolved_ && (port_.QueueInfo()->Size() == 1U);

    // Publish the complete active state before the backend can synchronously callback.
    buffers_.FlipActiveBlock();
    pending_valid_ = false;
    active_length_ = info.data.size_;

    const UartDmaTxStartResult result = backend_.StartDmaTx(
        buffers_.ActiveBuffer(), active_length_, buffers_.ActiveBlock(), in_isr);

    {
      auto dequeue = port_.BeginDequeue(in_isr);
      const ErrorCode pop_result = dequeue.PopInfo(info);
      ASSERT(pop_result == ErrorCode::OK);
      if (pop_result != ErrorCode::OK)
      {
        ASSERT(false);
        ClearActive();
        rx_config_gate_.LeaveTx();
        return StartPendingResult::FAILED;
      }

      if (result == UartDmaTxStartResult::FAILED)
      {
        ClearActive();
        buffers_.FlipActiveBlock();
      }
      rx_config_gate_.LeaveTx();
    }

    if (result == UartDmaTxStartResult::FAILED)
    {
      CompleteRecord(in_isr, ErrorCode::FAILED, info, synchronous_submission, submit);
      return StartPendingResult::FAILED;
    }

    CompleteRecord(in_isr, ErrorCode::OK, info, synchronous_submission, submit);
    return StartPendingResult::STARTED;
  }

  void CompleteRecord(bool in_isr, ErrorCode result, WriteInfoBlock& info,
                      bool synchronous_submission, SubmitContext* submit)
  {
    if (synchronous_submission)
    {
      submit->result_ = result;
      submit->resolved_ = true;
      return;
    }
    port_.Finish(in_isr, result, info);
  }

  static void DisableSynchronousCompletion(SubmitContext* submit)
  {
    if (submit != nullptr)
    {
      submit->synchronous_completion_allowed_ = false;
    }
  }

  bool ReleaseActive()
  {
    if (active_length_ == 0U)
    {
      return false;
    }
    ClearActive();
    return true;
  }

  void ClearActive() { active_length_ = 0U; }

  void RestartActiveDuringConfig(bool in_isr)
  {
    if (active_length_ == 0U)
    {
      return;
    }

    const UartDmaTxStartResult result = backend_.StartDmaTx(
        buffers_.ActiveBuffer(), active_length_, buffers_.ActiveBlock(), in_isr);
    REQUIRE_FROM_CALLBACK(result == UartDmaTxStartResult::STARTED, in_isr);
  }

  void RestartActiveAfterRecovery(bool in_isr)
  {
    if (active_length_ == 0U)
    {
      return;
    }
    if (!rx_config_gate_.TryEnterTx())
    {
      // A concurrently reserved CONFIG won the TX admission boundary. Its publication
      // is the guaranteed carrier; the retained active payload stays stopped for it.
      ASSERT(rx_config_gate_.ConfigRequested());
      return;
    }

    const UartDmaTxStartResult result = backend_.StartDmaTx(
        buffers_.ActiveBuffer(), active_length_, buffers_.ActiveBlock(), in_isr);
    rx_config_gate_.LeaveTx();
    REQUIRE_FROM_CALLBACK(result == UartDmaTxStartResult::STARTED, in_isr);
  }

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
  bool pending_valid_ = false;
};

}  // namespace LibXR
