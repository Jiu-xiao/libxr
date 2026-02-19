#include "esp_adc.hpp"

#include <new>

#include "esp_clk_tree.h"
#include "esp_private/adc_share_hw_ctrl.h"
#include "esp_private/esp_clk_tree_common.h"
#include "esp_private/sar_periph_ctrl.h"
#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3
#define sens_dev_t sens_dev_s
#include "hal/adc_oneshot_hal.h"
#undef sens_dev_t
#else
#include "hal/adc_oneshot_hal.h"
#endif

extern portMUX_TYPE rtc_spinlock;

namespace LibXR
{

bool ESP32ADC::InitOneshot()
{
  ASSERT(oneshot_hal_ == nullptr);
  if (oneshot_hal_ != nullptr)
  {
    return false;
  }

  oneshot_hal_ = new (std::nothrow) adc_oneshot_hal_ctx_t{};
  ASSERT(oneshot_hal_ != nullptr);
  if (oneshot_hal_ == nullptr)
  {
    return false;
  }

  adc_oneshot_hal_cfg_t unit_cfg = {};
  unit_cfg.unit = unit_;
  unit_cfg.work_mode = ADC_HAL_SINGLE_READ_MODE;
#if SOC_ADC_RTC_CTRL_SUPPORTED
  unit_cfg.clk_src = ADC_RTC_CLK_SRC_DEFAULT;
#else
  unit_cfg.clk_src = ADC_DIGI_CLK_SRC_DEFAULT;
#endif
  unit_cfg.clk_src_freq_hz = 0U;

#if SOC_ADC_DIG_CTRL_SUPPORTED && !SOC_ADC_RTC_CTRL_SUPPORTED
  if (esp_clk_tree_src_get_freq_hz(
          static_cast<soc_module_clk_t>(unit_cfg.clk_src),
          ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED, &unit_cfg.clk_src_freq_hz) != ESP_OK)
  {
    delete oneshot_hal_;
    oneshot_hal_ = nullptr;
    return false;
  }
  adc_apb_periph_claim();
#endif

  adc_oneshot_hal_init(oneshot_hal_, &unit_cfg);
  sar_periph_ctrl_adc_oneshot_power_acquire();
#if SOC_ADC_CALIBRATION_V1_SUPPORTED
  portENTER_CRITICAL(&rtc_spinlock);
  adc_hal_calibration_init(unit_);
  adc_set_hw_calibration_code(unit_, attenuation_);
  portEXIT_CRITICAL(&rtc_spinlock);
#endif

  adc_oneshot_hal_chan_cfg_t chan_cfg = {};
  chan_cfg.atten = attenuation_;
  chan_cfg.bitwidth = bitwidth_;

  bool any_ready = false;
  for (uint8_t i = 0; i < num_channels_; ++i)
  {
    if (channel_ids_[i] >= SOC_ADC_CHANNEL_NUM(static_cast<int>(unit_)))
    {
      channel_ready_[i] = false;
      continue;
    }

    portENTER_CRITICAL(&rtc_spinlock);
    adc_oneshot_hal_channel_config(oneshot_hal_, &chan_cfg, channel_ids_[i]);
    portEXIT_CRITICAL(&rtc_spinlock);

    channel_ready_[i] = true;
    any_ready = true;
    ConfigureAnalogPad(channel_ids_[i]);
  }

  if (!any_ready)
  {
    sar_periph_ctrl_adc_oneshot_power_release();
#if SOC_ADC_DIG_CTRL_SUPPORTED && !SOC_ADC_RTC_CTRL_SUPPORTED
    adc_apb_periph_free();
#endif
    delete oneshot_hal_;
    oneshot_hal_ = nullptr;
    return false;
  }

  oneshot_inited_ = true;
  backend_ = Backend::ONESHOT;
  return true;
}

}  // namespace LibXR
