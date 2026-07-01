#pragma once

#include <ti/driverlib/dl_adc12.h>

#include <atomic>
#include <initializer_list>

#include "adc.hpp"
#include "ti_msp_dl_config.h"

#if defined(__MSPM0_HAS_ADC12__)

namespace LibXR
{

class MSPM0ADC : public ADC
{
 public:
  class Channel : public ADC
  {
   public:
    Channel();
    Channel(MSPM0ADC* adc, uint8_t index, DL_ADC12_MEM_IDX mem_idx);

    float Read() override;

    DL_ADC12_MEM_IDX MemIndex() const { return mem_idx_; }

   private:
    MSPM0ADC* adc_;
    uint8_t index_;
    DL_ADC12_MEM_IDX mem_idx_;
  };

  struct Resources
  {
    ADC12_Regs* instance;
    DL_ADC12_MEM_IDX mem_idx;
    float vref;
  };

  explicit MSPM0ADC(Resources res);
  MSPM0ADC(Resources res, RawData dma_buff,
           std::initializer_list<DL_ADC12_MEM_IDX> mem_indices);

  ~MSPM0ADC() override;

  float Read() override;

  Channel& GetChannel(uint8_t index);

  float ReadChannel(uint8_t channel);

 private:
  static constexpr uint8_t DMA_CHANNEL_INVALID = 0xFF;

  void Initialize(RawData dma_buff, std::initializer_list<DL_ADC12_MEM_IDX> mem_indices);

  void ConfigureSamplingMode(bool continuous, DL_ADC12_MEM_IDX start_mem_idx,
                             DL_ADC12_MEM_IDX end_mem_idx);

  float ReadByPolling(uint8_t channel);

  float ReadByDMA(uint8_t channel);

  void StartContinuousDMA();

  float ConvertToVoltage(float adc_value) const;

  Resources res_;
  float scale_;
  bool use_dma_;
  bool use_fifo_dma_;
  uint8_t dma_channel_id_;
  uint8_t num_channels_;
  uint8_t filter_size_;
  RawData dma_buffer_;
  Channel* channels_;
  DL_ADC12_MEM_IDX* mem_indices_;
  uint16_t single_dma_sample_;
  alignas(LibXR::CACHE_LINE_SIZE) std::atomic<uint32_t> locked_;
};

#define MSPM0_ADC_INIT(name)                                                        \
  ::LibXR::MSPM0ADC::Resources                                                      \
  {                                                                                 \
    name##_INST, name##_ADCMEM_0, static_cast<float>(name##_ADCMEM_0_REF_VOLTAGE_V) \
  }

}  // namespace LibXR

#endif
