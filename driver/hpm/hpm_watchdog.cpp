#include "hpm_watchdog.hpp"

#include <limits>

using namespace LibXR;

namespace
{
ErrorCode ValidateConfiguration(const Watchdog::Configuration& config)
{
  if (config.timeout_ms == 0u || config.feed_ms == 0u ||
      config.feed_ms >= config.timeout_ms)
  {
    return ErrorCode::ARG_ERR;
  }

  return ErrorCode::OK;
}

#if LIBXR_HPM_EWDG_SUPPORTED
constexpr uint32_t kMillisecondsPerSecond = 1000u;
constexpr uint32_t kEwdgOsc32kHz = 32768u;
constexpr uint32_t kEwdgOsc24mHz = 24000000u;
constexpr uint32_t kEwdgBusClockSourceIndex = 0u;
constexpr uint32_t kEwdgExternalClockSourceIndex = 1u;
constexpr uint32_t kEwdgSdkDefaultClockGroup = 0u;

#if defined(EWDG_SOC_OVERTIME_REG_WIDTH) && (EWDG_SOC_OVERTIME_REG_WIDTH == 16)
constexpr uint64_t kEwdgSocTimeoutTickMax = 0xFFFFULL;
#else
constexpr uint64_t kEwdgSocTimeoutTickMax = 0xFFFFFFFFULL;
#endif

#if defined(EWDG_OT_RST_VAL_OT_RST_VAL_MASK) && defined(EWDG_OT_RST_VAL_OT_RST_VAL_SHIFT)
constexpr uint64_t kEwdgRegisterTimeoutTickMax =
    EWDG_OT_RST_VAL_OT_RST_VAL_MASK >> EWDG_OT_RST_VAL_OT_RST_VAL_SHIFT;
#else
constexpr uint64_t kEwdgRegisterTimeoutTickMax = kEwdgSocTimeoutTickMax;
#endif

constexpr uint64_t kEwdgTimeoutTickMax =
    kEwdgSocTimeoutTickMax < kEwdgRegisterTimeoutTickMax ? kEwdgSocTimeoutTickMax
                                                         : kEwdgRegisterTimeoutTickMax;

#if defined(EWDG_SOC_CLK_DIV_VAL_MAX)
constexpr uint32_t kEwdgSocClockDivPowerMax = EWDG_SOC_CLK_DIV_VAL_MAX;
#else
constexpr uint32_t kEwdgSocClockDivPowerMax =
    EWDG_CTRL0_DIV_VALUE_MASK >> EWDG_CTRL0_DIV_VALUE_SHIFT;
#endif

#if defined(EWDG_CTRL0_DIV_VALUE_MASK) && defined(EWDG_CTRL0_DIV_VALUE_SHIFT)
constexpr uint32_t kEwdgRegisterClockDivPowerMax =
    EWDG_CTRL0_DIV_VALUE_MASK >> EWDG_CTRL0_DIV_VALUE_SHIFT;
#else
constexpr uint32_t kEwdgRegisterClockDivPowerMax = kEwdgSocClockDivPowerMax;
#endif

constexpr uint32_t kEwdgClockDivPowerMax =
    kEwdgSocClockDivPowerMax < kEwdgRegisterClockDivPowerMax
        ? kEwdgSocClockDivPowerMax
        : kEwdgRegisterClockDivPowerMax;

uint64_t DivCeil(uint64_t value, uint64_t divisor)
{
  return value / divisor + (value % divisor == 0u ? 0u : 1u);
}

ErrorCode ConvertTimeoutMsToTicks(uint32_t counter_clock_hz, uint32_t timeout_ms,
                                  uint64_t* timeout_ticks)
{
  if (timeout_ticks == nullptr)
  {
    return ErrorCode::PTR_NULL;
  }

  *timeout_ticks = 0u;

  const uint64_t timeout_ms_64 = static_cast<uint64_t>(timeout_ms);
  if (counter_clock_hz != 0u &&
      timeout_ms_64 > std::numeric_limits<uint64_t>::max() / counter_clock_hz)
  {
    return ErrorCode::OUT_OF_RANGE;
  }

  const uint64_t timeout_clock_cycles =
      timeout_ms_64 * static_cast<uint64_t>(counter_clock_hz);
  *timeout_ticks = DivCeil(timeout_clock_cycles, kMillisecondsPerSecond);
  return ErrorCode::OK;
}

ErrorCode ValidateClockSource(clock_name_t clock, clk_src_t source)
{
  const uint32_t source_group = GET_CLK_SRC_GROUP(source);
  const uint32_t source_index = GET_CLK_SRC_INDEX(source);
  const bool is_pwdg_clock = (clock == clock_pwdg);

  if ((is_pwdg_clock && source_group != CLK_SRC_GROUP_PEWDG) ||
      (!is_pwdg_clock && source_group != CLK_SRC_GROUP_EWDG))
  {
    return ErrorCode::ARG_ERR;
  }

  if (source_index == kEwdgExternalClockSourceIndex ||
      source_index == kEwdgBusClockSourceIndex)
  {
    return ErrorCode::OK;
  }

  return ErrorCode::ARG_ERR;
}
#endif
}  // namespace

