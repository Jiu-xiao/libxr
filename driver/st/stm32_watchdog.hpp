#pragma once

#include "main.h"
#include "watchdog.hpp"

#if defined(HAL_IWDG_MODULE_ENABLED)

namespace LibXR
{

/**
 * @class STM32Watchdog
 * @brief STM32 IWDG 独立看门狗 / Independent Watchdog
 *
 */
class STM32Watchdog : public Watchdog
{
 public:
  explicit STM32Watchdog(IWDG_HandleTypeDef* hiwdg, uint32_t timeout_ms = 1000,
                         uint32_t feed_ms = 250, uint32_t clock = LSI_VALUE)
      : hiwdg_(hiwdg), clock_(clock)
  {
    ASSERT(hiwdg);
    SetConfig({timeout_ms, feed_ms});
  }

  ErrorCode SetConfig(const Configuration& config) override
  {
    if (config.feed_ms == 0 || config.timeout_ms == 0 ||
        config.feed_ms > config.timeout_ms)
    {
      ASSERT(false);
      return ErrorCode::ARG_ERR;
    }

    /* 分频与重载自动计算 / Prescaler & reload auto calculation */
    static const struct
    {
      uint8_t pr;
      uint16_t div;
    } TABLE[] = {{0, 4}, {1, 8}, {2, 16}, {3, 32}, {4, 64}, {5, 128}, {6, 256}};
    uint32_t lsi = clock_;
    uint8_t best_pr = 6;
    uint16_t best_rlr = 0xFFF;
    bool found = false;

    for (const auto& item : TABLE)
    {
      uint32_t prescaler = item.div;
      uint32_t reload = (config.timeout_ms * lsi) / (1000 * prescaler);
      if (reload == 0)
      {
        reload = 1;
      }
      if (reload > 1)
      {
        reload--;
      }
      if (reload <= 0xFFF)
      {
        best_pr = item.pr;
        best_rlr = static_cast<uint16_t>(reload);
        found = true;
        break;
      }
    }
    if (!found)
    {
      return ErrorCode::NOT_SUPPORT;
    }

    timeout_ms_ = config.timeout_ms;
    feed_ms_ = config.feed_ms;

    hiwdg_->Init.Prescaler = best_pr;
    hiwdg_->Init.Reload = best_rlr;
#if defined(IWDG)
    hiwdg_->Instance = IWDG;  // NOLINT
#elif defined(IWDG1)
    hiwdg_->Instance = IWDG1;  // NOLINT
#endif

    return ErrorCode::OK;
  }

  ErrorCode Feed() override
  {
    return HAL_IWDG_Refresh(hiwdg_) == HAL_OK ? ErrorCode::OK : ErrorCode::FAILED;
  }

  ErrorCode Start() override
  {
    auto_feed_ = true;
    if (HAL_IWDG_Init(hiwdg_) != HAL_OK)
    {
      return ErrorCode::FAILED;
    }
    return ErrorCode::OK;
  }

  ErrorCode Stop() override
  {
    /* STM32 IWDG 不支持关闭 */
    auto_feed_ = false;
    return ErrorCode::NOT_SUPPORT;
  }

  IWDG_HandleTypeDef* hiwdg_;  ///< STM32 HAL IWDG handle
  uint32_t clock_;             ///< LSI clock in Hz
};

}  // namespace LibXR

#endif
