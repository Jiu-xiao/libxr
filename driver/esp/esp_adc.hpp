#pragma once

#include <cstddef>
#include <cstdint>

#include "sdkconfig.h"
#include "adc.hpp"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_private/gpio.h"
#include "hal/adc_types.h"
#include "soc/soc_caps.h"

namespace LibXR
{

class ESP32ADC
{
 public:
  class Channel : public ADC
  {
   public:
    Channel() : parent_(nullptr), idx_(0), channel_num_(0) {}

    Channel(ESP32ADC* parent, uint8_t idx, uint8_t channel_num)
        : parent_(parent), idx_(idx), channel_num_(channel_num)
    {
    }

    float Read() override { return parent_ ? parent_->ReadChannel(idx_) : 0.f; }

    uint8_t ChannelNumber() const { return channel_num_; }

   private:
    ESP32ADC* parent_;
    uint8_t idx_;
    uint8_t channel_num_;
  };

  ESP32ADC(
      adc_unit_t unit, const adc_channel_t* channels, uint8_t num_channels,
      uint32_t freq = SOC_ADC_SAMPLE_FREQ_THRES_LOW,
      adc_atten_t attenuation = ADC_ATTEN_DB_12,
      adc_bitwidth_t bitwidth = static_cast<adc_bitwidth_t>(SOC_ADC_DIGI_MAX_BITWIDTH),
      float reference_voltage = 3.3f, size_t dma_buf_size = 256)
      : unit_(unit),
        num_channels_(num_channels),
        attenuation_(attenuation),
        bitwidth_(ResolveBitwidth(bitwidth)),
        reference_voltage_(reference_voltage),
        max_raw_((1U << static_cast<uint8_t>(bitwidth_)) - 1U)
  {
    (void)freq;
    (void)dma_buf_size;

#if defined(CONFIG_IDF_TARGET_ESP32) && CONFIG_IDF_TARGET_ESP32 && \
    defined(CONFIG_ESP_WIFI_ENABLED) && CONFIG_ESP_WIFI_ENABLED
    ASSERT(unit_ != ADC_UNIT_2);
#endif

    channels_ = new Channel[num_channels_];
    channel_ids_ = new adc_channel_t[num_channels_];
    channel_ready_ = new bool[num_channels_];
    latest_values_ = new float[num_channels_];

    for (uint8_t i = 0; i < num_channels_; ++i)
    {
      channels_[i] = Channel(this, i, static_cast<uint8_t>(channels[i]));
      channel_ids_[i] = channels[i];
      channel_ready_[i] = false;
      latest_values_[i] = 0.f;
    }

    adc_oneshot_unit_init_cfg_t unit_cfg = {};
    unit_cfg.unit_id = unit_;
    unit_cfg.clk_src = static_cast<adc_oneshot_clk_src_t>(0);
    unit_cfg.ulp_mode = ADC_ULP_MODE_DISABLE;

    if (adc_oneshot_new_unit(&unit_cfg, &unit_handle_) != ESP_OK)
    {
      return;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = attenuation_,
        .bitwidth = bitwidth_,
    };

    for (uint8_t i = 0; i < num_channels_; ++i)
    {
      if (adc_oneshot_config_channel(unit_handle_, channel_ids_[i], &chan_cfg) != ESP_OK)
      {
        continue;
      }

      channel_ready_[i] = true;

      int io_num = -1;
      if ((adc_oneshot_channel_to_io(unit_, channel_ids_[i], &io_num) == ESP_OK) &&
          (io_num >= 0))
      {
        (void)gpio_config_as_analog(static_cast<gpio_num_t>(io_num));
      }
    }
  }

  ~ESP32ADC()
  {
    if (unit_handle_ != nullptr)
    {
      (void)adc_oneshot_del_unit(unit_handle_);
      unit_handle_ = nullptr;
    }

    delete[] channels_;
    delete[] channel_ids_;
    delete[] channel_ready_;
    delete[] latest_values_;
  }

  Channel& GetChannel(uint8_t idx) { return channels_[idx]; }

  float ReadChannel(uint8_t idx)
  {
    if (idx >= num_channels_)
    {
      return 0.f;
    }

    if ((unit_handle_ == nullptr) || !channel_ready_[idx])
    {
      return latest_values_[idx];
    }

    int raw = 0;
    if (adc_oneshot_read(unit_handle_, channel_ids_[idx], &raw) == ESP_OK)
    {
      latest_values_[idx] = Normalize(static_cast<float>(raw));
    }

    return latest_values_[idx];
  }

 private:
  static adc_bitwidth_t ResolveBitwidth(adc_bitwidth_t bitwidth)
  {
    if (bitwidth == ADC_BITWIDTH_DEFAULT)
    {
      return static_cast<adc_bitwidth_t>(SOC_ADC_RTC_MAX_BITWIDTH);
    }
    return bitwidth;
  }

  float Normalize(float raw) const
  {
    return (raw / static_cast<float>(max_raw_)) * reference_voltage_;
  }

  Channel* channels_ = nullptr;
  adc_channel_t* channel_ids_ = nullptr;
  bool* channel_ready_ = nullptr;
  float* latest_values_ = nullptr;

  adc_unit_t unit_;
  uint8_t num_channels_;
  adc_atten_t attenuation_;
  adc_bitwidth_t bitwidth_;
  float reference_voltage_;
  uint16_t max_raw_;

  adc_oneshot_unit_handle_t unit_handle_ = nullptr;
};

}  // namespace LibXR

