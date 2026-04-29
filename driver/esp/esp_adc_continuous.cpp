#include <array>
#include <new>

#include "esp_adc.hpp"

namespace LibXR
{

#if SOC_ADC_DIG_CTRL_SUPPORTED && SOC_ADC_DMA_SUPPORTED

bool ESP32ADC::IsDigiUnitSupported(adc_unit_t unit)
{
  return SOC_ADC_DIG_SUPPORTED_UNIT(static_cast<int>(unit)) != 0;
}

adc_digi_output_format_t ESP32ADC::ResolveContinuousFormat()
{
#if SOC_ADC_DIGI_RESULT_BYTES == 2
  return ADC_DIGI_OUTPUT_FORMAT_TYPE1;
#else
  return ADC_DIGI_OUTPUT_FORMAT_TYPE2;
#endif
}

void ESP32ADC::DrainContinuousFrames(uint32_t timeout_ms)
{
  if ((backend_ != Backend::CONTINUOUS_DMA) || (continuous_handle_ == nullptr) ||
      (continuous_read_buf_ == nullptr) || (continuous_parsed_buf_ == nullptr) ||
      (continuous_frame_size_ == 0U))
  {
    return;
  }

  std::array<uint32_t, SOC_ADC_MAX_CHANNEL_NUM> sums = {};
  std::array<uint32_t, SOC_ADC_MAX_CHANNEL_NUM> counts = {};
  bool first_read = true;
  bool any_sample = false;

  while (true)
  {
    uint32_t out_length = 0U;
    const esp_err_t read_err = adc_continuous_read(
        continuous_handle_, continuous_read_buf_, continuous_frame_size_, &out_length,
        first_read ? timeout_ms : 0U);
    first_read = false;

    if (read_err == ESP_ERR_TIMEOUT)
    {
      break;
    }

    ASSERT(read_err == ESP_OK);
    if (read_err != ESP_OK)
    {
      if (read_err == ESP_ERR_INVALID_STATE)
      {
        (void)adc_continuous_flush_pool(continuous_handle_);
      }
      break;
    }

    if (out_length == 0U)
    {
      break;
    }

    uint32_t parsed_count = 0U;
    const esp_err_t parse_err =
        adc_continuous_parse_data(continuous_handle_, continuous_read_buf_, out_length,
                                  continuous_parsed_buf_, &parsed_count);
    ASSERT(parse_err == ESP_OK);
    if (parse_err != ESP_OK)
    {
      break;
    }

    for (uint32_t i = 0; i < parsed_count; ++i)
    {
      const adc_continuous_data_t& sample = continuous_parsed_buf_[i];
      if (!sample.valid || (sample.unit != unit_))
      {
        continue;
      }

      const uint8_t sample_channel = static_cast<uint8_t>(sample.channel);
      if (sample_channel >= SOC_ADC_MAX_CHANNEL_NUM)
      {
        continue;
      }

      const uint8_t sample_idx = channel_idx_map_[sample_channel];
      if ((sample_idx == INVALID_CHANNEL_IDX) || (sample_idx >= num_channels_))
      {
        continue;
      }

      sums[sample_idx] += sample.raw_data;
      counts[sample_idx] += 1U;
      any_sample = true;
    }
  }

  if (!any_sample)
  {
    return;
  }

  for (uint8_t i = 0; i < num_channels_; ++i)
  {
    if (counts[i] == 0U)
    {
      continue;
    }

    const uint16_t raw = static_cast<uint16_t>(sums[i] / counts[i]);
    latest_raw_[i] = raw;
    latest_values_[i] = RawToVoltage(i, raw);
    channel_ready_[i] = true;
  }
}

ESP32ADC::ContinuousInitResult ESP32ADC::InitContinuous(uint32_t freq,
                                                        size_t dma_buf_size)
{
  if (!IsDigiUnitSupported(unit_))
  {
    return ContinuousInitResult::UNSUPPORTED;
  }

  const uint32_t sample_freq = ClampSampleFreq(freq);
  const uint32_t min_frame =
      static_cast<uint32_t>(num_channels_) * SOC_ADC_DIGI_RESULT_BYTES;
  const uint32_t frame_size = AlignUp(min_frame, SOC_ADC_DIGI_DATA_BYTES_PER_CONV);
  const uint32_t store_size =
      AlignUp(static_cast<uint32_t>((dma_buf_size > static_cast<size_t>(frame_size))
                                        ? dma_buf_size
                                        : frame_size * 5U),
              SOC_ADC_DIGI_DATA_BYTES_PER_CONV);
  const uint32_t parsed_capacity = frame_size / SOC_ADC_DIGI_RESULT_BYTES;

  continuous_read_buf_ = new (std::nothrow) uint8_t[frame_size];
  continuous_parsed_buf_ = new (std::nothrow) adc_continuous_data_t[parsed_capacity];
  ASSERT((continuous_read_buf_ != nullptr) && (continuous_parsed_buf_ != nullptr));
  if ((continuous_read_buf_ == nullptr) || (continuous_parsed_buf_ == nullptr))
  {
    return ContinuousInitResult::FAILED;
  }

  adc_continuous_handle_cfg_t handle_cfg = {};
  handle_cfg.max_store_buf_size = store_size;
  handle_cfg.conv_frame_size = frame_size;
  handle_cfg.flags.flush_pool = 1U;
  if (adc_continuous_new_handle(&handle_cfg, &continuous_handle_) != ESP_OK)
  {
    return ContinuousInitResult::FAILED;
  }

  std::array<adc_digi_pattern_config_t, SOC_ADC_PATT_LEN_MAX> patterns = {};
  ASSERT(num_channels_ <= patterns.size());
  if (num_channels_ > patterns.size())
  {
    return ContinuousInitResult::FAILED;
  }

  for (uint8_t i = 0; i < num_channels_; ++i)
  {
    patterns[i].atten = static_cast<uint8_t>(attenuation_);
    patterns[i].channel = static_cast<uint8_t>(channel_ids_[i]);
    patterns[i].unit = static_cast<uint8_t>(unit_);
    patterns[i].bit_width = static_cast<uint8_t>(bitwidth_);
    ConfigureAnalogPad(channel_ids_[i]);
    channel_ready_[i] = false;
    latest_values_[i] = 0.0f;
    latest_raw_[i] = 0U;
  }

  adc_continuous_config_t config = {};
  config.pattern_num = num_channels_;
  config.adc_pattern = patterns.data();
  config.sample_freq_hz = sample_freq;
  config.conv_mode =
      (unit_ == ADC_UNIT_2) ? ADC_CONV_SINGLE_UNIT_2 : ADC_CONV_SINGLE_UNIT_1;
  config.format = ResolveContinuousFormat();
  if (adc_continuous_config(continuous_handle_, &config) != ESP_OK)
  {
    return ContinuousInitResult::FAILED;
  }

  if (adc_continuous_start(continuous_handle_) != ESP_OK)
  {
    return ContinuousInitResult::FAILED;
  }

  continuous_frame_size_ = frame_size;
  continuous_prime_timeout_ms_ =
      ((1000U * static_cast<uint32_t>(num_channels_) * 2U) / sample_freq) + 5U;
  if (continuous_prime_timeout_ms_ < 5U)
  {
    continuous_prime_timeout_ms_ = 5U;
  }
  if (continuous_prime_timeout_ms_ > 50U)
  {
    continuous_prime_timeout_ms_ = 50U;
  }

  backend_ = Backend::CONTINUOUS_DMA;
  DrainContinuousFrames(continuous_prime_timeout_ms_);
  return ContinuousInitResult::STARTED;
}

#else

ESP32ADC::ContinuousInitResult ESP32ADC::InitContinuous(uint32_t, size_t)
{
  return ContinuousInitResult::UNSUPPORTED;
}

#endif

}  // namespace LibXR
