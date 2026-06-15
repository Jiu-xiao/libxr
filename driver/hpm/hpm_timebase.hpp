#pragma once

#include "timebase.hpp"

#include "hpm_clock_drv.h"
#include "hpm_mchtmr_drv.h"
#include "hpm_soc.h"

namespace LibXR
{

/**
 * @class HPMTimebase
 * @brief HPM MCHTMR 时间基准实现 / HPM MCHTMR based timebase implementation.
 *
 * 基于 MCHTMR 计数器提供微秒与毫秒时间戳。
 * Provides microsecond and millisecond timestamps using MCHTMR counter.
 */
class HPMTimebase : public Timebase
{
 public:
  /**
   * @brief 构造 HPM 时间基对象 / Construct an HPM timebase object.
   * @param timer MCHTMR 外设基地址 / MCHTMR base address.
   * @param clock 定时器时钟源 / Clock source used by the timer.
   *
   * 构造时选择计数器实例并缓存输入时钟频率。
   * Construction selects the counter instance and caches the input clock rate.
   */
  HPMTimebase(MCHTMR_Type* timer = HPM_MCHTMR, clock_name_t clock = clock_mchtmr0);
};

}  // namespace LibXR