HPMWatchdog::HPMWatchdog(LibXRHpmEwdgType* ewdg, clock_name_t clock, uint32_t timeout_ms,
                         uint32_t feed_ms, clk_src_t clock_source, bool auto_start)
    : ewdg_(ewdg),
      clock_(clock),
      clock_source_(clock_source),
      current_config_{timeout_ms, feed_ms}
{
#if LIBXR_HPM_EWDG_SUPPORTED
  if (auto_start)
  {
    (void)Start();
  }
#else
  (void)ewdg_;
  (void)clock_;
  (void)clock_source_;
  (void)auto_start;
#endif
}

ErrorCode HPMWatchdog::ConvertStatus(hpm_stat_t status)
{
#if LIBXR_HPM_EWDG_SUPPORTED
  switch (status)
  {
    case status_success:
      return ErrorCode::OK;
    case status_invalid_argument:
      return ErrorCode::ARG_ERR;
    case status_ewdg_tick_out_of_range:
    case status_ewdg_div_out_of_range:
      return ErrorCode::OUT_OF_RANGE;
    case status_ewdg_feature_unsupported:
      return ErrorCode::NOT_SUPPORT;
    default:
      return ErrorCode::FAILED;
  }
#else
  (void)status;
  return ErrorCode::NOT_SUPPORT;
#endif
}

ErrorCode HPMWatchdog::SetConfig(const Configuration& config)
{
  ErrorCode ans = ValidateConfiguration(config);
  if (ans != ErrorCode::OK)
  {
    return ans;
  }

#if LIBXR_HPM_EWDG_SUPPORTED
  if (ewdg_ == nullptr)
  {
    return ErrorCode::PTR_NULL;
  }

  ans = ApplyConfiguration(config, started_);
  if (ans == ErrorCode::OK)
  {
    current_config_ = config;
  }

  return ans;
#else
  (void)config;
  return ErrorCode::NOT_SUPPORT;
#endif
}

ErrorCode HPMWatchdog::Feed()
{
#if LIBXR_HPM_EWDG_SUPPORTED
  if (ewdg_ == nullptr)
  {
    return ErrorCode::PTR_NULL;
  }
  if (!started_)
  {
    return ErrorCode::INIT_ERR;
  }

  return ConvertStatus(ewdg_refresh(ewdg_));
#else
  return ErrorCode::NOT_SUPPORT;
#endif
}

