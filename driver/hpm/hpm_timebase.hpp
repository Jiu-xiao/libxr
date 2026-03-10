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
   */
  HPMTimebase(MCHTMR_Type* timer = HPM_MCHTMR, clock_name_t clock = clock_mchtmr0);

  /**
   * @brief 获取微秒时间戳 / Get current timestamp in microseconds.
   * @return 当前微秒时间戳 / Current microsecond timestamp.
   */
  MicrosecondTimestamp _get_microseconds() override;

  /**
   * @brief 获取毫秒时间戳 / Get current timestamp in milliseconds.
   * @return 当前毫秒时间戳 / Current millisecond timestamp.
   */
  MillisecondTimestamp _get_milliseconds() override;

 private:
  /** @brief 定时器外设实例 / Timer peripheral instance. */
  MCHTMR_Type* timer_;
  /** @brief 定时器输入时钟频率(Hz) / Timer input clock frequency in Hz. */
  uint32_t clock_hz_;
};

}  // namespace LibXR
