#include "mspm0_adc.hpp"

#if defined(__MSPM0_HAS_ADC12__)

#include <ti/driverlib/dl_dma.h>

#include <cstdint>

using namespace LibXR;

namespace
{

constexpr uint32_t MSPM0_ADC_DMA_TRIGGER_INVALID = 0xFFFFFFFFU;
constexpr uint8_t MSPM0_ADC_DMA_CHANNEL_INVALID = 0xFF;
constexpr uint32_t MSPM0_ADC_POLLING_TIMEOUT = 300000U;

uint32_t GetMemResultInterruptMask(DL_ADC12_MEM_IDX mem_idx)
{
  switch (mem_idx)
  {
    case DL_ADC12_MEM_IDX_0:
      return DL_ADC12_INTERRUPT_MEM0_RESULT_LOADED;
    case DL_ADC12_MEM_IDX_1:
      return DL_ADC12_INTERRUPT_MEM1_RESULT_LOADED;
    case DL_ADC12_MEM_IDX_2:
      return DL_ADC12_INTERRUPT_MEM2_RESULT_LOADED;
    case DL_ADC12_MEM_IDX_3:
      return DL_ADC12_INTERRUPT_MEM3_RESULT_LOADED;
    case DL_ADC12_MEM_IDX_4:
      return DL_ADC12_INTERRUPT_MEM4_RESULT_LOADED;
    case DL_ADC12_MEM_IDX_5:
      return DL_ADC12_INTERRUPT_MEM5_RESULT_LOADED;
    case DL_ADC12_MEM_IDX_6:
      return DL_ADC12_INTERRUPT_MEM6_RESULT_LOADED;
    case DL_ADC12_MEM_IDX_7:
      return DL_ADC12_INTERRUPT_MEM7_RESULT_LOADED;
    case DL_ADC12_MEM_IDX_8:
      return DL_ADC12_INTERRUPT_MEM8_RESULT_LOADED;
    case DL_ADC12_MEM_IDX_9:
      return DL_ADC12_INTERRUPT_MEM9_RESULT_LOADED;
    case DL_ADC12_MEM_IDX_10:
      return DL_ADC12_INTERRUPT_MEM10_RESULT_LOADED;
    case DL_ADC12_MEM_IDX_11:
      return DL_ADC12_INTERRUPT_MEM11_RESULT_LOADED;
    default:
      return DL_ADC12_INTERRUPT_MEM0_RESULT_LOADED;
  }
}

uint32_t GetMemResultDmaTriggerMask(DL_ADC12_MEM_IDX mem_idx)
{
  switch (mem_idx)
  {
    case DL_ADC12_MEM_IDX_0:
      return DL_ADC12_DMA_MEM0_RESULT_LOADED;
    case DL_ADC12_MEM_IDX_1:
      return DL_ADC12_DMA_MEM1_RESULT_LOADED;
    case DL_ADC12_MEM_IDX_2:
      return DL_ADC12_DMA_MEM2_RESULT_LOADED;
    case DL_ADC12_MEM_IDX_3:
      return DL_ADC12_DMA_MEM3_RESULT_LOADED;
    case DL_ADC12_MEM_IDX_4:
      return DL_ADC12_DMA_MEM4_RESULT_LOADED;
    case DL_ADC12_MEM_IDX_5:
      return DL_ADC12_DMA_MEM5_RESULT_LOADED;
    case DL_ADC12_MEM_IDX_6:
      return DL_ADC12_DMA_MEM6_RESULT_LOADED;
    case DL_ADC12_MEM_IDX_7:
      return DL_ADC12_DMA_MEM7_RESULT_LOADED;
    case DL_ADC12_MEM_IDX_8:
      return DL_ADC12_DMA_MEM8_RESULT_LOADED;
    case DL_ADC12_MEM_IDX_9:
      return DL_ADC12_DMA_MEM9_RESULT_LOADED;
    case DL_ADC12_MEM_IDX_10:
      return DL_ADC12_DMA_MEM10_RESULT_LOADED;
    case DL_ADC12_MEM_IDX_11:
      return DL_ADC12_DMA_MEM11_RESULT_LOADED;
    default:
      return DL_ADC12_DMA_MEM0_RESULT_LOADED;
  }
}

uint32_t GetAdcDmaTrigger(const ADC12_Regs* instance)  // NOLINT
{
  const uintptr_t instance_addr = reinterpret_cast<uintptr_t>(instance);

#if defined(ADC0_BASE) && defined(DMA_ADC0_EVT_GEN_BD_TRIG)
  if (instance_addr == static_cast<uintptr_t>(ADC0_BASE))
  {
    return DMA_ADC0_EVT_GEN_BD_TRIG;
  }
#endif

#if defined(ADC1_BASE) && defined(DMA_ADC1_EVT_GEN_BD_TRIG)
  if (instance_addr == static_cast<uintptr_t>(ADC1_BASE))
  {
    return DMA_ADC1_EVT_GEN_BD_TRIG;
  }
#endif

  return MSPM0_ADC_DMA_TRIGGER_INVALID;
}

uint8_t ResolveDmaChannelId(uint32_t trigger)
{
  if (trigger == MSPM0_ADC_DMA_TRIGGER_INVALID)
  {
    return MSPM0_ADC_DMA_CHANNEL_INVALID;
  }

#if defined(DMA_BASE)
#if defined(DMA_SYS_N_DMA_CHANNEL)
  constexpr uint8_t DMA_CHANNEL_COUNT = static_cast<uint8_t>(DMA_SYS_N_DMA_CHANNEL);
#else
  constexpr uint8_t DMA_CHANNEL_COUNT = 8;
#endif

  for (uint8_t channel = 0; channel < DMA_CHANNEL_COUNT; ++channel)
  {
    if (DL_DMA_getTriggerType(DMA, channel) != DL_DMA_TRIGGER_TYPE_EXTERNAL)
    {
      continue;
    }

    if (DL_DMA_getTrigger(DMA, channel) == trigger)
    {
      return channel;
    }
  }
#endif

  return MSPM0_ADC_DMA_CHANNEL_INVALID;
}

bool IsFullDmaChannel(uint8_t channel)
{
#if defined(DMA_SYS_N_DMA_FULL_CHANNEL)
  return channel < static_cast<uint8_t>(DMA_SYS_N_DMA_FULL_CHANNEL);
#else
  (void)channel;
  return false;
#endif
}

uint32_t GetContinuousSampleMode(const ADC12_Regs* instance)
{
  const uint32_t sample_mode = DL_ADC12_getSampleMode(instance);
  if (sample_mode == DL_ADC12_SAMP_MODE_SEQUENCE ||
      sample_mode == DL_ADC12_SAMP_MODE_SEQUENCE_REPEAT)
  {
    return DL_ADC12_SAMP_MODE_SEQUENCE_REPEAT;
  }

  return DL_ADC12_SAMP_MODE_SINGLE_REPEAT;
}

}  // namespace

