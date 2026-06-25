#pragma once

#include "pwm.hpp"

#include "hpm_soc.h"
#include "hpm_clock_drv.h"
#include "hpm_gptmr_drv.h"

#if defined(PWM_SOC_CMP_MAX_COUNT) && defined(PWM_SOC_OUTPUT_TO_PWM_MAX_COUNT)
#define LIBXR_HPM_PWM_SUPPORTED 1
#include "hpm_pwm_drv.h"
using LibXRHpmPwmType = PWM_Type;
#else
#define LIBXR_HPM_PWM_SUPPORTED 0
using LibXRHpmPwmType = void;
#endif

#if !LIBXR_HPM_PWM_SUPPORTED
#define LIBXR_HPM_GPTMR_PWM_FALLBACK 1
#else
#define LIBXR_HPM_GPTMR_PWM_FALLBACK 0
#endif

namespace LibXR
{

/**
 * @class HPMPWM
 * @brief HPM 平台 PWM 驱动实现 / PWM driver implementation for HPM platform.
 */
class HPMPWM : public PWM
{
 public:
  /**
   * @brief 构造 HPM PWM 对象 / Construct an HPM PWM object.
   * @param pwm PWM 外设基地址 / PWM peripheral base address.
   * @param clock PWM 对应时钟源 / Clock source used by this PWM instance.
   * @param pwm_index PWM 输出通道索引 / PWM output index.
   * @param cmp_index 与输出绑定的比较器索引 / Comparator index bound to the PWM output.
   * @param invert 是否反相输出 / Whether to invert output polarity.
   * @param auto_board_init 是否在 GPTMR fallback 路径自动调用板级时钟/引脚初始化 /
   * Whether to auto-run board-level clock/pin init in GPTMR fallback path.
   */
  HPMPWM(LibXRHpmPwmType* pwm, clock_name_t clock, uint8_t pwm_index, uint8_t cmp_index,
         bool invert = false, bool auto_board_init = true);

  /**
   * @brief 设置占空比 / Set PWM duty cycle.
   * @param value 占空比范围 [0.0, 1.0] / Duty cycle in range [0.0, 1.0].
   * @return 操作结果 / Operation result.
   */
  ErrorCode SetDutyCycle(float value) override;

  /**
   * @brief 配置 PWM 频率 / Configure PWM frequency.
   * @param config PWM 配置参数 / PWM configuration.
   * @return 操作结果 / Operation result.
   */
  ErrorCode SetConfig(Configuration config) override;

  /**
   * @brief 启动 PWM 输出 / Start PWM output.
   * @return 操作结果 / Operation result.
   */
  ErrorCode Enable() override;

  /**
   * @brief 停止 PWM 输出 / Stop PWM output.
   * @return 操作结果 / Operation result.
   */
  ErrorCode Disable() override;

 private:
  static constexpr uint8_t INVALID_CMP_INDEX = 0xFFu;
  static uint8_t ResolveGptmrReloadCmpIndex(uint8_t duty_cmp_index);

  LibXRHpmPwmType* pwm_;
  GPTMR_Type* gptmr_;
  clock_name_t clock_;
  uint8_t pwm_index_;
  uint8_t cmp_index_;
  bool invert_;
  bool auto_board_init_;
  uint32_t reload_;
  bool configured_;
};

}  // namespace LibXR
