#pragma once

#include <initializer_list>

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

  template <typename T, typename = void>
  struct HasContinuousConvMode : std::false_type
  {
  };
  template <typename T>
  struct HasContinuousConvMode<
      T, std::void_t<decltype(std::declval<T>()->Init.ContinuousConvMode)>>
      : std::true_type
  {
  };

  template <typename T, typename = void>
  struct HasDMAContinuousRequests : std::false_type
  {
  };
  template <typename T>
  struct HasDMAContinuousRequests<
      T, std::void_t<decltype(std::declval<T>()->Init.DMAContinuousRequests)>>
      : std::true_type
  {
  };

  template <typename T, typename = void>
  struct HasNbrOfConversion : std::false_type
  {
  };
  template <typename T>
  struct HasNbrOfConversion<
      T, std::void_t<decltype(std::declval<T>()->Init.NbrOfConversion)>> : std::true_type
  {
  };

  template <typename T, typename = void>
  struct HasDMACircularMode : std::false_type
  {
  };
  template <typename T>
  struct HasDMACircularMode<
      T, std::void_t<decltype(std::declval<T>()->DMA_Handle->Init.Mode)>> : std::true_type
  {
  };

  template <typename T>
  static typename std::enable_if<HasContinuousConvMode<T>::value>::type
  AssertContinuousConvModeEnabled(T hadc)
  {
    ASSERT(hadc->Init.ContinuousConvMode == ENABLE);
  }
  template <typename T>
  static typename std::enable_if<!HasContinuousConvMode<T>::value>::type
  AssertContinuousConvModeEnabled(T)
  {
  }

  template <typename T>
  static typename std::enable_if<HasContinuousConvMode<T>::value>::type
  AssertContinuousConvModeDisabled(T hadc)
  {
    ASSERT(hadc->Init.ContinuousConvMode == DISABLE);
  }
  template <typename T>
  static typename std::enable_if<!HasContinuousConvMode<T>::value>::type
  AssertContinuousConvModeDisabled(T)
  {
  }

  template <typename T>
  static typename std::enable_if<HasDMAContinuousRequests<T>::value>::type
  AssertDMAContReqEnabled(T hadc)
  {
    ASSERT(hadc->Init.DMAContinuousRequests == ENABLE);
  }
  template <typename T>
  static typename std::enable_if<!HasDMAContinuousRequests<T>::value>::type
  AssertDMAContReqEnabled(T)
  {
  }

  template <typename T>
  static typename std::enable_if<HasNbrOfConversion<T>::value>::type AssertNbrOfConvEq(
      T hadc, uint32_t n)
  {
    ASSERT(hadc->Init.NbrOfConversion == n);
  }
  template <typename T>
  static typename std::enable_if<!HasNbrOfConversion<T>::value>::type AssertNbrOfConvEq(
      T, uint32_t)
  {
  }

  template <typename T>
  static typename std::enable_if<HasDMACircularMode<T>::value>::type AssertDMACircular(
      T hadc)
  {
    ASSERT(hadc->DMA_Handle != nullptr);
    ASSERT(hadc->DMA_Handle->Init.Mode == DMA_CIRCULAR);
  }
  template <typename T>
  static typename std::enable_if<!HasDMACircularMode<T>::value>::type AssertDMACircular(T)
  {
  }

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
    Channel(STM32ADC* adc, uint8_t index, uint32_t ch);

    /**
     * @brief 读取 ADC 值
     * @brief Reads the ADC value
     *
     * @return float
     */
    float Read() override;

   private:
    Channel();
    STM32ADC* adc_;
    uint8_t index_;
    uint32_t ch_;

    friend class STM32ADC;
  };

  /**
   * @brief STM32ADC 类，用于处理 STM32 系统的 ADC 通道。 Provides handling for STM32
   *
   * @param hadc ADC外设 ADC device
   * @param dma_buff DMA缓冲区 DMA buffer
   * @param channels ADC通道列表 List of ADC channels
   * @param vref 参考电压 Reference voltage
   */
  STM32ADC(ADC_HandleTypeDef* hadc, RawData dma_buff,
           std::initializer_list<uint32_t> channels, float vref);

  /**
   * @brief 析构函数 Destructor
   */
  ~STM32ADC();

  /**
   * @brief 获取 ADC 通道对象 Get ADC channel object
   *
   * @param index 通道索引 Channel index
   * @return Channel& 通道对象 Channel object
   */
  Channel& GetChannel(uint8_t index);

  /**
   * @brief 读取 ADC 值
   *
   * @param channel 通道号 Channel number
   * @return float
   */
  float ReadChannel(uint8_t channel);

 private:
  alignas(LIBXR_CACHE_LINE_SIZE) std::atomic<uint32_t> locked_ = 0U;
  ADC_HandleTypeDef* hadc_;
  const uint8_t NUM_CHANNELS;
  uint8_t filter_size_;
  bool use_dma_;
  RawData dma_buffer_;
  float resolution_;
  Channel** channels_;
  float vref_;

  float ConvertToVoltage(float adc_value);
};

}  // namespace LibXR

#endif
