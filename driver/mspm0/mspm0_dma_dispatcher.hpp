#pragma once

#include <ti/driverlib/dl_dma.h>

#include <cstdint>

#include "libxr_def.hpp"

/**
 * @namespace LibXR::MSPM0DmaDispatcher
 * @brief MSPM0 共享 DMA 中断向量集成契约 / MSPM0 shared DMA-vector integration contract
 *
 * 默认模式提供唯一的 `DMA_IRQHandler`。首个 registration 出现时，仅在共享 IRQ 尚未使能
 * 时使能 NVIC，之后不再由本模块关闭。完整 BSP 中每个已使能的 DMA 原因都必须注册；未认领
 * 原因不会被清除，并会保持 pending。 / In default mode, this module provides the sole
 * `DMA_IRQHandler`. It enables the shared NVIC line after the first registration only
 * when the line was previously disabled and never disables it afterward. Every enabled
 * DMA cause in the complete BSP must be registered; an unclaimed cause is left pending.
 *
 * 定义 `LIBXR_MSPM0_DMA_EXTERNAL_IRQ_HANDLER` 的 BSP 必须在本分发器编译单元中使用相同
 * 定义，提供唯一的共享向量 handler，自行管理 NVIC，并在每次共享 DMA IRQ 中调用
 * `Dispatch()`。只为某个选定通道调用会使其他已注册 owner 的中断滞留。 / A BSP that
 * defines `LIBXR_MSPM0_DMA_EXTERNAL_IRQ_HANDLER` must define it consistently for this
 * dispatcher translation unit, provide the single shared-vector handler, manage its NVIC
 * state, and call `Dispatch()` on every shared DMA IRQ. Calling it only for one selected
 * channel can strand another registered owner.
 */