MSPM0ADC::MSPM0ADC(Resources res)
    : res_(res),
      scale_(0.0f),
      use_dma_(false),
      dma_channel_id_(DMA_CHANNEL_INVALID),
      dma_sample_(0)
{
  ASSERT(res_.instance != nullptr);
  ASSERT(res_.vref > 0.0f);

  const uint32_t resolution = DL_ADC12_getResolution(res_.instance);
  float full_scale = 4095.0f;

  if (resolution == DL_ADC12_SAMP_CONV_RES_10_BIT)
  {
    full_scale = 1023.0f;
  }
  else if (resolution == DL_ADC12_SAMP_CONV_RES_8_BIT)
  {
    full_scale = 255.0f;
  }

  scale_ = res_.vref / full_scale;

#if !defined(DMA_BASE)
  use_dma_ = false;
#else
  const uint32_t dma_trigger = GetAdcDmaTrigger(res_.instance);
  dma_channel_id_ = ResolveDmaChannelId(dma_trigger);
  use_dma_ =
      (dma_channel_id_ != DMA_CHANNEL_INVALID) && IsFullDmaChannel(dma_channel_id_);

  if (DL_ADC12_isDMAEnabled(res_.instance))
  {
    ASSERT(use_dma_);
  }

  if (use_dma_)
  {
    StartContinuousDMA();
  }
#endif
}