ErrorCode HPMWatchdog::Start()
{
#if LIBXR_HPM_EWDG_SUPPORTED
  if (ewdg_ == nullptr)
  {
    return ErrorCode::PTR_NULL;
  }

  ErrorCode ans = ValidateConfiguration(current_config_);
  if (ans != ErrorCode::OK)
  {
    return ans;
  }

  ans = ApplyConfiguration(current_config_, true);
  if (ans != ErrorCode::OK)
  {
    return ans;
  }

  ans = ConvertStatus(ewdg_refresh(ewdg_));
  if (ans != ErrorCode::OK)
  {
    ewdg_disable(ewdg_);
    this->auto_feed_ = false;
    started_ = false;
    return ans;
  }

  this->auto_feed_ = true;
  started_ = true;
  return ErrorCode::OK;
#else
  return ErrorCode::NOT_SUPPORT;
#endif
}

ErrorCode HPMWatchdog::Stop()
{
#if LIBXR_HPM_EWDG_SUPPORTED
  if (ewdg_ == nullptr)
  {
    return ErrorCode::PTR_NULL;
  }

  ewdg_disable(ewdg_);
  this->auto_feed_ = false;
  started_ = false;
  return ErrorCode::OK;
#else
  return ErrorCode::NOT_SUPPORT;
#endif
}

#if LIBXR_HPM_EWDG_SUPPORTED
ErrorCode HPMWatchdog::EnsureClockReady()
{
  const clk_src_t clock_source = ResolveClockSource();
  ErrorCode ans = ValidateClockSource(clock_, clock_source);
  if (ans != ErrorCode::OK)
  {
    return ans;
  }

  clock_add_to_group(clock_, kEwdgSdkDefaultClockGroup);

  if (ResolveEwdgClockSelect() == ewdg_cnt_clk_src_bus_clk)
  {
    ewdg_switch_clock_source(ewdg_, ewdg_cnt_clk_src_bus_clk);
  }

  return ResolveCounterClockFrequency(clock_source, &counter_clock_hz_);
}

ErrorCode HPMWatchdog::ResolveTimeoutSetting(uint32_t timeout_ms,
                                             uint32_t counter_clock_hz,
                                             uint32_t* timeout_ticks,
                                             uint32_t* clock_div_power) const
{
  if (timeout_ticks == nullptr || clock_div_power == nullptr)
  {
    return ErrorCode::PTR_NULL;
  }

  *timeout_ticks = 0u;
  *clock_div_power = 0u;

  if (counter_clock_hz == 0u)
  {
    return ErrorCode::INIT_ERR;
  }

  uint64_t ticks = 0u;
  ErrorCode ans = ConvertTimeoutMsToTicks(counter_clock_hz, timeout_ms, &ticks);
  if (ans != ErrorCode::OK)
  {
    return ans;
  }

  for (uint32_t div_power = 0u; div_power <= kEwdgClockDivPowerMax; ++div_power)
  {
    if (ticks <= kEwdgTimeoutTickMax)
    {
      *timeout_ticks = static_cast<uint32_t>(ticks);
      *clock_div_power = div_power;
      return ErrorCode::OK;
    }

    ticks = DivCeil(ticks, 2u);
  }

  return ErrorCode::OUT_OF_RANGE;
}

