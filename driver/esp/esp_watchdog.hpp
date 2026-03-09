#pragma once

#include "esp_def.hpp"

#include <cstdint>

#include "hal/wdt_hal.h"
#include "soc/soc_caps.h"
#include "watchdog.hpp"

namespace LibXR
{

#if CONFIG_ESP_TASK_WDT_EN && !CONFIG_ESP_TASK_WDT_USE_ESP_TIMER
#define LIBXR_ESP_WDT_MWDT0_RESERVED 1
#else
#define LIBXR_ESP_WDT_MWDT0_RESERVED 0
#endif

#if SOC_TIMER_GROUPS >= 2
#if CONFIG_ESP_INT_WDT
#define LIBXR_ESP_WDT_MWDT1_RESERVED 1
#else
#define LIBXR_ESP_WDT_MWDT1_RESERVED 0
#endif
#else
#define LIBXR_ESP_WDT_MWDT1_RESERVED 1
#endif

#if (LIBXR_ESP_WDT_MWDT1_RESERVED == 0)
constexpr wdt_inst_t kESP32WatchdogDefaultInstance = WDT_MWDT1;
#elif (LIBXR_ESP_WDT_MWDT0_RESERVED == 0)
constexpr wdt_inst_t kESP32WatchdogDefaultInstance = WDT_MWDT0;
#else
constexpr wdt_inst_t kESP32WatchdogDefaultInstance = WDT_RWDT;
#endif

static_assert(!(kESP32WatchdogDefaultInstance == WDT_MWDT0 &&
                LIBXR_ESP_WDT_MWDT0_RESERVED),
              "LibXR ESP32Watchdog selected MWDT0, but MWDT0 is reserved by ESP-IDF.");
static_assert(!(kESP32WatchdogDefaultInstance == WDT_MWDT1 &&
                LIBXR_ESP_WDT_MWDT1_RESERVED),
              "LibXR ESP32Watchdog selected MWDT1, but MWDT1 is reserved by ESP-IDF.");

class ESP32Watchdog : public Watchdog
{
 public:
  explicit ESP32Watchdog(uint32_t timeout_ms = 1000, uint32_t feed_ms = 250);
  ~ESP32Watchdog() override;

  ErrorCode SetConfig(const Configuration& config) override;
  ErrorCode Feed() override;
  ErrorCode Start() override;
  ErrorCode Stop() override;

 private:
  static bool IsInstanceSupported(wdt_inst_t instance);
  ErrorCode InitializeHardware();
  ErrorCode ApplyConfiguration();
  void ReleaseHardware();

  wdt_hal_context_t hal_{};
  const wdt_inst_t instance_ = kESP32WatchdogDefaultInstance;
  bool initialized_ = false;
  bool started_ = false;
};

}  // namespace LibXR

#undef LIBXR_ESP_WDT_MWDT0_RESERVED
#undef LIBXR_ESP_WDT_MWDT1_RESERVED
