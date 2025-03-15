#pragma once

#include "main.h"

#ifdef HAL_ADC_MODULE_ENABLED

#ifdef ADC
#undef ADC
#endif

#include "adc.hpp"
#include "libxr.hpp"

namespace LibXR {

class STM32ADC {
  template <typename, typename = void>
  struct GetADCResolution {
    float Get() { return 4095.0f; }
  };

  template <typename T>
  struct GetADCResolution<
      T, std::void_t<decltype(std::declval<T>().Init.Resolution)>> {
    float Get(T* hadc) {
      switch (hadc->Init.Resolution) {
#ifdef ADC_RESOLUTION_16B
        case ADC_RESOLUTION_16B:
          return 65535.0f;
#endif
#ifdef ADC_RESOLUTION_12B
        case ADC_RESOLUTION_12B:
          return 4095.0f;
#endif
#ifdef ADC_RESOLUTION_10B
        case ADC_RESOLUTION_10B:
          return 1023.0f;
#endif
#ifdef ADC_RESOLUTION_8B
        case ADC_RESOLUTION_8B:
          return 255.0f;
#endif
#ifdef ADC_RESOLUTION_6B
        case ADC_RESOLUTION_6B:
          return 63.0f;
#endif
        default:
          return 4095.0f;
      }
    }
  };

 public:
  class Channel {
   public:
    Channel(STM32ADC* adc, uint8_t index, uint32_t ch)
        : adc_(adc), index_(index), ch_(ch) {}

    float Read() { return adc_->ReadChannel(index_); }

   private:
    Channel() {};
    STM32ADC* adc_;
    uint8_t index_;
    uint32_t ch_;

    friend class STM32ADC;
  };

  template <size_t NumChannels>
  STM32ADC(ADC_HandleTypeDef* hadc, RawData dma_buff,
           const std::array<uint32_t, NumChannels>& channels, float vref = 3.3f)
      : hadc_(hadc),
        NUM_CHANNELS(NumChannels),
        filter_size_(dma_buff.size_ / NumChannels / 2),
        use_dma_(hadc_->DMA_Handle != nullptr),
        dma_buffer_(dma_buff),
        resolution_(GetADCResolution<ADC_HandleTypeDef>{}.Get()),
        channels_(new Channel[NumChannels]),
        vref_(vref) {
    for (uint8_t i = 0; i < NUM_CHANNELS; ++i) {
      channels_[i] = Channel(this, i, channels[i]);
    }

    use_dma_ ? HAL_ADC_Start_DMA(hadc_,
                                 reinterpret_cast<uint32_t*>(dma_buffer_.addr_),
                                 NUM_CHANNELS * filter_size_)
             : HAL_ADC_Start(hadc_);
  }

  ~STM32ADC() {
    use_dma_ ? HAL_ADC_Stop_DMA(hadc_) : HAL_ADC_Stop(hadc_);
    delete[] channels_;
  }

  Channel& GetChannel(uint8_t index) { return channels_[index]; }

  float ReadChannel(uint8_t channel) {
    if (channel >= NUM_CHANNELS) {
      ASSERT(false);
      return -1.0f;
    }

    uint16_t* buffer = reinterpret_cast<uint16_t*>(dma_buffer_.addr_);
    if (use_dma_) {
      uint32_t sum = 0;
      for (uint8_t i = 0; i < filter_size_; ++i) {
        sum += buffer[channel + i * NUM_CHANNELS];
      }
      return ConvertToVoltage(static_cast<float>(sum) /
                              static_cast<float>(filter_size_));
    }

    ADC_ChannelConfTypeDef sConfig = {};
#ifdef ADC_SAMPLETIME_15CYCLES
    sConfig = {channels_[channel].ch_, 1, ADC_SAMPLETIME_15CYCLES};
#elif defined(ADC_SAMPLETIME_16CYCLES_5)
    sConfig = {channels_[channel].ch_, 1, ADC_SAMPLETIME_16CYCLES_5};
#elif defined(ADC_SAMPLETIME_13CYCLES_5)
    sConfig = {channels_[channel].ch_, 1, ADC_SAMPLETIME_13CYCLES_5};
#endif

    HAL_ADC_ConfigChannel(hadc_, &sConfig);

    uint32_t sum = 0;
    for (uint8_t i = 0; i < filter_size_; ++i) {
      HAL_ADC_Start(hadc_);
      HAL_ADC_PollForConversion(hadc_, HAL_MAX_DELAY);
      buffer[channel + i * NUM_CHANNELS] = HAL_ADC_GetValue(hadc_);
      sum += buffer[channel + i * NUM_CHANNELS];
    }
    return ConvertToVoltage(static_cast<float>(sum) /
                            static_cast<float>(filter_size_));
  }

 private:
  ADC_HandleTypeDef* hadc_;
  const uint8_t NUM_CHANNELS;
  uint8_t filter_size_;
  bool use_dma_;
  RawData dma_buffer_;
  float resolution_;
  Channel* channels_;
  float vref_;

  float ConvertToVoltage(float adc_value) {
    return adc_value * vref_ / resolution_;
  }
};

}  // namespace LibXR

#endif
