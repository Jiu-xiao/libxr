#pragma once

#include "adc.hpp"
#include "esp_adc/adc_oneshot.h"
#include "hal/adc_types.h"

namespace LibXR
{

class ESP32ADC : public ADC
{
 public:
  explicit ESP32ADC(
      adc_unit_t unit, adc_channel_t channel, adc_atten_t attenuation = ADC_ATTEN_DB_12,
      adc_bitwidth_t bitwidth = static_cast<adc_bitwidth_t>(SOC_ADC_DIGI_MIN_BITWIDTH),
      float reference_voltage = 3.3f)
      : m_unit_(unit),
        m_channel_(channel),
        m_attenuation_(attenuation),
        m_bitwidth_(bitwidth),
        m_reference_voltage_(reference_voltage),
        m_max_raw_((1 << bitwidth) - 1)
  {
    adc_oneshot_unit_init_cfg_t unit_cfg = {};
    unit_cfg.unit_id = m_unit_;
    unit_cfg.ulp_mode = ADC_ULP_MODE_DISABLE;

    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &m_oneshot_));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = m_attenuation_,
        .bitwidth = m_bitwidth_,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(m_oneshot_, m_channel_, &chan_cfg));
  }

  ~ESP32ADC()
  {
    if (m_oneshot_)
    {
      adc_oneshot_del_unit(m_oneshot_);
    }
  }

  float Read() override
  {
    int raw = 0;
    if (adc_oneshot_read(m_oneshot_, m_channel_, &raw) == ESP_OK)
    {
      return Normalize(raw);
    }
    return 0.0f;
  }

 private:
  float Normalize(int raw) const
  {
    return (static_cast<float>(raw) / static_cast<float>(m_max_raw_)) *
           m_reference_voltage_;
  }

  adc_unit_t m_unit_;
  adc_channel_t m_channel_;
  adc_atten_t m_attenuation_;
  adc_bitwidth_t m_bitwidth_;
  float m_reference_voltage_;
  uint16_t m_max_raw_;
  adc_oneshot_unit_handle_t m_oneshot_ = nullptr;
};

}  // namespace LibXR
