#pragma once

#include "main.h"
#include "watchdog.hpp"

#if defined(HAL_IWDG_MODULE_ENABLED)

namespace LibXR
{

/**
 * @brief STM32 IWDG 看门狗驱动 / STM32 independent watchdog driver
 *
 */
class STM32Watchdog : public Watchdog
{
 public:
  explicit STM32Watchdog(IWDG_HandleTypeDef* hiwdg, uint32_t timeout_ms = 1000,
                         uint32_t feed_ms = 250, uint32_t clock = LSI_VALUE);

  ErrorCode SetConfig(const Configuration& config) override;

  ErrorCode Feed() override;

  ErrorCode Start() override;

  ErrorCode Stop() override;

  IWDG_HandleTypeDef* hiwdg_;  ///< STM32 HAL IWDG handle
  uint32_t clock_;             ///< LSI clock in Hz
};

}  // namespace LibXR

#endif