float MSPM0ADC::Read()
{
  if (use_dma_)
  {
    return ReadByDMA();
  }

  return ReadByPolling();
}

float MSPM0ADC::ReadByPolling()
{
  const uint32_t mem_interrupt = GetMemResultInterruptMask(res_.mem_idx);

  DL_ADC12_clearInterruptStatus(res_.instance, mem_interrupt);
  DL_ADC12_startConversion(res_.instance);

  uint32_t timeout = MSPM0_ADC_POLLING_TIMEOUT;
  while (DL_ADC12_getRawInterruptStatus(res_.instance, mem_interrupt) == 0U)
  {
    if (timeout-- == 0U)
    {
      ASSERT(false);
      return 0.0f;
    }
  }

  const uint16_t raw = DL_ADC12_getMemResult(res_.instance, res_.mem_idx);
  DL_ADC12_clearInterruptStatus(res_.instance, mem_interrupt);
  return static_cast<float>(raw) * scale_;
}

float MSPM0ADC::ReadByDMA() { return static_cast<float>(dma_sample_) * scale_; }

void MSPM0ADC::StartContinuousDMA()
{
#if defined(DMA_BASE) && defined(DEVICE_HAS_DMA_FULL_CHANNEL)
  const uint32_t dma_mask = GetMemResultDmaTriggerMask(res_.mem_idx);

  DL_DMA_disableChannel(DMA, dma_channel_id_);
  DL_DMA_configTransfer(DMA, dma_channel_id_, DL_DMA_FULL_CH_REPEAT_SINGLE_TRANSFER_MODE,
                        DL_DMA_NORMAL_MODE, DL_DMA_WIDTH_HALF_WORD,
                        DL_DMA_WIDTH_HALF_WORD, DL_DMA_ADDR_UNCHANGED,
                        DL_DMA_ADDR_UNCHANGED);
  DL_DMA_setSrcAddr(
      DMA, dma_channel_id_,
      static_cast<uint32_t>(DL_ADC12_getMemResultAddress(res_.instance, res_.mem_idx)));
  DL_DMA_setDestAddr(DMA, dma_channel_id_,
                     static_cast<uint32_t>(reinterpret_cast<uintptr_t>(
                         const_cast<uint16_t*>(&dma_sample_))));
  DL_DMA_setTransferSize(DMA, dma_channel_id_, 1);
  DL_DMA_enableChannel(DMA, dma_channel_id_);

  DL_ADC12_stopConversion(res_.instance);
  DL_ADC12_disableConversions(res_.instance);
  DL_Common_updateReg(
      &res_.instance->ULLMEM.CTL1,
      GetContinuousSampleMode(res_.instance) | DL_ADC12_SAMPLING_SOURCE_AUTO |
          DL_ADC12_TRIG_SRC_SOFTWARE,
      ADC12_CTL1_SAMPMODE_MASK | ADC12_CTL1_CONSEQ_MASK | ADC12_CTL1_TRIGSRC_MASK);

  DL_ADC12_setDMASamplesCnt(res_.instance, 1);
  DL_ADC12_enableDMATrigger(res_.instance, dma_mask);
  DL_ADC12_enableDMA(res_.instance);
  DL_ADC12_enableConversions(res_.instance);
  DL_ADC12_clearDMATriggerStatus(res_.instance, dma_mask);
  DL_ADC12_clearInterruptStatus(res_.instance, DL_ADC12_INTERRUPT_DMA_DONE);
  DL_ADC12_startConversion(res_.instance);
#else
  ASSERT(false);
#endif
}

#endif
