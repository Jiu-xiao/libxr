#pragma once

#include <atomic>
#include <cstdint>

#include "adc.hpp"
#include "esp_adc/adc_continuous.h"
#include "hal/adc_types.h"

#if (SOC_ADC_DIGI_RESULT_BYTES == 2)
#define XR_ADC_DMA_OUTPUT_TYPE ADC_DIGI_OUTPUT_FORMAT_TYPE1
#define XR_ADC_DMA_GET_CHANNEL(p_data) ((p_data)->type1.channel)
#define XR_ADC_DMA_GET_DATA(p_data) ((p_data)->type1.data)
#else
#define XR_ADC_DMA_OUTPUT_TYPE ADC_DIGI_OUTPUT_FORMAT_TYPE2
#define XR_ADC_DMA_GET_CHANNEL(p_data) ((p_data)->type2.channel)
#define XR_ADC_DMA_GET_DATA(p_data) ((p_data)->type2.data)
#endif

namespace LibXR
{

/**
 * @brief ESP32 ADC多通道驱动
 * @brief ESP32 multi-channel ADC driver
 *
 * 该类封装ESP32的ADC连续采样、DMA搬运和多通道均值计算。
 * 支持线程安全读取，适用于高采样率场景。
 *
 * This class encapsulates ESP32 ADC continuous sampling, DMA buffering,
 * and per-channel averaging. Thread-safe and suitable for high-speed applications.
 */
class ESP32ADC
{
 public:
  /**
   * @brief ADC通道对象，提供通道级数据访问和抽象接口。
   * @brief ADC channel object, offering per-channel access and interface.
   */
  class Channel : public ADC
  {
   public:
    /**
     * @brief 默认构造函数
     * Default constructor.
     */
    Channel() : parent_(nullptr), idx_(0), channel_num_(0) {}

    /**
     * @brief 构造指定父ADC和通道编号的通道对象
     * Construct with given parent ADC and channel numbers.
     *
     * @param parent 父ADC对象指针 / Parent ADC object pointer
     * @param idx 逻辑通道索引 / Logical channel index
     * @param channel_num 物理ADC通道号 / Physical ADC channel number
     */
    Channel(ESP32ADC *parent, uint8_t idx, uint8_t channel_num)
        : parent_(parent), idx_(idx), channel_num_(channel_num)
    {
    }

    /**
     * @brief 读取当前通道归一化电压值（单位：V）
     * Read normalized voltage (unit: V) for this channel.
     *
     * @return 电压值（V）/ Voltage value in volts
     */
    float Read() override { return parent_ ? parent_->ReadChannel(idx_) : 0.f; }

    /**
     * @brief 获取物理ADC通道号
     * Get the physical ADC channel number.
     *
     * @return 通道号 / Channel number
     */
    uint8_t ChannelNumber() const { return channel_num_; }

   private:
    ESP32ADC *parent_;     ///< 父ADC对象 / Parent ADC pointer
    uint8_t idx_;          ///< 逻辑通道号 / Logical index
    uint8_t channel_num_;  ///< 物理通道号 / Physical ADC channel number
  };

  /**
   * @brief 构造函数：初始化ADC与DMA、参数配置
   * Constructor: Initialize ADC, DMA, and config parameters.
   *
   * @param unit ADC单元（ADC_UNIT_1或2）/ ADC unit number (ADC_UNIT_1 or 2)
   * @param channels ADC通道号数组 / Array of ADC channel numbers
   * @param num_channels 通道数量 / Number of channels
   * @param freq 采样频率（Hz）/ Sample frequency in Hz (default:
   * SOC_ADC_SAMPLE_FREQ_THRES_LOW)
   * @param attenuation 衰减档位 / ADC attenuation (default: ADC_ATTEN_DB_12)
   * @param bitwidth 精度位宽 / Bit width (default: chip max)
   * @param reference_voltage 参考电压（V）/ Reference voltage (default: 3.3V)
   * @param dma_buf_size DMA缓冲区大小 / DMA buffer size in bytes (default: 256)
   */
  ESP32ADC(
      adc_unit_t unit, const adc_channel_t *channels, uint8_t num_channels,
      uint32_t freq = SOC_ADC_SAMPLE_FREQ_THRES_LOW,
      adc_atten_t attenuation = ADC_ATTEN_DB_12,
      adc_bitwidth_t bitwidth = static_cast<adc_bitwidth_t>(SOC_ADC_DIGI_MAX_BITWIDTH),
      float reference_voltage = 3.3f, size_t dma_buf_size = 256)
      : m_unit_(unit),
        m_num_channels_(num_channels),
        m_attenuation_(attenuation),
        m_bitwidth_(bitwidth),
        m_reference_voltage_(reference_voltage),
        m_max_raw_((1 << bitwidth) - 1)
  {
    m_patterns_ = new adc_digi_pattern_config_t[m_num_channels_];
    m_channels_ = new Channel[m_num_channels_];
    m_latest_values_ = new float[m_num_channels_];
    m_sum_buf_ = new int[m_num_channels_];
    m_cnt_buf_ = new int[m_num_channels_];

    for (uint8_t i = 0; i < m_num_channels_; ++i)
    {
      m_patterns_[i].atten = static_cast<uint8_t>(m_attenuation_);
      m_patterns_[i].channel = channels[i];
      m_patterns_[i].unit = static_cast<uint8_t>(m_unit_);
      m_patterns_[i].bit_width = static_cast<uint8_t>(m_bitwidth_);
      m_channels_[i] = Channel(this, i, channels[i]);
      m_latest_values_[i] = 0.f;
    }

    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = dma_buf_size,
        .conv_frame_size = dma_buf_size / 2,
        // .flags = {.flush_pool = 1},
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &m_handle_));

