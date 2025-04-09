#pragma once

#include "main.h"

#ifdef HAL_ADC_MODULE_ENABLED

#ifdef ADC
#undef ADC
#endif

#include "adc.hpp"
#include "libxr.hpp"

namespace LibXR
{

/**
 * @brief STM32ADC 类，用于处理 STM32 系统的 ADC 通道。 Provides handling for STM32 ADC
 * channels.
 *
 */
class STM32ADC
{
  /**
   * @brief 获取 ADC 分辨率 Get ADC resolution
   *
   * @tparam typename
   * @tparam typename
   */
  template <typename, typename = void>
  struct GetADCResolution
  {
    float Get(ADC_HandleTypeDef* hadc)
    {
      UNUSED(hadc);
      return 4095.0f;
    }
  };

  /**
   * @brief 获取 ADC 分辨率 Get ADC resolution
   *
   * @tparam T
   */
  template <typename T>
  struct GetADCResolution<T, std::void_t<decltype(std::declval<T>().Init.Resolution)>>
  {
    float Get(T* hadc)
    {
      switch (hadc->Init.Resolution)
      {
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
  /**
   * @brief STM32ADC 类，用于处理 STM32 系统的 ADC 通道。 Provides handling for STM32 ADC
   *
   */
  class Channel : public ADC
  {
   public:
    /**
     * @brief STM32ADC 类，用于处理 STM32 系统的 ADC 通道。 Provides handling for STM32
     * ADC
     *
     * @param adc STM32ADC对象 ADC object
     * @param index 通道索引 Channel index
     * @param ch 通道号 Channel number
     */
    Channel(STM32ADC* adc, uint8_t index, uint32_t ch) : adc_(adc), index_(index), ch_(ch)
    {
    }

    /**
     * @brief 读取 ADC 值
     * @brief Reads the ADC value
     *
     * @return float
     */
    float Read() override { return adc_->ReadChannel(index_); }

   private:
    Channel() {};
    STM32ADC* adc_;
    uint8_t index_;
    uint32_t ch_;

    friend class STM32ADC;
  };

  /**
   * @brief STM32ADC 类，用于处理 STM32 系统的 ADC 通道。 Provides handling for STM32
   *
   * @tparam NumChannels 通道数量 Number of channels
   * @param hadc ADC外设 ADC device
   * @param dma_buff DMA缓冲区 DMA buffer
   * @param channels 通道号 Channel number
   * @param vref 参考电压 Reference voltage
   */
  template <size_t NumChannels>
  STM32ADC(ADC_HandleTypeDef* hadc, RawData dma_buff,
           const std::array<uint32_t, NumChannels>& channels, float vref = 3.3f)
      : hadc_(hadc),
        NUM_CHANNELS(NumChannels),
        filter_size_(dma_buff.size_ / NumChannels / 2),
        use_dma_(hadc_->DMA_Handle != nullptr),
        dma_buffer_(dma_buff),
        resolution_(GetADCResolution<ADC_HandleTypeDef>{}.Get(hadc)),
        channels_(new Channel[NumChannels]),
        vref_(vref)
  {
    for (uint8_t i = 0; i < NUM_CHANNELS; ++i)
    {
      channels_[i] = Channel(this, i, channels[i]);
    }

    use_dma_ ? HAL_ADC_Start_DMA(hadc_, reinterpret_cast<uint32_t*>(dma_buffer_.addr_),
                                 NUM_CHANNELS * filter_size_)
             : HAL_ADC_Start(hadc_);
  }

  ~STM32ADC()
  {
    use_dma_ ? HAL_ADC_Stop_DMA(hadc_) : HAL_ADC_Stop(hadc_);
    delete[] channels_;
  }

  /**
   * @brief 获取 ADC 通道对象 Get ADC channel object
   *
   * @param index 通道索引 Channel index
   * @return Channel& 通道对象 Channel object
   */
  Channel& GetChannel(uint8_t index) { return channels_[index]; }

  /**
   * @brief 读取 ADC 值
   *
   * @param channel 通道号 Channel number
   * @return float
   */
  float ReadChannel(uint8_t channel)
  {
    if (channel >= NUM_CHANNELS)
    {
      ASSERT(false);
      return -1.0f;
    }

    uint16_t* buffer = reinterpret_cast<uint16_t*>(dma_buffer_.addr_);
    if (use_dma_)
    {
      uint32_t sum = 0;
      for (uint8_t i = 0; i < filter_size_; ++i)
      {
        sum += buffer[channel + i * NUM_CHANNELS];
      }
      return ConvertToVoltage(static_cast<float>(sum) / static_cast<float>(filter_size_));
    }

    ADC_ChannelConfTypeDef config = {};
    uint32_t time = 0;
    UNUSED(time);
#if defined(ADC_SAMPLETIME_16CYCLES)
    time = ADC_SAMPLETIME_16CYCLES;
#elif defined(ADC_SAMPLETIME_16CYCLES_5)
    time = ADC_SAMPLETIME_16CYCLES_5;
#elif defined(ADC_SAMPLETIME_17CYCLES)
    time = ADC_SAMPLETIME_17CYCLES;
#elif defined(ADC_SAMPLETIME_17CYCLES_5)
    time = ADC_SAMPLETIME_17CYCLES_5;
#elif defined(ADC_SAMPLETIME_18CYCLES)
    time = ADC_SAMPLETIME_18CYCLES;
#elif defined(ADC_SAMPLETIME_18CYCLES_5)
    time = ADC_SAMPLETIME_18CYCLES_5;
#elif defined(ADC_SAMPLETIME_19CYCLES)
    time = ADC_SAMPLETIME_19CYCLES;
#elif defined(ADC_SAMPLETIME_19CYCLES_5)
    time = ADC_SAMPLETIME_19CYCLES_5;
#elif defined(ADC_SAMPLETIME_20CYCLES)
    time = ADC_SAMPLETIME_20CYCLES;
#elif defined(ADC_SAMPLETIME_20CYCLES_5)
    time = ADC_SAMPLETIME_20CYCLES_5;
#elif defined(ADC_SAMPLETIME_21CYCLES)
    time = ADC_SAMPLETIME_21CYCLES;
#elif defined(ADC_SAMPLETIME_21CYCLES_5)
    time = ADC_SAMPLETIME_21CYCLES_5;
#elif defined(ADC_SAMPLETIME_22CYCLES)
    time = ADC_SAMPLETIME_22CYCLES;
#elif defined(ADC_SAMPLETIME_22CYCLES_5)
    time = ADC_SAMPLETIME_22CYCLES_5;
#elif defined(ADC_SAMPLETIME_23CYCLES)
    time = ADC_SAMPLETIME_23CYCLES;
#elif defined(ADC_SAMPLETIME_23CYCLES_5)
    time = ADC_SAMPLETIME_23CYCLES_5;
#elif defined(ADC_SAMPLETIME_24CYCLES)
    time = ADC_SAMPLETIME_24CYCLES;
#elif defined(ADC_SAMPLETIME_24CYCLES_5)
    time = ADC_SAMPLETIME_24CYCLES_5;
#elif defined(ADC_SAMPLETIME_25CYCLES)
    time = ADC_SAMPLETIME_25CYCLES;
#elif defined(ADC_SAMPLETIME_25CYCLES_5)
    time = ADC_SAMPLETIME_25CYCLES_5;
#elif defined(ADC_SAMPLETIME_26CYCLES)
    time = ADC_SAMPLETIME_26CYCLES;
#elif defined(ADC_SAMPLETIME_26CYCLES_5)
    time = ADC_SAMPLETIME_26CYCLES_5;
#elif defined(ADC_SAMPLETIME_27CYCLES)
    time = ADC_SAMPLETIME_27CYCLES;
#elif defined(ADC_SAMPLETIME_27CYCLES_5)
    time = ADC_SAMPLETIME_27CYCLES_5;
#elif defined(ADC_SAMPLETIME_28CYCLES)
    time = ADC_SAMPLETIME_28CYCLES;
#elif defined(ADC_SAMPLETIME_28CYCLES_5)
    time = ADC_SAMPLETIME_28CYCLES_5;
#elif defined(ADC_SAMPLETIME_29CYCLES)
    time = ADC_SAMPLETIME_29CYCLES;
#elif defined(ADC_SAMPLETIME_29CYCLES_5)
    time = ADC_SAMPLETIME_29CYCLES_5;
#elif defined(ADC_SAMPLETIME_30CYCLES)
    time = ADC_SAMPLETIME_30CYCLES;
#elif defined(ADC_SAMPLETIME_30CYCLES_5)
    time = ADC_SAMPLETIME_30CYCLES_5;
#elif defined(ADC_SAMPLETIME_31CYCLES)
    time = ADC_SAMPLETIME_31CYCLES;
#elif defined(ADC_SAMPLETIME_31CYCLES_5)
    time = ADC_SAMPLETIME_31CYCLES_5;
#elif defined(ADC_SAMPLETIME_32CYCLES)
    time = ADC_SAMPLETIME_32CYCLES;
#elif defined(ADC_SAMPLETIME_32CYCLES_5)
    time = ADC_SAMPLETIME_32CYCLES_5;
#error "Unsupported sample time"
#endif

    config.Channel = channels_[channel].ch_;
    config.Rank = 1;
#if !defined(STM32L0)
    config.SamplingTime = time;
#endif

    HAL_ADC_ConfigChannel(hadc_, &config);

    uint32_t sum = 0;
    for (uint8_t i = 0; i < filter_size_; ++i)
    {
      HAL_ADC_Start(hadc_);
      HAL_ADC_PollForConversion(hadc_, 20);
      buffer[channel + i * NUM_CHANNELS] = HAL_ADC_GetValue(hadc_);
      sum += buffer[channel + i * NUM_CHANNELS];
    }
    return ConvertToVoltage(static_cast<float>(sum) / static_cast<float>(filter_size_));
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

  float ConvertToVoltage(float adc_value) { return adc_value * vref_ / resolution_; }
};

}  // namespace LibXR

#endif
