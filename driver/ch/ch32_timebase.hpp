#pragma once

#include "libxr.hpp"

#include DEF2STR(LIBXR_CH32_CONFIG_FILE)

namespace LibXR
{

/**
 * @brief CH32 时间基准实现 / CH32 timebase implementation
 */
class CH32Timebase : public Timebase
{
 public:
  /**
   * @brief 构造函数 / Constructor
   *
   * 要求 BSP 已将 SysTick 配置为 HCLK 驱动的 64 位自由运行向上计数器。
   * Requires the BSP to configure SysTick as a 64-bit HCLK-driven,
   * free-running up-counter.
   * @note 构造后不得改变 HCLK 或 SystemCoreClock。
   *       HCLK and SystemCoreClock must remain unchanged after construction.
   */
  CH32Timebase();
};

}  // namespace LibXR
