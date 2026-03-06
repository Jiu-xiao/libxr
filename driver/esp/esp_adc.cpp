#include "esp_adc.hpp"

#include <new>

#include "esp_clk_tree.h"
#include "esp_private/adc_private.h"
#include "esp_private/adc_share_hw_ctrl.h"
#include "esp_private/esp_clk_tree_common.h"
#include "esp_private/gpio.h"
#include "esp_private/sar_periph_ctrl.h"
#include "hal/adc_hal_common.h"
#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3
#define sens_dev_t sens_dev_s
#include "hal/adc_oneshot_hal.h"
#undef sens_dev_t
#else
#include "hal/adc_oneshot_hal.h"
#endif

#if defined(CONFIG_IDF_TARGET_ESP32) && CONFIG_IDF_TARGET_ESP32 && \
    defined(CONFIG_ESP_WIFI_ENABLED) && CONFIG_ESP_WIFI_ENABLED
#include "esp_wifi.h"
#endif

#ifndef ANALOG_CLOCK_ENABLE
#define ANALOG_CLOCK_ENABLE() ((void)0)
#endif
#ifndef ANALOG_CLOCK_DISABLE
#define ANALOG_CLOCK_DISABLE() ((void)0)
#endif

extern portMUX_TYPE rtc_spinlock;

