#include "hpm_adc.hpp"

#if LIBXR_HPM_ADC_SUPPORTED

#include "board.h"

using namespace LibXR;

namespace
{

constexpr uint32_t LOCK_MAGIC = 0xADCA5A5AU;
constexpr uint32_t READ_RETRY_COUNT = 1024U;

}  // namespace

extern "C" void __attribute__((weak)) board_init_adc16_pins(void) {}
extern "C" uint32_t __attribute__((weak)) board_init_adc_clock(void* ptr,
                                                               bool clk_src_bus)
{
  (void)ptr;
  (void)clk_src_bus;
  return 0U;
}

HPMADC::Channel::Channel() : adc_(nullptr), index_(0U), ch_(0U) {}

HPMADC::Channel::Channel(HPMADC* adc, uint8_t index, uint8_t ch)
    : adc_(adc), index_(index), ch_(ch)
{
}

float HPMADC::Channel::Read() { return adc_ ? adc_->ReadChannel(index_) : 0.0f; }

HPMADC::HPMADC(ADC16_Type* adc, clock_name_t clock,
               std::initializer_list<uint8_t> channels, float vref, uint32_t sample_cycle,
               adc16_resolution_t resolution, adc16_clock_divider_t clock_divider,
               bool auto_board_init, uint8_t filter_size)
    : adc_(adc),
      clock_(clock),
      NUM_CHANNELS(static_cast<uint8_t>(channels.size())),
      filter_size_(filter_size),
      resolution_(ResolutionMax(resolution)),
      vref_(vref),
      valid_(false)
{
  ASSERT(adc_ != nullptr);
  ASSERT(NUM_CHANNELS > 0U);
  ASSERT(NUM_CHANNELS <= MAX_CHANNELS);
  ASSERT(vref_ > 0.0f);
  ASSERT(filter_size_ > 0U);
  ASSERT(resolution_ > 0.0f);

  auto it = channels.begin();
  for (uint8_t i = 0; i < NUM_CHANNELS; ++i)
  {
    channels_[i] = Channel(this, i, *it++);
    last_raw_[i] = 0U;
  }

  valid_ = Init(sample_cycle, resolution, clock_divider, auto_board_init);
  ASSERT(valid_);
}

HPMADC::Channel& HPMADC::GetChannel(uint8_t index)
{
  ASSERT(index < NUM_CHANNELS);
  return channels_[index];
}

float HPMADC::ReadChannel(uint8_t channel)
{
  ASSERT(valid_);
  ASSERT(channel < NUM_CHANNELS);
  if (!valid_)
  {
    return 0.0f;
  }
  if (channel >= NUM_CHANNELS)
  {
    return -1.0f;
  }

  uint32_t expected = 0U;
  if (!locked_.compare_exchange_strong(expected, LOCK_MAGIC, std::memory_order_acquire,
                                       std::memory_order_relaxed))
  {
    ASSERT(false);
    return 0.0f;
  }

  uint32_t sum = 0U;
  uint8_t samples = 0U;
  for (uint8_t i = 0U; i < filter_size_; ++i)
  {
    uint16_t raw = 0U;
    hpm_stat_t status = status_fail;
    for (uint32_t retry = 0U; retry < READ_RETRY_COUNT; ++retry)
    {
      status = adc16_get_oneshot_result(adc_, channels_[channel].ch_, &raw);
      if (status == status_success)
      {
        break;
      }
    }

    if (status == status_success)
    {
      last_raw_[channel] = raw;
      sum += raw;
      ++samples;
    }
  }

  locked_.store(0U, std::memory_order_release);

  if (samples == 0U)
  {
    return 0.0f;
  }

  return ConvertToVoltage(static_cast<float>(sum) / static_cast<float>(samples));
}

uint16_t HPMADC::GetLastRaw(uint8_t channel) const
{
  ASSERT(channel < NUM_CHANNELS);
  if (channel >= NUM_CHANNELS)
  {
    return 0U;
  }
  return last_raw_[channel];
}

float HPMADC::ResolutionMax(adc16_resolution_t resolution)
{
  switch (resolution)
  {
    case adc16_res_8_bits:
      return 255.0f;
    case adc16_res_10_bits:
      return 1023.0f;
    case adc16_res_12_bits:
      return 4095.0f;
    case adc16_res_16_bits:
    default:
      return 65535.0f;
  }
}

bool HPMADC::Init(uint32_t sample_cycle, adc16_resolution_t resolution,
                  adc16_clock_divider_t clock_divider, bool auto_board_init)
{
  if ((adc_ == nullptr) || (NUM_CHANNELS == 0U) || (sample_cycle == 0U))
  {
    return false;
  }

  if (auto_board_init)
  {
    board_init_adc16_pins();
  }

  const uint32_t clock_hz = board_init_adc_clock(adc_, true);
  if (clock_hz == 0U)
  {
    clock_add_to_group(clock_, 0);
  }

  adc16_config_t config;
  adc16_get_default_config(&config);
  config.res = resolution;
  config.conv_mode = adc16_conv_mode_oneshot;
  config.adc_clk_div = clock_divider;
  config.wait_dis = true;
  config.sel_sync_ahb = true;
  config.adc_ahb_en = false;

  if (adc16_init(adc_, &config) != status_success)
  {
    return false;
  }

  for (uint8_t i = 0; i < NUM_CHANNELS; ++i)
  {
    adc16_channel_config_t channel_config;
    adc16_get_channel_default_config(&channel_config);
    channel_config.ch = channels_[i].ch_;
    channel_config.sample_cycle = sample_cycle;

    if (adc16_init_channel(adc_, &channel_config) != status_success)
    {
      return false;
    }

    last_raw_[i] = 0U;
  }

  adc16_set_nonblocking_read(adc_);

#if defined(ADC_SOC_BUSMODE_ENABLE_CTRL_SUPPORT) && ADC_SOC_BUSMODE_ENABLE_CTRL_SUPPORT
  adc16_enable_oneshot_mode(adc_);
#endif

  return true;
}

float HPMADC::ConvertToVoltage(float adc_value) const
{
  return adc_value * vref_ / resolution_;
}

#endif
