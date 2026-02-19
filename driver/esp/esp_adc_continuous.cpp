#include "esp_adc.hpp"

#include <array>
#include <cstdlib>
#include <new>

#include "esp_clk_tree.h"
#include "esp_heap_caps.h"
#include "esp_private/adc_share_hw_ctrl.h"
#include "esp_private/esp_clk_tree_common.h"
#include "esp_private/sar_periph_ctrl.h"
#include "hal/adc_hal_common.h"
#include "hal/adc_hal.h"
#include "hal/dma_types.h"

#if SOC_GDMA_SUPPORTED
#include "esp_private/gdma.h"
#include "hal/gdma_ll.h"
#elif defined(CONFIG_IDF_TARGET_ESP32) && CONFIG_IDF_TARGET_ESP32
#include "esp_private/i2s_platform.h"
#include "hal/i2s_ll.h"
#include "soc/i2s_periph.h"
#endif

#if SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE
#include "esp_cache.h"
#include "hal/cache_hal.h"
#endif

#ifndef ANALOG_CLOCK_ENABLE
#define ANALOG_CLOCK_ENABLE() ((void)0)
#endif
#ifndef ANALOG_CLOCK_DISABLE
#define ANALOG_CLOCK_DISABLE() ((void)0)
#endif

namespace LibXR
{

#if SOC_ADC_DIG_CTRL_SUPPORTED && SOC_ADC_DMA_SUPPORTED

namespace
{

constexpr uint32_t kContinuousInternalBufNum = 5U;
constexpr uint32_t kDmaDescAlign = 4U;
#if defined(CONFIG_IDF_TARGET_ESP32) && CONFIG_IDF_TARGET_ESP32
constexpr uint32_t kEsp32DmaIntrMask = BIT(9);
#endif

}  // namespace

struct ESP32ADC::ContinuousLLContext
{
  adc_hal_dma_ctx_t hal = {};
  adc_hal_digi_ctrlr_cfg_t ctrl = {};

  dma_descriptor_t* desc = nullptr;
  uint8_t* dma_buf = nullptr;
  uint32_t frame_size = 0U;
  uint32_t desc_count = 0U;
  size_t desc_bytes = 0U;