    adc_continuous_config_t dig_cfg = {};
    dig_cfg.sample_freq_hz = freq;
    if (m_unit_ == ADC_UNIT_1)
    {
      dig_cfg.conv_mode = ADC_CONV_SINGLE_UNIT_1;
    }
    else
    {
      dig_cfg.conv_mode = ADC_CONV_SINGLE_UNIT_2;
    }
    dig_cfg.format = XR_ADC_DMA_OUTPUT_TYPE;
    dig_cfg.adc_pattern = m_patterns_;
    dig_cfg.pattern_num = m_num_channels_;
    ESP_ERROR_CHECK(adc_continuous_config(m_handle_, &dig_cfg));

    adc_continuous_evt_cbs_t cbs = {
        .on_conv_done = &ESP32ADC::OnConvDone,
    };
    ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(m_handle_, &cbs, this));
    ESP_ERROR_CHECK(adc_continuous_start(m_handle_));
  }

  /**
   * @brief 析构函数，释放所有资源
   * Destructor. Free all resources and stop ADC.
   */
  ~ESP32ADC()
  {
    if (m_handle_)
    {
      adc_continuous_stop(m_handle_);
      adc_continuous_deinit(m_handle_);
    }
    delete[] m_patterns_;
    delete[] m_channels_;
    delete[] m_latest_values_;
    delete[] m_sum_buf_;
    delete[] m_cnt_buf_;
  }

  /**
   * @brief 获取通道对象的引用
   * Get a reference to channel object by index.
   * @param idx 逻辑通道索引 / Channel logical index
   * @return Channel对象引用 / Reference to Channel object
   */
  Channel &GetChannel(uint8_t idx) { return m_channels_[idx]; }

  /**
   * @brief 读取指定通道最新均值（已归一化为电压）
   * Read latest averaged and normalized value (in volts) for the given channel.
   * @param idx 逻辑通道索引 / Channel index
   * @return 电压值（V）/ Voltage value (V)
   */
  float ReadChannel(uint8_t idx) { return m_latest_values_[idx]; }

 private:
  /**
   * @brief DMA采集完成中断回调。内部调用。
   * DMA conversion done callback. Called internally by ESP-IDF.
   */
  static bool IRAM_ATTR OnConvDone(adc_continuous_handle_t handle,
                                   const adc_continuous_evt_data_t *edata,
                                   void *user_data)
  {
    auto *self = reinterpret_cast<ESP32ADC *>(user_data);
    self->HandleSamples(edata->conv_frame_buffer, edata->size);
    return false;
  }

  /**
   * @brief 采样缓冲区数据解析与均值计算
   * Parse DMA buffer and calculate channel averages.
   *
   * @param buf 缓冲区指针 / Buffer pointer
   * @param size_bytes 数据字节数 / Buffer size in bytes
   */
  void HandleSamples(const void *buf, size_t size_bytes)
  {
    for (uint8_t idx = 0; idx < m_num_channels_; ++idx)
    {
      m_sum_buf_[idx] = 0;
      m_cnt_buf_[idx] = 0;
    }

    size_t n = size_bytes / sizeof(adc_digi_output_data_t);
    const adc_digi_output_data_t *samples =
        static_cast<const adc_digi_output_data_t *>(buf);

    for (size_t i = 0; i < n; ++i)
    {
      uint8_t ch = XR_ADC_DMA_GET_CHANNEL(&samples[i]);
      for (uint8_t idx = 0; idx < m_num_channels_; ++idx)
      {
        if (ch == m_channels_[idx].ChannelNumber())
        {
          int raw = XR_ADC_DMA_GET_DATA(&samples[i]);
          m_sum_buf_[idx] += raw;
          m_cnt_buf_[idx] += 1;
          break;
        }
      }
    }

    for (uint8_t idx = 0; idx < m_num_channels_; ++idx)
    {
      if (m_cnt_buf_[idx] > 0)
      {
        float avg = Normalize(static_cast<float>(m_sum_buf_[idx]) / m_cnt_buf_[idx]);
        m_latest_values_[idx] = avg;
      }
    }
  }

  /**
   * @brief 原始ADC值归一化到电压
   * Normalize raw ADC value to voltage.
   *
   * @param raw 原始采样值 / Raw ADC value
   * @return 电压（V）/ Voltage (V)
   */
  float Normalize(float raw) const
  {
    return (raw / static_cast<float>(m_max_raw_)) * m_reference_voltage_;
  }

  adc_digi_pattern_config_t *m_patterns_;  ///< ADC采样模式数组 / Pattern config array
  Channel *m_channels_;                    ///< 通道对象数组 / Channel objects array
  float *m_latest_values_;  ///< 最新均值（每通道）/ Latest average value (per channel)
  int *m_sum_buf_;          ///< 求和缓冲 / Accumulation buffer
  int *m_cnt_buf_;          ///< 计数缓冲 / Count buffer

  adc_unit_t m_unit_;                           ///< ADC单元 / ADC unit
  uint8_t m_num_channels_;                      ///< 通道数 / Number of channels
  adc_atten_t m_attenuation_;                   ///< 衰减档位 / Attenuation
  adc_bitwidth_t m_bitwidth_;                   ///< 位宽 / Bit width
  float m_reference_voltage_;                   ///< 参考电压 / Reference voltage
  uint16_t m_max_raw_;                          ///< 最大原始采样值 / Max raw value
  adc_continuous_handle_t m_handle_ = nullptr;  ///< ESP-IDF采集句柄 / ADC handle
};

}  // namespace LibXR
