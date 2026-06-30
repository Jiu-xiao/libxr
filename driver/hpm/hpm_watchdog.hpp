#pragma once

/**
 * @file hpm_watchdog.hpp
 * @brief HPM EWDG 看门狗适配头文件 / Adapter header for the HPM EWDG watchdog.
 *
 * @details
 * 本文件在 HPM SDK `hpm_ewdg_drv` 之上实现 LibXR `Watchdog` 抽象。当前后端只覆盖
 * EWDG：配置阶段使用 `ewdg_get_default_config()` 和 `ewdg_init()` 写入低层
 * tick/div 超时复位策略，`Start()` 使能并刷新，`Feed()` 调用 `ewdg_refresh()`，
 * `Stop()` 调用 `ewdg_disable()`。超时范围按 `EWDG_SOC_OVERTIME_REG_WIDTH`、
 * `EWDG_SOC_CLK_DIV_VAL_MAX` 和实际分频寄存器宽度校验，板级复位行为和真实超时时间
 * 仍需上板确认。
 *
 * This file implements the LibXR `Watchdog` abstraction on top of the HPM SDK
 * `hpm_ewdg_drv` APIs. The current backend covers EWDG only: configuration uses
 * `ewdg_get_default_config()` and `ewdg_init()` to program low-level tick/div
 * timeout-reset policy,
 * `Start()` enables and refreshes the watchdog, `Feed()` calls `ewdg_refresh()`,
 * and `Stop()` calls `ewdg_disable()`. Timeout range checks use
 * `EWDG_SOC_OVERTIME_REG_WIDTH`, `EWDG_SOC_CLK_DIV_VAL_MAX`, and the real divider
 * register width; board-level reset behavior and real timeout timing still require
 * hardware validation.
 */

#include <cstdint>

#include "hpm_clock_drv.h"
#include "hpm_soc.h"
#include "watchdog.hpp"

#if (defined(HPMSOC_HAS_HPMSDK_EWDG) || defined(HPM_EWDG0) || defined(HPM_PEWDG)) && \
    __has_include("hpm_ewdg_drv.h")
#include "hpm_ewdg_drv.h"
#define LIBXR_HPM_EWDG_SUPPORTED 1
using LibXRHpmEwdgType = EWDG_Type;
#else
#define LIBXR_HPM_EWDG_SUPPORTED 0
using LibXRHpmEwdgType = void;
#endif

namespace LibXR
{

/**
 * @class HPMWatchdog
 * @brief HPM EWDG 看门狗适配器 / HPM EWDG watchdog adapter for the LibXR Watchdog
 * interface.
 *
 * @details
 * 当 HPM SDK 暴露 `HPMSOC_HAS_HPMSDK_EWDG` 且可以包含 `hpm_ewdg_drv.h`
 * 时，本驱动通过 SDK EWDG 接口完成初始化、启动、喂狗和停止；否则接口保留，
 * 有效配置和启停/喂狗操作返回 `ErrorCode::NOT_SUPPORT`。`SetConfig()` 仍会先执行
 * LibXR 参数校验，因此非法 `timeout_ms`/`feed_ms` 会返回参数错误。默认实例为
 * `HPM_EWDG0`，默认时钟为 `clock_watchdog0`，自动选择 32 kHz 看门狗时钟源。
 *
 * When the HPM SDK exposes `HPMSOC_HAS_HPMSDK_EWDG` and `hpm_ewdg_drv.h` is
 * available, this driver initializes, starts, feeds, and stops the watchdog
 * through the SDK EWDG API. Otherwise the class remains available and valid
 * configuration plus start/feed/stop operations return `ErrorCode::NOT_SUPPORT`.
 * `SetConfig()` still performs LibXR argument validation first, so invalid
 * `timeout_ms`/`feed_ms` values return argument errors. The default instance is
 * `HPM_EWDG0` with `clock_watchdog0`, using the 32 kHz watchdog clock source by
 * default.
 */
class HPMWatchdog final : public Watchdog
{
 public:
  /**
   * @brief 自动选择 EWDG 时钟源的哨兵值。
   *        Sentinel value for automatic EWDG clock source selection.
   */
#if defined(CLK_SRC_GROUP_INVALID) && defined(MAKE_CLK_SRC)
  static constexpr clk_src_t kAutoClockSource =
      static_cast<clk_src_t>(MAKE_CLK_SRC(CLK_SRC_GROUP_INVALID, 0x0Fu));
#else
  static constexpr clk_src_t kAutoClockSource = static_cast<clk_src_t>(0xFFu);
#endif

#if LIBXR_HPM_EWDG_SUPPORTED && defined(HPM_EWDG0)
  /** @brief Construct an HPM EWDG watchdog and optionally start it. */
  explicit HPMWatchdog(LibXRHpmEwdgType* ewdg = HPM_EWDG0,
                       clock_name_t clock = clock_watchdog0, uint32_t timeout_ms = 1000,
                       uint32_t feed_ms = 250, clk_src_t clock_source = kAutoClockSource,
                       bool auto_start = true);
#else
  /**
   * @brief 构造未启用 EWDG 后端的占位看门狗。
   *        Construct a placeholder watchdog when the EWDG backend is unavailable.
   *
   * @details
   * 该构造函数保留统一类型接口；有效配置、启动、喂狗和停止操作会返回
   * `ErrorCode::NOT_SUPPORT`。`SetConfig()` 仍会先做 `timeout_ms`/`feed_ms`
   * 参数校验。
   *
   * This constructor preserves the common type interface; valid configuration,
   * start, feed, and stop operations return `ErrorCode::NOT_SUPPORT`.
   * `SetConfig()` still validates `timeout_ms`/`feed_ms` first.
   *
   * @param ewdg 占位 EWDG 指针；后端不可用时不会解引用。
   *             Placeholder EWDG pointer; it is not dereferenced when unsupported.
   * @param clock 占位时钟名；后端不可用时不会使用。
   *              Placeholder clock name; it is not used when unsupported.
   * @param timeout_ms 初始超时配置，仅保存到 LibXR 配置路径。
   *                   Initial timeout configuration, only saved through the LibXR path.
   * @param feed_ms 初始自动喂狗周期，仅保存到 LibXR 配置路径。
   *                Initial auto-feed interval, only saved through the LibXR path.
   * @param clock_source 占位时钟源；后端不可用时不会使用。
   *                     Placeholder clock source; it is not used when unsupported.
   * @param auto_start 占位自动启动标志；后端不可用时不会启动硬件。
   *                   Placeholder auto-start flag; no hardware is started when
   * unsupported.
   */
  explicit HPMWatchdog(LibXRHpmEwdgType* ewdg = nullptr,
                       clock_name_t clock = static_cast<clock_name_t>(0),
                       uint32_t timeout_ms = 1000, uint32_t feed_ms = 250,
                       clk_src_t clock_source = kAutoClockSource, bool auto_start = true);
#endif

