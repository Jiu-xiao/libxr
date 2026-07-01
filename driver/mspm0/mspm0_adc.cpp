#include "mspm0_adc.hpp"

#if defined(__MSPM0_HAS_ADC12__)

#include <ti/driverlib/dl_dma.h>

#include <cstddef>
#include <cstdint>
#include <limits>

using namespace LibXR;

namespace
{

constexpr uint32_t MSPM0_ADC_DMA_TRIGGER_INVALID = 0xFFFFFFFFU;
constexpr uint8_t MSPM0_ADC_DMA_CHANNEL_INVALID = 0xFF;
constexpr uint32_t MSPM0_ADC_POLLING_TIMEOUT = 300000U;
constexpr uint8_t MSPM0_ADC_MEM_COUNT = 12U;

uint8_t MemIndexValue(DL_ADC12_MEM_IDX mem_idx) { return static_cast<uint8_t>(mem_idx); }

bool IsValidMemIndex(DL_ADC12_MEM_IDX mem_idx)
{
  return MemIndexValue(mem_idx) < MSPM0_ADC_MEM_COUNT;
}

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

uint32_t GetAllMemResultDmaTriggerMask()
{
  uint32_t mask = 0U;
  for (uint8_t i = 0; i < MSPM0_ADC_MEM_COUNT; ++i)
  {
    mask |= GetMemResultDmaTriggerMask(static_cast<DL_ADC12_MEM_IDX>(i));
  }
  return mask;
}

uint32_t GetSequenceStartAddress(DL_ADC12_MEM_IDX mem_idx)
{
  switch (mem_idx)
  {
    case DL_ADC12_MEM_IDX_0:
      return DL_ADC12_SEQ_START_ADDR_00;
    case DL_ADC12_MEM_IDX_1:
      return DL_ADC12_SEQ_START_ADDR_01;
    case DL_ADC12_MEM_IDX_2:
      return DL_ADC12_SEQ_START_ADDR_02;
    case DL_ADC12_MEM_IDX_3:
      return DL_ADC12_SEQ_START_ADDR_03;
    case DL_ADC12_MEM_IDX_4:
      return DL_ADC12_SEQ_START_ADDR_04;
    case DL_ADC12_MEM_IDX_5:
      return DL_ADC12_SEQ_START_ADDR_05;
    case DL_ADC12_MEM_IDX_6:
      return DL_ADC12_SEQ_START_ADDR_06;
    case DL_ADC12_MEM_IDX_7:
      return DL_ADC12_SEQ_START_ADDR_07;
    case DL_ADC12_MEM_IDX_8:
      return DL_ADC12_SEQ_START_ADDR_08;
    case DL_ADC12_MEM_IDX_9:
      return DL_ADC12_SEQ_START_ADDR_09;
    case DL_ADC12_MEM_IDX_10:
      return DL_ADC12_SEQ_START_ADDR_10;
    case DL_ADC12_MEM_IDX_11:
      return DL_ADC12_SEQ_START_ADDR_11;
    default:
      return DL_ADC12_SEQ_START_ADDR_00;
  }
}

uint32_t GetSequenceEndAddress(DL_ADC12_MEM_IDX mem_idx)
{
  switch (mem_idx)
  {
    case DL_ADC12_MEM_IDX_0:
      return DL_ADC12_SEQ_END_ADDR_00;
    case DL_ADC12_MEM_IDX_1:
      return DL_ADC12_SEQ_END_ADDR_01;
    case DL_ADC12_MEM_IDX_2:
      return DL_ADC12_SEQ_END_ADDR_02;
    case DL_ADC12_MEM_IDX_3:
      return DL_ADC12_SEQ_END_ADDR_03;
    case DL_ADC12_MEM_IDX_4:
      return DL_ADC12_SEQ_END_ADDR_04;
    case DL_ADC12_MEM_IDX_5:
      return DL_ADC12_SEQ_END_ADDR_05;
    case DL_ADC12_MEM_IDX_6:
      return DL_ADC12_SEQ_END_ADDR_06;
    case DL_ADC12_MEM_IDX_7:
      return DL_ADC12_SEQ_END_ADDR_07;
    case DL_ADC12_MEM_IDX_8:
      return DL_ADC12_SEQ_END_ADDR_08;
    case DL_ADC12_MEM_IDX_9:
      return DL_ADC12_SEQ_END_ADDR_09;
    case DL_ADC12_MEM_IDX_10:
      return DL_ADC12_SEQ_END_ADDR_10;
    case DL_ADC12_MEM_IDX_11:
      return DL_ADC12_SEQ_END_ADDR_11;
    default:
      return DL_ADC12_SEQ_END_ADDR_00;
  }
}

uint8_t CalculateFilterSize(RawData dma_buff, std::size_t channel_count)
{
  ASSERT(channel_count > 0U);

  const std::size_t filter_size = dma_buff.size_ / channel_count / sizeof(uint16_t);
  ASSERT(filter_size > 0U);
  ASSERT(filter_size <= std::numeric_limits<uint8_t>::max());
  return static_cast<uint8_t>(filter_size);
}

bool IsAlignedForWordDma(const void* addr)
{
  return (reinterpret_cast<uintptr_t>(addr) % alignof(uint32_t)) == 0U;
}

bool IsContiguousMemRange(const DL_ADC12_MEM_IDX* mem_indices, uint8_t count)
{
  ASSERT(mem_indices != nullptr);
  if (mem_indices == nullptr || count == 0U)
  {
    return false;
  }

  const uint8_t first = MemIndexValue(mem_indices[0]);
  for (uint8_t i = 0; i < count; ++i)
  {
    if (!IsValidMemIndex(mem_indices[i]) || MemIndexValue(mem_indices[i]) != first + i)
    {
      return false;
    }
  }
  return true;
}

bool CanUseFifoDMA(const RawData& dma_buff, uint8_t channel_count, uint8_t filter_size)
{
  const uint32_t total_samples =
      static_cast<uint32_t>(channel_count) * static_cast<uint32_t>(filter_size);

  return dma_buff.addr_ != nullptr && total_samples >= 2U && (total_samples % 2U) == 0U &&
         IsAlignedForWordDma(dma_buff.addr_) &&
         (channel_count == 1U || (channel_count % 2U) == 0U);
}

uint32_t GetFifoDmaTriggerMask(const DL_ADC12_MEM_IDX* mem_indices, uint8_t channel_count)
{
  ASSERT(mem_indices != nullptr);
  if (channel_count == 1U)
  {
    return GetMemResultDmaTriggerMask(mem_indices[0]);
  }

  uint32_t mask = 0U;
  for (uint8_t i = 1U; i < channel_count; i += 2U)
  {
    mask |= GetMemResultDmaTriggerMask(mem_indices[i]);
  }
  return mask;
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

}  // namespace

MSPM0ADC::Channel::Channel() : adc_(nullptr), index_(0), mem_idx_(DL_ADC12_MEM_IDX_0) {}

MSPM0ADC::Channel::Channel(MSPM0ADC* adc, uint8_t index, DL_ADC12_MEM_IDX mem_idx)
    : adc_(adc), index_(index), mem_idx_(mem_idx)
{
}

float MSPM0ADC::Channel::Read()
{
  return adc_ != nullptr ? adc_->ReadChannel(index_) : 0.0f;
}

MSPM0ADC::MSPM0ADC(Resources res, RawData dma_buff,
                   std::initializer_list<DL_ADC12_MEM_IDX> mem_indices)
    : res_(res),
      scale_(0.0f),
      use_dma_(false),
      use_fifo_dma_(false),
      dma_channel_id_(DMA_CHANNEL_INVALID),
      num_channels_(0),
      filter_size_(0),
      dma_buffer_(),
      channels_(nullptr),
      mem_indices_(nullptr),
      locked_(0U)
{
  Initialize(dma_buff, mem_indices);
}

MSPM0ADC::~MSPM0ADC()
{
  if (res_.instance == nullptr)
  {
    delete[] channels_;
    delete[] mem_indices_;
    return;
  }

#if defined(DMA_BASE)
  if (use_dma_)
  {
    DL_DMA_disableChannel(DMA, dma_channel_id_);
  }
#endif

  DL_ADC12_stopConversion(res_.instance);
  DL_ADC12_disableDMA(res_.instance);
  DL_ADC12_disableDMATrigger(res_.instance, GetAllMemResultDmaTriggerMask());
  DL_ADC12_disableFIFO(res_.instance);

  delete[] channels_;
  delete[] mem_indices_;
}

void MSPM0ADC::Initialize(RawData dma_buff,
                          std::initializer_list<DL_ADC12_MEM_IDX> mem_indices)
{
  ASSERT(res_.instance != nullptr);
  ASSERT(res_.vref > 0.0f);
  ASSERT(mem_indices.size() > 0U);
  ASSERT(mem_indices.size() <= MSPM0_ADC_MEM_COUNT);

  if (res_.instance == nullptr || res_.vref <= 0.0f || mem_indices.size() == 0U ||
      mem_indices.size() > MSPM0_ADC_MEM_COUNT)
  {
    return;
  }

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

  num_channels_ = static_cast<uint8_t>(mem_indices.size());
  filter_size_ = CalculateFilterSize(dma_buff, num_channels_);
  dma_buffer_ = dma_buff;
  channels_ = new Channel[num_channels_];
  mem_indices_ = new DL_ADC12_MEM_IDX[num_channels_];

  uint8_t index = 0;
  for (DL_ADC12_MEM_IDX mem_idx : mem_indices)
  {
    ASSERT(IsValidMemIndex(mem_idx));
    channels_[index] = Channel(this, index, mem_idx);
    mem_indices_[index] = mem_idx;
    ++index;
  }

#if !defined(DMA_BASE)
  DL_ADC12_disableDMA(res_.instance);
  DL_ADC12_disableDMATrigger(res_.instance, GetAllMemResultDmaTriggerMask());
  DL_ADC12_disableFIFO(res_.instance);
  ConfigureSamplingMode(false, mem_indices_[0], mem_indices_[0]);
#else
  const uint32_t dma_trigger = GetAdcDmaTrigger(res_.instance);
  dma_channel_id_ = ResolveDmaChannelId(dma_trigger);

  const bool has_dma_channel =
      (dma_channel_id_ != DMA_CHANNEL_INVALID) && IsFullDmaChannel(dma_channel_id_);
  const uint32_t total_samples =
      static_cast<uint32_t>(num_channels_) * static_cast<uint32_t>(filter_size_);
  const bool contiguous_mem_range = IsContiguousMemRange(mem_indices_, num_channels_);
  use_fifo_dma_ = has_dma_channel && contiguous_mem_range &&
                  CanUseFifoDMA(dma_buffer_, num_channels_, filter_size_);
  use_dma_ = use_fifo_dma_ || (has_dma_channel && total_samples == 1U);

  const bool adc_dma_enabled = DL_ADC12_isDMAEnabled(res_.instance);
  if (has_dma_channel || adc_dma_enabled)
  {
    REQUIRE(use_dma_);
  }

  if (use_dma_)
  {
    StartContinuousDMA();
  }
  else
  {
    DL_ADC12_disableDMA(res_.instance);
    DL_ADC12_disableDMATrigger(res_.instance, GetAllMemResultDmaTriggerMask());
    DL_ADC12_disableFIFO(res_.instance);
    ConfigureSamplingMode(false, mem_indices_[0], mem_indices_[0]);
  }
#endif
}

MSPM0ADC::Channel& MSPM0ADC::GetChannel(uint8_t index)
{
  ASSERT(index < num_channels_);
  return channels_[index];
}

float MSPM0ADC::ReadChannel(uint8_t channel)
{
  ASSERT(channel < num_channels_);
  if (channel >= num_channels_)
  {
    return -1.0f;
  }

  if (use_dma_)
  {
    return ReadByDMA(channel);
  }

  return ReadByPolling(channel);
}

void MSPM0ADC::ConfigureSamplingMode(bool continuous, DL_ADC12_MEM_IDX start_mem_idx,
                                     DL_ADC12_MEM_IDX end_mem_idx)
{
  ASSERT(num_channels_ > 0U);
  const bool sequence = start_mem_idx != end_mem_idx;
  const uint32_t sample_mode = sequence ? (continuous ? DL_ADC12_SAMP_MODE_SEQUENCE_REPEAT
                                                      : DL_ADC12_SAMP_MODE_SEQUENCE)
                                        : (continuous ? DL_ADC12_SAMP_MODE_SINGLE_REPEAT
                                                      : DL_ADC12_SAMP_MODE_SINGLE);

  DL_ADC12_stopConversion(res_.instance);
  DL_ADC12_disableConversions(res_.instance);
  DL_Common_updateReg(
      &res_.instance->ULLMEM.CTL1,
      sample_mode | DL_ADC12_SAMPLING_SOURCE_AUTO | DL_ADC12_TRIG_SRC_SOFTWARE,
      ADC12_CTL1_SAMPMODE_MASK | ADC12_CTL1_CONSEQ_MASK | ADC12_CTL1_TRIGSRC_MASK);
  DL_Common_updateReg(
      &res_.instance->ULLMEM.CTL2,
      GetSequenceStartAddress(start_mem_idx) | GetSequenceEndAddress(end_mem_idx),
      ADC12_CTL2_STARTADD_MASK | ADC12_CTL2_ENDADD_MASK);
  DL_ADC12_enableConversions(res_.instance);
}

float MSPM0ADC::ReadByPolling(uint8_t channel)
{
  uint32_t expected = 0U;
  if (!locked_.compare_exchange_strong(expected, 0xF0F0F0F0U, std::memory_order_acquire,
                                       std::memory_order_relaxed))
  {
    ASSERT(false);
    return 0.0f;
  }

  uint16_t* buffer = reinterpret_cast<uint16_t*>(dma_buffer_.addr_);
  const DL_ADC12_MEM_IDX mem_idx = mem_indices_[channel];
  const uint32_t mem_interrupt = GetMemResultInterruptMask(mem_idx);
  uint32_t sum = 0U;

  ConfigureSamplingMode(false, mem_idx, mem_idx);

  for (uint8_t i = 0; i < filter_size_; ++i)
  {
    DL_ADC12_clearInterruptStatus(res_.instance, mem_interrupt);
    DL_ADC12_startConversion(res_.instance);

    uint32_t timeout = MSPM0_ADC_POLLING_TIMEOUT;
    while (DL_ADC12_getRawInterruptStatus(res_.instance, mem_interrupt) == 0U)
    {
      if (timeout-- == 0U)
      {
        DL_ADC12_stopConversion(res_.instance);
        locked_.store(0U, std::memory_order_release);
        ASSERT(false);
        return 0.0f;
      }
    }

    const uint16_t raw = DL_ADC12_getMemResult(res_.instance, mem_idx);
    DL_ADC12_stopConversion(res_.instance);
    if (buffer != nullptr)
    {
      buffer[channel + static_cast<uint32_t>(i) * num_channels_] = raw;
    }
    sum += raw;
    DL_ADC12_clearInterruptStatus(res_.instance, mem_interrupt);
  }

  DL_ADC12_stopConversion(res_.instance);
  locked_.store(0U, std::memory_order_release);

  return ConvertToVoltage(static_cast<float>(sum) / static_cast<float>(filter_size_));
}

float MSPM0ADC::ReadByDMA(uint8_t channel)
{
  volatile uint16_t* buffer = reinterpret_cast<volatile uint16_t*>(dma_buffer_.addr_);
  ASSERT(buffer != nullptr);
  if (buffer == nullptr)
  {
    return 0.0f;
  }

  uint32_t sum = 0U;
  for (uint8_t i = 0; i < filter_size_; ++i)
  {
    sum += buffer[channel + static_cast<uint32_t>(i) * num_channels_];
  }
  return ConvertToVoltage(static_cast<float>(sum) / static_cast<float>(filter_size_));
}

void MSPM0ADC::StartContinuousDMA()
{
#if defined(DMA_BASE) && defined(DEVICE_HAS_DMA_FULL_CHANNEL)
  const uint32_t all_dma_triggers = GetAllMemResultDmaTriggerMask();
  const uint32_t total_samples =
      static_cast<uint32_t>(num_channels_) * static_cast<uint32_t>(filter_size_);

  DL_ADC12_stopConversion(res_.instance);
  DL_ADC12_disableConversions(res_.instance);
  DL_ADC12_disableDMA(res_.instance);
  DL_ADC12_disableDMATrigger(res_.instance, all_dma_triggers);
  DL_DMA_disableChannel(DMA, dma_channel_id_);

  if (use_fifo_dma_)
  {
    ASSERT((total_samples % 2U) == 0U);
    DL_ADC12_enableFIFO(res_.instance);
    DL_ADC12_setDMASamplesCnt(res_.instance, 2U);

    DL_DMA_configTransfer(DMA, dma_channel_id_,
                          DL_DMA_FULL_CH_REPEAT_SINGLE_TRANSFER_MODE, DL_DMA_NORMAL_MODE,
                          DL_DMA_WIDTH_WORD, DL_DMA_WIDTH_WORD, DL_DMA_ADDR_UNCHANGED,
                          DL_DMA_ADDR_INCREMENT);
    DL_DMA_setSrcAddr(DMA, dma_channel_id_, DL_ADC12_getFIFOAddress(res_.instance));
    DL_DMA_setDestAddr(
        DMA, dma_channel_id_,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(dma_buffer_.addr_)));
    DL_DMA_setTransferSize(DMA, dma_channel_id_, total_samples / 2U);
    DL_ADC12_enableDMATrigger(res_.instance,
                              GetFifoDmaTriggerMask(mem_indices_, num_channels_));
  }
  else
  {
    ASSERT(total_samples == 1U);
    DL_ADC12_disableFIFO(res_.instance);
    DL_ADC12_setDMASamplesCnt(res_.instance, 1U);

    DL_DMA_configTransfer(DMA, dma_channel_id_,
                          DL_DMA_FULL_CH_REPEAT_SINGLE_TRANSFER_MODE, DL_DMA_NORMAL_MODE,
                          DL_DMA_WIDTH_HALF_WORD, DL_DMA_WIDTH_HALF_WORD,
                          DL_DMA_ADDR_UNCHANGED, DL_DMA_ADDR_UNCHANGED);
    DL_DMA_setSrcAddr(DMA, dma_channel_id_,
                      static_cast<uint32_t>(
                          DL_ADC12_getMemResultAddress(res_.instance, mem_indices_[0])));
    DL_DMA_setDestAddr(
        DMA, dma_channel_id_,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(dma_buffer_.addr_)));
    DL_DMA_setTransferSize(DMA, dma_channel_id_, 1U);
    DL_ADC12_enableDMATrigger(res_.instance, GetMemResultDmaTriggerMask(mem_indices_[0]));
  }

  ConfigureSamplingMode(true, mem_indices_[0], mem_indices_[num_channels_ - 1U]);
  DL_DMA_enableChannel(DMA, dma_channel_id_);
  DL_ADC12_enableDMA(res_.instance);
  DL_ADC12_clearDMATriggerStatus(res_.instance, all_dma_triggers);
  DL_ADC12_clearInterruptStatus(res_.instance, DL_ADC12_INTERRUPT_DMA_DONE);
  DL_ADC12_startConversion(res_.instance);
#else
  ASSERT(false);
#endif
}

float MSPM0ADC::ConvertToVoltage(float adc_value) const { return adc_value * scale_; }

#endif
