#pragma once

#include <ti/driverlib/dl_adc12.h>

#include "adc.hpp"
#include "ti_msp_dl_config.h"

#if defined(__MSPM0_HAS_ADC12__)

namespace LibXR
{

class MSPM0ADC : public ADC
{
 public:
  struct Resources
  {
    ADC12_Regs* instance;
    DL_ADC12_MEM_IDX mem_idx;
    float vref;
  };

  explicit MSPM0ADC(Resources res);

  float Read() override;

 private:
  static constexpr uint8_t DMA_CHANNEL_INVALID = 0xFF;

  float ReadByPolling();

  float ReadByDMA();

  Resources res_;
  float scale_;
  bool use_dma_;
  uint8_t dma_channel_id_;
  uint16_t dma_sample_;
};

#define MSPM0_ADC_INIT(name)                                                        \
  ::LibXR::MSPM0ADC::Resources                                                      \
  {                                                                                 \
    name##_INST, name##_ADCMEM_0, static_cast<float>(name##_ADCMEM_0_REF_VOLTAGE_V) \
  }

}  // namespace LibXR

#endif