ErrorCode HPMWatchdog::ApplyConfiguration(const Configuration& config,
                                          bool enable_watchdog)
{
  ErrorCode ans = EnsureClockReady();
  if (ans != ErrorCode::OK)
  {
    return ans;
  }

  ewdg_config_t sdk_config{};
  ewdg_get_default_config(ewdg_, &sdk_config);
  sdk_config.enable_watchdog = enable_watchdog;
  sdk_config.cnt_src_freq = counter_clock_hz_;

  uint32_t timeout_ticks = 0u;
  uint32_t clock_div_power = 0u;
  ans = ResolveTimeoutSetting(config.timeout_ms, counter_clock_hz_, &timeout_ticks,
                              &clock_div_power);
  if (ans != ErrorCode::OK)
  {
    return ans;
  }

  sdk_config.ctrl_config.cnt_clk_sel = ResolveEwdgClockSelect();
  sdk_config.ctrl_config.use_lowlevel_timeout = true;
  sdk_config.ctrl_config.timeout_interrupt_val = 0u;
  sdk_config.ctrl_config.timeout_reset_val = timeout_ticks;
  sdk_config.ctrl_config.clock_div_by_power_of_2 = clock_div_power;
  sdk_config.ctrl_config.enable_window_mode = false;
  sdk_config.ctrl_config.enable_refresh_period = false;
  sdk_config.ctrl_config.enable_refresh_lock = false;
  sdk_config.ctrl_config.enable_config_lock = false;

  sdk_config.int_rst_config.enable_timeout_interrupt = false;
  sdk_config.int_rst_config.enable_timeout_reset = true;
  sdk_config.int_rst_config.enable_ctrl_parity_fail_reset = true;
  sdk_config.int_rst_config.enable_ctrl_unlock_fail_reset = true;
  sdk_config.int_rst_config.enable_ctrl_update_violation_reset = true;
  sdk_config.int_rst_config.enable_refresh_unlock_fail_reset = true;
  sdk_config.int_rst_config.enable_refresh_violation_reset = true;

  ans = ConvertStatus(ewdg_init(ewdg_, &sdk_config));
  if (ans != ErrorCode::OK)
  {
    return ans;
  }

  this->timeout_ms_ = config.timeout_ms;
  this->auto_feed_interval_ms = config.feed_ms;
  return ErrorCode::OK;
}

clk_src_t HPMWatchdog::ResolveClockSource() const
{
  if (clock_source_ != kAutoClockSource)
  {
    return clock_source_;
  }

  if (clock_ == clock_pwdg)
  {
    return clk_pwdg_src_osc32k;
  }

  return clk_wdg_src_osc32k;
}

ewdg_cnt_clk_sel_t HPMWatchdog::ResolveEwdgClockSelect() const
{
  const clk_src_t resolved_source = ResolveClockSource();
  const uint32_t source_group = GET_CLK_SRC_GROUP(resolved_source);
  const uint32_t source_index = GET_CLK_SRC_INDEX(resolved_source);

  if ((source_group == CLK_SRC_GROUP_EWDG || source_group == CLK_SRC_GROUP_PEWDG) &&
      source_index == kEwdgBusClockSourceIndex)
  {
    return ewdg_cnt_clk_src_bus_clk;
  }

  return ewdg_cnt_clk_src_ext_osc_clk;
}

ErrorCode HPMWatchdog::ResolveCounterClockFrequency(clk_src_t source,
                                                    uint32_t* frequency_hz) const
{
  if (frequency_hz == nullptr)
  {
    return ErrorCode::PTR_NULL;
  }

  *frequency_hz = 0u;

  const uint32_t source_group = GET_CLK_SRC_GROUP(source);
  const uint32_t source_index = GET_CLK_SRC_INDEX(source);

  const ErrorCode ans = ValidateClockSource(clock_, source);
  if (ans != ErrorCode::OK)
  {
    return ans;
  }

  if (source_group == CLK_SRC_GROUP_EWDG)
  {
    if (source_index == kEwdgExternalClockSourceIndex)
    {
      *frequency_hz = kEwdgOsc32kHz;
      return ErrorCode::OK;
    }

    if (source_index == kEwdgBusClockSourceIndex)
    {
      *frequency_hz = clock_get_frequency(clock_);
      return *frequency_hz == 0u ? ErrorCode::INIT_ERR : ErrorCode::OK;
    }

    return ErrorCode::ARG_ERR;
  }

  if (source_group == CLK_SRC_GROUP_PEWDG)
  {
    if (source_index == kEwdgExternalClockSourceIndex)
    {
      *frequency_hz = kEwdgOsc32kHz;
      return ErrorCode::OK;
    }

    if (source_index == kEwdgBusClockSourceIndex)
    {
      *frequency_hz = kEwdgOsc24mHz;
      return ErrorCode::OK;
    }

    return ErrorCode::ARG_ERR;
  }

  return ErrorCode::ARG_ERR;
}
#endif
