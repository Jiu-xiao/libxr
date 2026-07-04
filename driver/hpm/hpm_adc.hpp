#pragma once

#include <atomic>
#include <initializer_list>

#include "adc.hpp"
#include "hpm_clock_drv.h"
#include "hpm_soc.h"
#include "libxr.hpp"
#include "libxr_def.hpp"

#if defined(HPMSOC_HAS_HPMSDK_ADC16) && __has_include("hpm_adc16_drv.h")
#define LIBXR_HPM_ADC_SUPPORTED 1
#include "hpm_adc16_drv.h"
#else
#define LIBXR_HPM_ADC_SUPPORTED 0
#endif

#if LIBXR_HPM_ADC_SUPPORTED

namespace LibXR
{

class HPMADC
{
 public:
  class Channel : public ADC
  {
   public:
    Channel(HPMADC* adc, uint8_t index, uint8_t ch);

    float Read() override;

    uint8_t ChannelNumber() const { return ch_; }

   private:
    Channel();

    HPMADC* adc_;
    uint8_t index_;
    uint8_t ch_;

    friend class HPMADC;
  };

  HPMADC(ADC16_Type* adc, clock_name_t clock, std::initializer_list<uint8_t> channels,
         float vref, uint32_t sample_cycle = 20U,
         adc16_resolution_t resolution = adc16_res_16_bits,
         adc16_clock_divider_t clock_divider = adc16_clock_divider_4,
         bool auto_board_init = true, uint8_t filter_size = 1U);

  Channel& GetChannel(uint8_t index);

  float ReadChannel(uint8_t channel);

  uint16_t GetLastRaw(uint8_t channel) const;

  bool IsValid() const { return valid_; }

 private:
  static float ResolutionMax(adc16_resolution_t resolution);

  bool Init(uint32_t sample_cycle, adc16_resolution_t resolution,
            adc16_clock_divider_t clock_divider, bool auto_board_init);

  float ConvertToVoltage(float adc_value) const;

  static constexpr uint8_t MAX_CHANNELS =
#ifdef ADC_SOC_MAX_CHANNEL_NUM
      static_cast<uint8_t>(ADC_SOC_MAX_CHANNEL_NUM);
#else
      32U;
#endif

  alignas(LibXR::CONCURRENCY_ALIGNMENT) std::atomic<uint32_t> locked_ = 0U;
  ADC16_Type* adc_;
  clock_name_t clock_;
  const uint8_t NUM_CHANNELS;
  Channel channels_[MAX_CHANNELS];
  uint16_t last_raw_[MAX_CHANNELS];
  const uint8_t filter_size_;
  float resolution_;
  float vref_;
  bool valid_;
};

}  // namespace LibXR

#endif