namespace LibXR::MSPM0DmaDispatcher
{

/** @brief DMA 逻辑事件位掩码 / Logical DMA event bit mask */
using EventMask = uint32_t;

/** @brief 单通道 owner 的逻辑事件 / Logical events for one channel owner */
enum Event : EventMask
{
  COMPLETE = 1U << 0U,  ///< 本通道传输完成 / This channel completed a transfer
  EARLY = 1U << 1U,     ///< 本 full channel 的 Pre-IRQ / This full channel's Pre-IRQ
  ERROR = 1U << 2U,     ///< 共享地址或数据错误 / Shared address or data error
};

/**
 * @brief 支持 DMA Pre-IRQ 的通道位图 / Bitmap of channels supporting DMA Pre-IRQ
 *
 * 仅低八位可映射到 MSPM0 DMA Pre-IRQ 状态位；自定义 BSP mask 不得设置更高位。 /
 * Only the low eight bits map to MSPM0 DMA Pre-IRQ status; a custom BSP mask must not set
 * higher bits.
 */
#if defined(LIBXR_MSPM0_DMA_EARLY_CHANNEL_MASK)
inline constexpr uint32_t EARLY_CHANNEL_MASK = LIBXR_MSPM0_DMA_EARLY_CHANNEL_MASK;
#elif defined(ti_devices_msp_m0p_mspm0g351x__include)
inline constexpr uint32_t EARLY_CHANNEL_MASK = 0x3FU;
#else
inline constexpr uint32_t EARLY_CHANNEL_MASK = 0U;
#endif

static_assert((EARLY_CHANNEL_MASK & ~0xFFU) == 0U,
              "MSPM0 DMA Pre-IRQ mask only supports channels 0-7");

/**
 * @brief 检查硬件通道是否实现 DMA Pre-IRQ / Check whether a channel implements DMA
 * Pre-IRQ
 * @param channel DMA 硬件通道编号 / DMA hardware channel number
 * @return 通道存在、编号小于八且 mask 中置位时返回 `true` / `true` when the channel
 *         exists, is below eight, and is set in the mask
 */
[[nodiscard]] constexpr bool EarlyInterruptSupported(uint8_t channel)
{
  return channel < DMA_SYS_N_DMA_CHANNEL && channel < 8U &&
         (EARLY_CHANNEL_MASK & (1UL << channel)) != 0U;
}

/**
 * @brief DMA 通道中断回调类型 / DMA channel interrupt callback type
 * @param context registration 保留的上下文，可为空 / Context retained by the
 *                registration; may be null
 * @param events 本轮合并的一个或多个逻辑事件 / One or more logical events coalesced for
 *               this pass
 * @warning 回调在可屏蔽中断关闭时同步执行；不得阻塞、重入分发器或修改 registration /
 *          The callback runs synchronously with maskable interrupts disabled; it must not
 *          block, reenter the dispatcher, or mutate registration state
 */
using Callback = void (*)(void* context, EventMask events);

class Registration;

/**
 * @brief 注册 DMA 通道的唯一逻辑 owner，但暂不使能中断原因 / Register one logical DMA
 * channel owner without enabling interrupt causes
 * @param channel DMA 硬件通道编号 / DMA hardware channel number
 * @param events 该 owner 后续可使能的逻辑事件 / Logical events this owner may enable
 * @param callback 在屏蔽可屏蔽中断时执行的有界 ISR 回调；不得修改或递归调用本分发器 /
 *                 Bounded ISR callback invoked with maskable interrupts disabled; it
 *                 must not mutate or recursively dispatch this dispatcher
 * @param context 传给回调的不透明上下文；非空时其目标必须与通道 owner 具有相同的程序
 *                生命周期 / Opaque callback context; when non-null, its target must
 *                share the program lifetime of the channel owner
 * @param out 成功时写入的空 registration token / Empty registration token populated on
 *            success
 * @return 成功时返回 `OK`；事件或回调无效时返回 `ARG_ERR`；通道越界时返回
 *         `OUT_OF_RANGE`；通道不支持 EARLY 时返回 `NOT_SUPPORT`；`out` 已有效时返回
 *         `STATE_ERR`；通道已有 owner 时返回 `BUSY` / `OK` on success; `ARG_ERR` for
 *         invalid events or callback; `OUT_OF_RANGE` for an invalid channel;
 *         `NOT_SUPPORT` when EARLY is unavailable; `STATE_ERR` when `out` is already
 *         valid; `BUSY` when the channel already has an owner
 * @note registration 在程序生命周期内永久占有通道；调用方必须保留 token 以控制事件使能 /
 *       The registration owns the channel for the program lifetime; the caller must
 *       retain the token to control event enables
 */
ErrorCode Register(uint8_t channel, EventMask events, Callback callback, void* context,
                   Registration& out);

/**
 * @brief 使能或禁用有效 registration 已订阅的事件 / Enable or disable subscribed events
 * for a live registration
 * @param registration 有效的通道 registration token / Live channel registration token
 * @param events 非零且必须是 registration 已订阅事件的子集 / Nonzero subset of events
 *               subscribed by the registration
 * @param enabled `true` 表示使能，`false` 表示禁用 / `true` to enable, `false` to disable
 * @return 成功或目标状态已满足时返回 `OK`；事件无效或未订阅时返回 `ARG_ERR`；token
 *         无效时返回 `STATE_ERR` / `OK` on success or when the requested state already
 *         holds; `ARG_ERR` for invalid or unsubscribed events; `STATE_ERR` for an invalid
 *         token
 * @note 禁用事件前，调用方必须先静止对应 DMA producer / The caller must quiesce the
 *       corresponding DMA producer before disabling events
 */
ErrorCode SetEnabled(const Registration& registration, EventMask events, bool enabled);

/**
 * @brief 分发当前已使能且归本分发器所有的 DMA 原因 / Dispatch enabled dispatcher-owned
 * DMA causes
 * @note 必须从共享 DMA IRQ 路径调用；回调执行期间保持可屏蔽中断关闭 / Call from the
 *       shared DMA IRQ path; maskable interrupts remain disabled while callbacks run
 * @note 每次调用最多 drain 四轮；超过上限的已认领原因保持 pending 并由诊断计数 /
 *       Each call drains at most four passes; owned causes beyond that bound remain
 *       pending and increment the diagnostic count
 * @note 共享 ERROR 会交给每个已使能 ERROR 的 registration；未认领原因只记录、不清除 /
 *       A shared ERROR is delivered to every registration with ERROR enabled; unclaimed
 *       causes are recorded but not cleared
 */
void Dispatch();

/**
 * @brief 取得硬件通道的原始完成位 / Get the raw completion bit for a hardware channel
 * @param channel DMA 硬件通道编号 / DMA hardware channel number
 * @return 对应原始状态位；通道超出可编码范围时返回零 / Matching raw status bit, or zero
 *         when the channel cannot be encoded
 */
[[nodiscard]] constexpr uint32_t CompleteMask(uint8_t channel)
{
  return channel < 16U ? (1UL << channel) : 0U;
}

/**
 * @brief 取得硬件通道的原始 Pre-IRQ 位 / Get the raw Pre-IRQ bit for a hardware channel
 * @param channel DMA 硬件通道编号 / DMA hardware channel number
 * @return 对应原始状态位；通道超出可编码范围时返回零 / Matching raw status bit, or zero
 *         when the channel cannot be encoded
 */
[[nodiscard]] constexpr uint32_t EarlyMask(uint8_t channel)
{
  return channel < 8U ? (1UL << (16U + channel)) : 0U;
}

/**
 * @brief 取得共享地址和数据错误的原始掩码 / Get the shared raw address/data error mask
 * @return 共享 DMA 错误状态位 / Shared DMA error status bits
 */
[[nodiscard]] constexpr uint32_t ErrorMask()
{
  return DL_DMA_INTERRUPT_ADDR_ERROR | DL_DMA_INTERRUPT_DATA_ERROR;
}

/**
 * @name 分发器诊断 / Dispatcher diagnostics
 *
 * getter 分别返回 relaxed 单项快照，不构成跨字段一致快照；计数器不提供 reset，为不饱和
 * `uint32_t`，会自然回绕。 / Each getter returns an independent relaxed snapshot, not a
 * consistent multi-field snapshot. Counters have no reset operation, are non-saturating
 * `uint32_t` values, and wrap naturally.
 * @{
 */

/**
 * @brief 取得最近一次分发观察到的未认领已使能原因 / Get the latest unclaimed enabled
 * causes
 * @return 最近一次 `Dispatch()` 观察到的原始原因掩码；尚未观察时为零 / Raw cause mask
 *         observed by the latest `Dispatch()`; zero before the first observation
 */
[[nodiscard]] uint32_t GetLastUnclaimedMask();

/**
 * @brief 取得观察到未认领原因的分发次数 / Get the dispatch count with unclaimed causes
 * @return 至少包含一个未认领已使能原因的累计分发次数 / Number of dispatches that observed
 *         at least one unclaimed enabled cause
 */
[[nodiscard]] uint32_t GetUnclaimedCount();

/**
 * @brief 取得耗尽四轮 drain 上限的分发次数 / Get the four-pass drain-limit count
 * @return 四轮后仍有已认领原因 pending 的累计次数 / Number of dispatches that still had
 * an owned cause pending after four passes
 */
[[nodiscard]] uint32_t GetDrainLimitCount();

/** @} */

/**
 * @brief 单个通道 registration 的不可复制能力 token / Non-copyable channel token
 *
 * 默认构造的 token 无效；`Register()` 成功后永久绑定通道。调用方必须让 token 与通道
 * owner 具有相同的程序生命周期。 / A default-constructed token is invalid;
 * `Register()` permanently binds it to a channel. The token must share the program
 * lifetime of the channel owner.
 */
class Registration
{
 public:
  Registration() = default;
  Registration(const Registration&) = delete;
  Registration& operator=(const Registration&) = delete;
  Registration(Registration&&) = delete;
  Registration& operator=(Registration&&) = delete;

 private:
  uint8_t channel_ = 0xFFU;

  friend ErrorCode Register(uint8_t channel, EventMask events, Callback callback,
                            void* context, Registration& out);
  friend ErrorCode SetEnabled(const Registration& registration, EventMask events,
                              bool enabled);
};

}  // namespace LibXR::MSPM0DmaDispatcher