namespace LibXR
{

ESP32ADC::Channel::Channel() : parent_(nullptr), idx_(0), channel_num_(0) {}

ESP32ADC::Channel::Channel(ESP32ADC* parent, uint8_t idx, uint8_t channel_num)
    : parent_(parent), idx_(idx), channel_num_(channel_num)
{
}

float ESP32ADC::Channel::Read() { return parent_ ? parent_->ReadChannel(idx_) : 0.f; }

ESP32ADC::ESP32ADC(adc_unit_t unit, const adc_channel_t* channels, uint8_t num_channels,
                   uint32_t freq, adc_atten_t attenuation, adc_bitwidth_t bitwidth,
                   float reference_voltage, size_t dma_buf_size)
    : unit_(unit),
      num_channels_(num_channels),
      attenuation_(attenuation),
      bitwidth_(ResolveBitwidth(bitwidth)),
      reference_voltage_(reference_voltage),
      max_raw_((1U << static_cast<uint8_t>(bitwidth_)) - 1U)
{
  ASSERT(channels != nullptr);
  ASSERT(num_channels_ > 0U);
  ASSERT(reference_voltage_ > 0.0f);
  ASSERT(max_raw_ != 0U);
  ASSERT(num_channels_ <= SOC_ADC_MAX_CHANNEL_NUM);
  if ((channels == nullptr) || (num_channels_ == 0U) || (reference_voltage_ <= 0.0f) ||
      (max_raw_ == 0U) || (num_channels_ > SOC_ADC_MAX_CHANNEL_NUM))
  {
    return;
  }

  channels_ = new (std::nothrow) Channel[num_channels_];
  channel_ids_ = new (std::nothrow) adc_channel_t[num_channels_];
  channel_ready_ = new (std::nothrow) bool[num_channels_];
  latest_values_ = new (std::nothrow) float[num_channels_];
  latest_raw_ = new (std::nothrow) uint16_t[num_channels_];
  ASSERT(channels_ != nullptr);
  ASSERT(channel_ids_ != nullptr);
  ASSERT(channel_ready_ != nullptr);
  ASSERT(latest_values_ != nullptr);
  ASSERT(latest_raw_ != nullptr);
  if ((channels_ == nullptr) || (channel_ids_ == nullptr) || (channel_ready_ == nullptr) ||
      (latest_values_ == nullptr) || (latest_raw_ == nullptr))
  {
    ASSERT(false);
    return;
  }

  for (uint8_t i = 0; i < SOC_ADC_MAX_CHANNEL_NUM; ++i)
  {
    channel_idx_map_[i] = kInvalidChannelIdx;
  }

  for (uint8_t i = 0; i < num_channels_; ++i)
  {
    ASSERT(IsValidChannel(channels[i]));
    const uint8_t ch = static_cast<uint8_t>(channels[i]);
    ASSERT(channel_idx_map_[ch] == kInvalidChannelIdx);
    if (!IsValidChannel(channels[i]) || (channel_idx_map_[ch] != kInvalidChannelIdx))
    {
      return;
    }

    channels_[i] = Channel(this, i, ch);
    channel_ids_[i] = channels[i];
    channel_idx_map_[ch] = i;
    channel_ready_[i] = false;
    latest_values_[i] = 0.0f;
    latest_raw_[i] = 0U;
  }

#if defined(CONFIG_IDF_TARGET_ESP32) && CONFIG_IDF_TARGET_ESP32 && \
    defined(CONFIG_ESP_WIFI_ENABLED) && CONFIG_ESP_WIFI_ENABLED
  if (unit_ == ADC_UNIT_2)
  {
    wifi_mode_t wifi_mode = WIFI_MODE_NULL;
    const esp_err_t wifi_mode_err = esp_wifi_get_mode(&wifi_mode);
    const bool wifi_active = (wifi_mode_err == ESP_OK) && (wifi_mode != WIFI_MODE_NULL);
    ASSERT(!wifi_active);
    if (wifi_active)
    {
      return;
    }
  }
#endif

  const ContinuousInitResult cont_ans = InitContinuous(freq, dma_buf_size);
  if (cont_ans == ContinuousInitResult::STARTED)
  {
    valid_ = true;
    return;
  }

  if (cont_ans == ContinuousInitResult::FAILED)
  {
    ASSERT(false);
    return;
  }

  const bool oneshot_ok = InitOneshot();
  ASSERT(oneshot_ok);
  if (!oneshot_ok)
  {
    return;
  }

  valid_ = true;
}

ESP32ADC::~ESP32ADC()
{
#if SOC_ADC_DIG_CTRL_SUPPORTED && SOC_ADC_DMA_SUPPORTED
  DeinitContinuous();
#endif

  if (oneshot_inited_)
  {
    sar_periph_ctrl_adc_oneshot_power_release();
#if SOC_ADC_DIG_CTRL_SUPPORTED && !SOC_ADC_RTC_CTRL_SUPPORTED
    adc_apb_periph_free();
#endif
    oneshot_inited_ = false;
  }
  delete oneshot_hal_;
  oneshot_hal_ = nullptr;

  delete[] channels_;
  delete[] channel_ids_;
  delete[] channel_ready_;
  delete[] latest_values_;
  delete[] latest_raw_;
}

ESP32ADC::Channel& ESP32ADC::GetChannel(uint8_t idx)
{
  ASSERT(idx < num_channels_);
  return channels_[idx];
}

float ESP32ADC::ReadChannel(uint8_t idx)
{
  ASSERT(valid_);
  ASSERT(idx < num_channels_);
  if (!valid_ || (idx >= num_channels_))
  {
    return 0.f;
  }

  if (backend_ == Backend::CONTINUOUS_DMA)
  {
#if SOC_ADC_DIG_CTRL_SUPPORTED && SOC_ADC_DMA_SUPPORTED
    if (!channel_ready_[idx])
    {
      DrainContinuousFrames(continuous_prime_timeout_ms_);
    }
    else
    {
      DrainContinuousFrames(0U);
    }
#endif
    ASSERT(channel_ready_[idx]);
    return latest_values_[idx];
  }

  ASSERT(backend_ == Backend::ONESHOT);
  ASSERT(oneshot_inited_ && (oneshot_hal_ != nullptr));
  if (!oneshot_inited_ || (oneshot_hal_ == nullptr))
  {
    return 0.f;
  }

  const esp_err_t lock_err = adc_lock_acquire(unit_);
  ASSERT(lock_err == ESP_OK);
  if (lock_err != ESP_OK)
  {
    return 0.f;
  }

  int raw = 0;
  bool converted = false;
  bool clk_src_enabled = false;

  portENTER_CRITICAL(&rtc_spinlock);

#if SOC_ADC_DIG_CTRL_SUPPORTED && !SOC_ADC_RTC_CTRL_SUPPORTED
  const esp_err_t clk_on =
      esp_clk_tree_enable_src(static_cast<soc_module_clk_t>(oneshot_hal_->clk_src), true);
  ASSERT(clk_on == ESP_OK);
  clk_src_enabled = (clk_on == ESP_OK);
#else
  clk_src_enabled = true;
#endif

  if (clk_src_enabled)
  {
    ANALOG_CLOCK_ENABLE();
    adc_oneshot_hal_setup(oneshot_hal_, channel_ids_[idx]);
#if SOC_ADC_CALIBRATION_V1_SUPPORTED
    adc_set_hw_calibration_code(unit_, attenuation_);
#endif
    converted = adc_oneshot_hal_convert(oneshot_hal_, &raw);
    ANALOG_CLOCK_DISABLE();
  }

#if SOC_ADC_DIG_CTRL_SUPPORTED && !SOC_ADC_RTC_CTRL_SUPPORTED
  if (clk_src_enabled)
  {
    const esp_err_t clk_off =
        esp_clk_tree_enable_src(static_cast<soc_module_clk_t>(oneshot_hal_->clk_src), false);
    ASSERT(clk_off == ESP_OK);
    if (clk_off != ESP_OK)
    {
      converted = false;
    }
  }
#endif

  portEXIT_CRITICAL(&rtc_spinlock);

  const esp_err_t unlock_err = adc_lock_release(unit_);
  ASSERT(unlock_err == ESP_OK);
  if (unlock_err != ESP_OK)
  {
    return 0.f;
  }

  ASSERT(converted);
  if (!converted)
  {
    return 0.f;
  }

  latest_raw_[idx] = static_cast<uint16_t>(raw);
  latest_values_[idx] = Normalize(static_cast<float>(raw));
  channel_ready_[idx] = true;
  return latest_values_[idx];
}

adc_bitwidth_t ESP32ADC::ResolveBitwidth(adc_bitwidth_t bitwidth)
{
  if (bitwidth == ADC_BITWIDTH_DEFAULT)
  {
    return static_cast<adc_bitwidth_t>(SOC_ADC_RTC_MAX_BITWIDTH);
  }
  return bitwidth;
}

float ESP32ADC::Normalize(float raw) const
{
  return (raw / static_cast<float>(max_raw_)) * reference_voltage_;
}

uint32_t ESP32ADC::ClampSampleFreq(uint32_t freq)
{
  if (freq < SOC_ADC_SAMPLE_FREQ_THRES_LOW)
  {
    return SOC_ADC_SAMPLE_FREQ_THRES_LOW;
  }
  if (freq > SOC_ADC_SAMPLE_FREQ_THRES_HIGH)
  {
    return SOC_ADC_SAMPLE_FREQ_THRES_HIGH;
  }
  return freq;
}

uint32_t ESP32ADC::AlignUp(uint32_t value, uint32_t align)
{
  if (align == 0U)
  {
    return value;
  }
  return (value + align - 1U) / align * align;
}

bool ESP32ADC::IsValidChannel(adc_channel_t channel) const
{
  const int channel_count = SOC_ADC_CHANNEL_NUM(static_cast<int>(unit_));
  const int channel_num = static_cast<int>(channel);
  return (channel_count > 0) && (channel_num >= 0) && (channel_num < channel_count);
}

void ESP32ADC::ConfigureAnalogPad(adc_channel_t channel) const
{
  int io_num = -1;
  if ((adc_channel_to_io(unit_, channel, &io_num) == ESP_OK) && (io_num >= 0))
  {
    (void)gpio_config_as_analog(static_cast<gpio_num_t>(io_num));
  }
}

}  // namespace LibXR