  std::atomic<intptr_t> eof_desc_addr{0};
  bool hal_inited = false;
  bool dma_started = false;
  bool apb_claimed = false;
  bool analog_clock_on = false;
  bool power_claimed = false;
  bool lock_adc1 = false;
  bool lock_adc2 = false;

#if SOC_GDMA_SUPPORTED
  gdma_channel_handle_t gdma_chan = nullptr;
  bool gdma_connected = false;
  int gdma_group_id = -1;
  int gdma_channel_id = -1;
#elif defined(CONFIG_IDF_TARGET_ESP32) && CONFIG_IDF_TARGET_ESP32
  i2s_dev_t* i2s_dev = nullptr;
  intr_handle_t dma_intr = nullptr;
  bool i2s_claimed = false;
#endif
};

bool ESP32ADC::IsDigiUnitSupported(adc_unit_t unit)
{
  return SOC_ADC_DIG_SUPPORTED_UNIT(static_cast<int>(unit)) != 0;
}

void ESP32ADC::DrainContinuousFrames()
{
  if ((backend_ != Backend::CONTINUOUS_DMA) || (continuous_ctx_ == nullptr))
  {
    return;
  }

  auto* ctx = continuous_ctx_;
  if (!ctx->hal_inited)
  {
    return;
  }

#if SOC_GDMA_SUPPORTED
  auto refresh_gdma_eof_desc_addr = [&](ContinuousLLContext* ll_ctx)
  {
    if ((ll_ctx == nullptr) || (ll_ctx->gdma_group_id < 0) ||
        (ll_ctx->gdma_channel_id < 0))
    {
      return;
    }

    auto* gdma_dev = GDMA_LL_GET_HW(ll_ctx->gdma_group_id);
    ASSERT(gdma_dev != nullptr);
    if (gdma_dev == nullptr)
    {
      return;
    }

    const uint32_t suc_desc = gdma_ll_rx_get_success_eof_desc_addr(
        gdma_dev, static_cast<uint32_t>(ll_ctx->gdma_channel_id));
    const uint32_t err_desc = gdma_ll_rx_get_error_eof_desc_addr(
        gdma_dev, static_cast<uint32_t>(ll_ctx->gdma_channel_id));
    const intptr_t eof_desc_addr =
        static_cast<intptr_t>((suc_desc != 0U) ? suc_desc : err_desc);
    if (eof_desc_addr != 0)
    {
      ll_ctx->eof_desc_addr.store(eof_desc_addr, std::memory_order_relaxed);
    }
  };
  refresh_gdma_eof_desc_addr(ctx);
#endif

  uint8_t* out_buf = nullptr;
  uint32_t out_len = 0U;
  const auto status = adc_hal_get_reading_result(
      &ctx->hal, ctx->eof_desc_addr.load(std::memory_order_relaxed), &out_buf, &out_len);

  if (status != ADC_HAL_DMA_DESC_VALID)
  {
    return;
  }

  if ((out_buf == nullptr) || (out_len == 0U))
  {
    return;
  }

#if SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE
  const esp_err_t msync_ret =
      esp_cache_msync(out_buf, out_len, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
  ASSERT(msync_ret == ESP_OK);
  if (msync_ret != ESP_OK)
  {
    return;
  }
#endif

  std::array<uint32_t, SOC_ADC_MAX_CHANNEL_NUM> sums = {};
  std::array<uint32_t, SOC_ADC_MAX_CHANNEL_NUM> counts = {};
  ConsumeContinuousBuffer(out_buf, out_len, sums.data(), counts.data());

  for (uint8_t i = 0; i < num_channels_; ++i)
  {
    if (counts[i] == 0U)
    {
      continue;
    }

    const uint16_t raw = static_cast<uint16_t>(sums[i] / counts[i]);
    latest_raw_[i].store(raw, std::memory_order_relaxed);
    latest_values_[i] = Normalize(static_cast<float>(raw));
  }
}

void ESP32ADC::ConsumeContinuousBuffer(
    const uint8_t* buffer, uint32_t size, uint32_t* sums, uint32_t* counts)
{
  if ((buffer == nullptr) || (size == 0U) || (sums == nullptr) || (counts == nullptr))
  {
    return;
  }

  const uint32_t step = SOC_ADC_DIGI_RESULT_BYTES;
  if (step == 0U)
  {
    return;
  }

  auto consume_sample = [&](const adc_digi_output_data_t* sample) {
    if (sample == nullptr)
    {
      return;
    }

    adc_unit_t sample_unit = unit_;
    uint8_t sample_channel = 0;
    uint16_t sample_raw = 0;

#if defined(CONFIG_IDF_TARGET_ESP32) && CONFIG_IDF_TARGET_ESP32
    // ESP32 continuous output uses type1 layout.
    sample_channel = static_cast<uint8_t>(sample->type1.channel);
    sample_raw = static_cast<uint16_t>(sample->type1.data);
#elif defined(CONFIG_IDF_TARGET_ESP32C6) && CONFIG_IDF_TARGET_ESP32C6
    // C6 type2 payload does not carry ADC unit; continuous mode uses ADC1 path.
    sample_channel = static_cast<uint8_t>(sample->type2.channel);
    sample_raw = static_cast<uint16_t>(sample->type2.data);
    sample_unit = ADC_UNIT_1;
#else
    // C3/S2/S3 type2 payload carries unit and channel.
    sample_channel = static_cast<uint8_t>(sample->type2.channel);
    sample_raw = static_cast<uint16_t>(sample->type2.data);
#if (defined(CONFIG_IDF_TARGET_ESP32C3) && CONFIG_IDF_TARGET_ESP32C3) || \
    (defined(CONFIG_IDF_TARGET_ESP32S3) && CONFIG_IDF_TARGET_ESP32S3) || \
    (defined(CONFIG_IDF_TARGET_ESP32S2) && CONFIG_IDF_TARGET_ESP32S2)
    sample_unit = (sample->type2.unit == 0U) ? ADC_UNIT_1 : ADC_UNIT_2;
#endif
#endif

    if (sample_unit != unit_)
    {
      return;
    }

    if (sample_channel >=
        static_cast<uint8_t>(SOC_ADC_CHANNEL_NUM(static_cast<int>(unit_))))
    {
      return;
    }

    if (sample_channel >= SOC_ADC_MAX_CHANNEL_NUM)
    {
      return;
    }

    const uint8_t sample_idx = channel_idx_map_[sample_channel];
    if ((sample_idx == kInvalidChannelIdx) || (sample_idx >= num_channels_))
    {
      return;
    }

    sums[sample_idx] += static_cast<uint32_t>(sample_raw);
    counts[sample_idx] += 1U;
  };

  const uint32_t valid_size = size - (size % step);
  ASSERT(valid_size == size);

  uint32_t offset = 0U;
  while ((offset + step) <= valid_size)
  {
    consume_sample(
        reinterpret_cast<const adc_digi_output_data_t*>(buffer + offset));
    offset += step;
  }
}

void ESP32ADC::DeinitContinuous()
{
  auto* ctx = continuous_ctx_;
  if (ctx == nullptr)
  {
    return;
  }

  if (ctx->dma_started)
  {
#if SOC_GDMA_SUPPORTED
    (void)gdma_stop(ctx->gdma_chan);
#elif defined(CONFIG_IDF_TARGET_ESP32) && CONFIG_IDF_TARGET_ESP32
    i2s_ll_enable_intr(ctx->i2s_dev, kEsp32DmaIntrMask, false);
    i2s_ll_clear_intr_status(ctx->i2s_dev, kEsp32DmaIntrMask);
    i2s_ll_rx_stop_link(ctx->i2s_dev);
#endif
    ctx->dma_started = false;
  }

  if (ctx->hal_inited)
  {
    adc_hal_digi_enable(false);
    adc_hal_digi_connect(false);
    adc_hal_digi_deinit();
    ctx->hal_inited = false;
  }

#if SOC_GDMA_SUPPORTED
  if (ctx->gdma_chan != nullptr)
  {
    if (ctx->gdma_connected)
    {
      (void)gdma_disconnect(ctx->gdma_chan);
      ctx->gdma_connected = false;
    }
    (void)gdma_del_channel(ctx->gdma_chan);
    ctx->gdma_chan = nullptr;
  }
#elif defined(CONFIG_IDF_TARGET_ESP32) && CONFIG_IDF_TARGET_ESP32
  if (ctx->dma_intr != nullptr)
  {
    (void)esp_intr_free(ctx->dma_intr);
    ctx->dma_intr = nullptr;
  }
  if (ctx->i2s_claimed)
  {
    i2s_platform_release_occupation(I2S_CTLR_HP, ADC_HAL_DMA_I2S_HOST);
    ctx->i2s_claimed = false;
  }
#endif

  if (ctx->lock_adc2)
  {
    (void)adc_lock_release(ADC_UNIT_2);
    ctx->lock_adc2 = false;
  }
  if (ctx->lock_adc1)
  {
    (void)adc_lock_release(ADC_UNIT_1);
    ctx->lock_adc1 = false;
  }

  if (ctx->power_claimed)
  {
    sar_periph_ctrl_adc_continuous_power_release();
    ctx->power_claimed = false;
  }
  if (ctx->analog_clock_on)
  {
    ANALOG_CLOCK_DISABLE();
    ctx->analog_clock_on = false;
  }
  if (ctx->apb_claimed)
  {
    adc_apb_periph_free();
    ctx->apb_claimed = false;
  }

  free(ctx->dma_buf);
  ctx->dma_buf = nullptr;
  free(ctx->desc);
  ctx->desc = nullptr;

  delete ctx;
  continuous_ctx_ = nullptr;
}

ESP32ADC::ContinuousInitResult ESP32ADC::InitContinuous(uint32_t freq, size_t dma_buf_size)
{
  if (!IsDigiUnitSupported(unit_))
  {
    // Capability mismatch is expected for some unit/target combinations.
    return ContinuousInitResult::UNSUPPORTED;
  }

  auto fail = [&]() -> ContinuousInitResult {
    DeinitContinuous();
    return ContinuousInitResult::FAILED;
  };

  auto fail_with_patterns = [&](adc_digi_pattern_config_t* patterns)
      -> ContinuousInitResult {
    delete[] patterns;
    return fail();
  };

  const uint32_t align = SOC_ADC_DIGI_DATA_BYTES_PER_CONV;
  const uint32_t min_frame =
      static_cast<uint32_t>(num_channels_) * SOC_ADC_DIGI_RESULT_BYTES;
  const uint32_t req_frame = static_cast<uint32_t>(
      (dma_buf_size > static_cast<size_t>(min_frame)) ? dma_buf_size : min_frame);
  const uint32_t frame_size = AlignUp(req_frame, align);

  auto* ctx = new (std::nothrow) ContinuousLLContext();
  ASSERT(ctx != nullptr);
  if (ctx == nullptr)
  {
    return fail();
  }

  continuous_ctx_ = ctx;
  ctx->frame_size = frame_size;
  const uint32_t dma_desc_num_per_frame =
      (frame_size + DMA_DESCRIPTOR_BUFFER_MAX_SIZE_4B_ALIGNED - 1U) /
      DMA_DESCRIPTOR_BUFFER_MAX_SIZE_4B_ALIGNED;
  ctx->desc_count = dma_desc_num_per_frame * kContinuousInternalBufNum;

  ctx->dma_buf = static_cast<uint8_t*>(heap_caps_calloc(
      kContinuousInternalBufNum, frame_size,
      MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  ctx->desc = static_cast<dma_descriptor_t*>(heap_caps_aligned_calloc(
      kDmaDescAlign, ctx->desc_count, sizeof(dma_descriptor_t),
      MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  ASSERT((ctx->dma_buf != nullptr) && (ctx->desc != nullptr));
  if ((ctx->dma_buf == nullptr) || (ctx->desc == nullptr))
  {
    return fail();
  }

#if SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE
  {
    const uint32_t cache_line_size =
        cache_hal_get_cache_line_size(CACHE_LL_LEVEL_INT_MEM, CACHE_TYPE_DATA);
    ctx->desc_bytes =
        ((ctx->desc_count * sizeof(dma_descriptor_t) + cache_line_size - 1U) /
         cache_line_size) *
        cache_line_size;
  }
#else
  ctx->desc_bytes = ctx->desc_count * sizeof(dma_descriptor_t);
#endif

  ctx->hal.rx_desc = ctx->desc;
  adc_hal_dma_config_t dma_cfg = {};
  dma_cfg.eof_desc_num = kContinuousInternalBufNum;
  dma_cfg.eof_step = dma_desc_num_per_frame;
  dma_cfg.eof_num = frame_size / SOC_ADC_DIGI_DATA_BYTES_PER_CONV;
  adc_hal_dma_ctx_config(&ctx->hal, &dma_cfg);

  adc_digi_pattern_config_t* patterns =
      new (std::nothrow) adc_digi_pattern_config_t[num_channels_];
  ASSERT(patterns != nullptr);
  if (patterns == nullptr)
  {
    return fail();
  }

  for (uint8_t i = 0; i < num_channels_; ++i)
  {
    patterns[i].atten = static_cast<uint8_t>(attenuation_);
    patterns[i].channel = static_cast<uint8_t>(channel_ids_[i]);
    patterns[i].unit = static_cast<uint8_t>(unit_);
    patterns[i].bit_width = static_cast<uint8_t>(bitwidth_);
  }

  uint32_t clk_src_freq_hz = 0U;
  if (esp_clk_tree_src_get_freq_hz(
          static_cast<soc_module_clk_t>(ADC_DIGI_CLK_SRC_DEFAULT),
                                   ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED,
                                   &clk_src_freq_hz) != ESP_OK)
  {
    return fail_with_patterns(patterns);
  }

  ctx->ctrl.adc_pattern_len = num_channels_;
  ctx->ctrl.adc_pattern = patterns;
  ctx->ctrl.sample_freq_hz = ClampSampleFreq(freq);
  ctx->ctrl.conv_mode =
      (unit_ == ADC_UNIT_2) ? ADC_CONV_SINGLE_UNIT_2 : ADC_CONV_SINGLE_UNIT_1;
  ctx->ctrl.bit_width = static_cast<uint32_t>(bitwidth_);
  ctx->ctrl.clk_src = ADC_DIGI_CLK_SRC_DEFAULT;
  ctx->ctrl.clk_src_freq_hz = clk_src_freq_hz;

  adc_apb_periph_claim();
  ctx->apb_claimed = true;
  ANALOG_CLOCK_ENABLE();
  ctx->analog_clock_on = true;
  sar_periph_ctrl_adc_continuous_power_acquire();
  ctx->power_claimed = true;

  if (unit_ == ADC_UNIT_2)
  {
    if (adc_lock_acquire(ADC_UNIT_2) != ESP_OK)
    {
      return fail_with_patterns(patterns);
    }
    ctx->lock_adc2 = true;
  }
  else
  {
    if (adc_lock_acquire(ADC_UNIT_1) != ESP_OK)
    {
      return fail_with_patterns(patterns);
    }
    ctx->lock_adc1 = true;
  }

  adc_hal_set_controller(unit_, ADC_HAL_CONTINUOUS_READ_MODE);

#if SOC_GDMA_SUPPORTED
  gdma_channel_alloc_config_t rx_alloc = {};
  rx_alloc.direction = GDMA_CHANNEL_DIRECTION_RX;
  // Continuous ADC path does not require cache-disabled ISR service.
  // Keep this explicit to avoid accidental IRAM-safety constraint enable.
  rx_alloc.flags.isr_cache_safe = false;
  if (gdma_new_ahb_channel(&rx_alloc, &ctx->gdma_chan) != ESP_OK)
  {
    return fail_with_patterns(patterns);
  }
  if (gdma_connect(ctx->gdma_chan, GDMA_MAKE_TRIGGER(GDMA_TRIG_PERIPH_ADC, 0)) != ESP_OK)
  {
    return fail_with_patterns(patterns);
  }
  ctx->gdma_connected = true;
  if (gdma_get_group_channel_id(
          ctx->gdma_chan, &ctx->gdma_group_id, &ctx->gdma_channel_id) != ESP_OK)
  {
    return fail_with_patterns(patterns);
  }

  gdma_strategy_config_t strategy = {};
  strategy.auto_update_desc = true;
  strategy.owner_check = true;
  if (gdma_apply_strategy(ctx->gdma_chan, &strategy) != ESP_OK)
  {
    return fail_with_patterns(patterns);
  }
  ctx->eof_desc_addr.store(0, std::memory_order_relaxed);
#elif defined(CONFIG_IDF_TARGET_ESP32) && CONFIG_IDF_TARGET_ESP32
  if (i2s_platform_acquire_occupation(I2S_CTLR_HP, ADC_HAL_DMA_I2S_HOST, "libxr_adc") !=
      ESP_OK)
  {
    return fail_with_patterns(patterns);
  }
  ctx->i2s_claimed = true;
  ctx->i2s_dev = I2S_LL_GET_HW(ADC_HAL_DMA_I2S_HOST);

  const esp_err_t intr_err = esp_intr_alloc(
      i2s_periph_signal[ADC_HAL_DMA_I2S_HOST].irq, ESP_INTR_FLAG_IRAM,
      +[](void* user_data) {
        auto* ll_ctx = static_cast<ContinuousLLContext*>(user_data);
        if ((ll_ctx == nullptr) || (ll_ctx->i2s_dev == nullptr))
        {
          return;
        }
        const bool finish =
            (i2s_ll_get_intr_status(ll_ctx->i2s_dev) & kEsp32DmaIntrMask) != 0U;
        if (!finish)
        {
          return;
        }
        i2s_ll_clear_intr_status(ll_ctx->i2s_dev, kEsp32DmaIntrMask);
        uint32_t desc_addr = 0U;
        i2s_ll_rx_get_eof_des_addr(ll_ctx->i2s_dev, &desc_addr);
        ll_ctx->eof_desc_addr.store(static_cast<intptr_t>(desc_addr),
                                    std::memory_order_relaxed);
      },
      ctx, &ctx->dma_intr);
  if (intr_err != ESP_OK)
  {
    return fail_with_patterns(patterns);
  }
#endif

  adc_hal_digi_init(&ctx->hal);
  ctx->hal_inited = true;
  adc_hal_digi_controller_config(&ctx->hal, &ctx->ctrl);
  adc_hal_digi_enable(false);
  adc_hal_digi_connect(false);

#if SOC_GDMA_SUPPORTED
  (void)gdma_stop(ctx->gdma_chan);
  (void)gdma_reset(ctx->gdma_chan);
#elif defined(CONFIG_IDF_TARGET_ESP32) && CONFIG_IDF_TARGET_ESP32
  i2s_ll_enable_intr(ctx->i2s_dev, kEsp32DmaIntrMask, false);
  i2s_ll_clear_intr_status(ctx->i2s_dev, kEsp32DmaIntrMask);
  i2s_ll_rx_stop_link(ctx->i2s_dev);
  i2s_ll_rx_reset_dma(ctx->i2s_dev);
#endif

  adc_hal_digi_reset();
  adc_hal_digi_dma_link(&ctx->hal, ctx->dma_buf);

#if SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE
  {
    const esp_err_t msync_ret = esp_cache_msync(
        ctx->hal.rx_desc, ctx->desc_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    ASSERT(msync_ret == ESP_OK);
    if (msync_ret != ESP_OK)
    {
      return fail_with_patterns(patterns);
    }
  }
#endif

#if SOC_GDMA_SUPPORTED
  if (gdma_start(ctx->gdma_chan, reinterpret_cast<intptr_t>(ctx->hal.rx_desc)) != ESP_OK)
  {
    return fail_with_patterns(patterns);
  }
#elif defined(CONFIG_IDF_TARGET_ESP32) && CONFIG_IDF_TARGET_ESP32
  i2s_ll_clear_intr_status(ctx->i2s_dev, kEsp32DmaIntrMask);
  i2s_ll_enable_intr(ctx->i2s_dev, kEsp32DmaIntrMask, true);
  i2s_ll_enable_dma(ctx->i2s_dev, true);
  i2s_ll_rx_start_link(ctx->i2s_dev, reinterpret_cast<uint32_t>(ctx->hal.rx_desc));
#endif
  ctx->dma_started = true;

  adc_hal_digi_connect(true);
  adc_hal_digi_enable(true);

  delete[] patterns;
  backend_ = Backend::CONTINUOUS_DMA;
  for (uint8_t i = 0; i < num_channels_; ++i)
  {
    channel_ready_[i] = true;
    ConfigureAnalogPad(channel_ids_[i]);
  }
  return ContinuousInitResult::STARTED;
}

#else

ESP32ADC::ContinuousInitResult ESP32ADC::InitContinuous(uint32_t, size_t)
{
  return ContinuousInitResult::UNSUPPORTED;
}

#endif

}  // namespace LibXR
