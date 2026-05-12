#pragma once

#include "esp_def.hpp"

#include <cstddef>
#include <cstdint>

#include "adc.hpp"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"
#include "hal/adc_types.h"
#include "soc/soc_caps.h"

#if SOC_ADC_DIG_CTRL_SUPPORTED && SOC_ADC_DMA_SUPPORTED
#include "esp_adc/adc_continuous.h"
#endif

struct adc_oneshot_hal_ctx_t;

namespace LibXR
{

class ESP32ADC
{
 public:
  class Channel : public ADC
  {
   public:
    Channel();
    Channel(ESP32ADC* parent, uint8_t idx, uint8_t channel_num);

    float Read() override;

    uint8_t ChannelNumber() const { return channel_num_; }

   private:
    ESP32ADC* parent_;
    uint8_t idx_;
    uint8_t channel_num_;
  };

  enum class ContinuousInitResult : uint8_t
  {
    STARTED = 0,
    UNSUPPORTED,
    FAILED,
  };

  ESP32ADC(
      adc_unit_t unit, const adc_channel_t* channels, uint8_t num_channels,
      uint32_t freq = SOC_ADC_SAMPLE_FREQ_THRES_LOW,
      adc_atten_t attenuation = ADC_ATTEN_DB_12,
      adc_bitwidth_t bitwidth =
          static_cast<adc_bitwidth_t>(SOC_ADC_DIGI_MAX_BITWIDTH),
      float reference_voltage = 3.3f, size_t dma_buf_size = 256);


  Channel& GetChannel(uint8_t idx);

  float ReadChannel(uint8_t idx);

 private:
  enum class Backend : uint8_t
  {
    NONE = 0,
    CONTINUOUS_DMA,
    ONESHOT,
  };

  static adc_bitwidth_t ResolveBitwidth(adc_bitwidth_t bitwidth);
  float Normalize(float raw) const;
  static uint32_t ClampSampleFreq(uint32_t freq);
  static uint32_t AlignUp(uint32_t value, uint32_t align);
  bool IsValidChannel(adc_channel_t channel) const;
  void ConfigureAnalogPad(adc_channel_t channel) const;
  bool InitCalibration();
  bool InitOneshot();
  float RawToVoltage(uint8_t idx, uint16_t raw) const;

#if SOC_ADC_DIG_CTRL_SUPPORTED && SOC_ADC_DMA_SUPPORTED
  static bool IsDigiUnitSupported(adc_unit_t unit);
  static adc_digi_output_format_t ResolveContinuousFormat();
  void DrainContinuousFrames(uint32_t timeout_ms);
  ContinuousInitResult InitContinuous(uint32_t freq, size_t dma_buf_size);
#endif

  Channel* channels_ = nullptr;
  adc_channel_t* channel_ids_ = nullptr;
  uint8_t channel_idx_map_[SOC_ADC_MAX_CHANNEL_NUM] = {};
  bool* channel_ready_ = nullptr;
  float* latest_values_ = nullptr;
  uint16_t* latest_raw_ = nullptr;

  adc_cali_handle_t cali_handles_[SOC_ADC_MAX_CHANNEL_NUM] = {};

  adc_unit_t unit_;
  uint8_t num_channels_;
  adc_atten_t attenuation_;
  adc_bitwidth_t bitwidth_;
  float reference_voltage_;
  uint16_t max_raw_;
  static constexpr uint8_t INVALID_CHANNEL_IDX = 0xFFU;

  Backend backend_ = Backend::NONE;
  adc_oneshot_hal_ctx_t* oneshot_hal_ = nullptr;
  bool oneshot_inited_ = false;
  bool valid_ = false;

#if SOC_ADC_DIG_CTRL_SUPPORTED && SOC_ADC_DMA_SUPPORTED
  adc_continuous_handle_t continuous_handle_ = nullptr;
  uint8_t* continuous_read_buf_ = nullptr;
  adc_continuous_data_t* continuous_parsed_buf_ = nullptr;
  uint32_t continuous_frame_size_ = 0U;
  uint32_t continuous_prime_timeout_ms_ = 0U;
#endif
};

}  // namespace LibXR