  /**
   * @brief 设置超时和自动喂狗周期，并初始化 EWDG 配置。
   *        Set timeout/feed intervals and initialize the EWDG configuration.
   *
   * @param config 看门狗配置，`timeout_ms` 和 `feed_ms` 必须在硬件范围内。
   *               Watchdog configuration; `timeout_ms` and `feed_ms` must fit
   *               the hardware range.
   * @return `OK` 表示配置成功；参数非法、超出硬件范围或后端不可用时返回对应错误码。
   *         `OK` on success; otherwise an error for invalid arguments, hardware
   *         range overflow, or unavailable backend.
   */
  ErrorCode SetConfig(const Configuration& config) override;

  /**
   * @brief 立即喂狗。
   *        Feed the watchdog immediately.
   *
   * @return 已启动时返回 `OK`；未启动、指针为空或后端不可用时返回对应错误码。
   *         `OK` when started; otherwise an error for not started, null pointer,
   *         or unavailable backend.
   */
  ErrorCode Feed() override;

  /**
   * @brief 启动 EWDG 并执行一次喂狗。
   *        Start EWDG and feed it once.
   *
   * @return 操作结果错误码。
   *         Operation result error code.
   */
  ErrorCode Start() override;

  /**
   * @brief 停止 EWDG。
   *        Stop EWDG.
   *
   * @return 操作结果错误码；后端不可用时返回 `NOT_SUPPORT`。
   *         Operation result error code; returns `NOT_SUPPORT` when unavailable.
   */
  ErrorCode Stop() override;

 private:
  /**
   * @brief 将 HPM SDK 状态码转换为 LibXR 错误码。
   *        Convert an HPM SDK status code to a LibXR error code.
   */
  static ErrorCode ConvertStatus(hpm_stat_t status);

#if LIBXR_HPM_EWDG_SUPPORTED
  /**
   * @brief Ensure the EWDG clock gate/source are ready and resolve frequency.
   */
  ErrorCode EnsureClockReady();

  /**
   * @brief 解析可写入 EWDG 的超时 tick 和分频值。
   *        Resolve writable EWDG timeout ticks and divider value.
   */
  ErrorCode ResolveTimeoutSetting(uint32_t timeout_ms, uint32_t counter_clock_hz,
                                  uint32_t* timeout_ticks,
                                  uint32_t* clock_div_power) const;

  /**
   * @brief 将当前配置写入 EWDG SDK。
   *        Apply the current configuration through the EWDG SDK.
   */
  ErrorCode ApplyConfiguration(const Configuration& config, bool enable_watchdog);

  /**
   * @brief 解析自动或用户指定的 EWDG 时钟源。
   *        Resolve the automatic or user-specified EWDG clock source.
   */
  clk_src_t ResolveClockSource() const;

  /**
   * @brief 根据时钟源选择 EWDG 内部计数时钟类型。
   *        Select the EWDG internal counter clock type from the clock source.
   */
  ewdg_cnt_clk_sel_t ResolveEwdgClockSelect() const;

  /**
   * @brief 根据 SDK 时钟源解析 EWDG 计数器频率。
   *        Resolve the EWDG counter frequency from an SDK clock source.
   */
  ErrorCode ResolveCounterClockFrequency(clk_src_t source, uint32_t* frequency_hz) const;
#endif

  LibXRHpmEwdgType* ewdg_;         ///< EWDG 寄存器基地址 / EWDG register base.
  clock_name_t clock_;             ///< EWDG 时钟名 / EWDG clock name.
  clk_src_t clock_source_;         ///< EWDG 时钟源 / EWDG clock source.
  Configuration current_config_;   ///< 当前配置 / Current configuration.
  uint32_t counter_clock_hz_ = 0;  ///< 计数器频率 / Counter clock frequency.
  bool started_ = false;           ///< 是否已启动 / Whether started.
};

}  // namespace LibXR
