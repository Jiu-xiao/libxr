#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <span>

#include "../../fake_mspm0_runtime.hpp"

struct DMA_CPU_INT_Regs
{
  uint32_t IMASK = 0U;
  uint32_t RIS = 0U;
  uint32_t MIS = 0U;
  uint32_t ICLR = 0U;
};

struct DMA_Regs
{
  DMA_CPU_INT_Regs CPU_INT{};
};

enum DL_DMA_TRANSFER_MODE : uint32_t
{
  DL_DMA_SINGLE_TRANSFER_MODE = 0U,
  DL_DMA_SINGLE_BLOCK_TRANSFER_MODE = 1U,
  DL_DMA_FULL_CH_REPEAT_SINGLE_TRANSFER_MODE = 2U,
  DL_DMA_FULL_CH_REPEAT_BLOCK_TRANSFER_MODE = 3U,
};

enum DL_DMA_EXTENDED_MODE : uint32_t
{
  DL_DMA_NORMAL_MODE = 0U,
  DL_DMA_FULL_CH_FILL_MODE = 1U,
  DL_DMA_FULL_CH_TABLE_MODE = 2U,
};

enum DL_DMA_INCREMENT : uint32_t
{
  DL_DMA_ADDR_UNCHANGED = 0U,
  DL_DMA_ADDR_DECREMENT = 1U,
  DL_DMA_ADDR_INCREMENT = 2U,
  DL_DMA_ADDR_STRIDE_2 = 3U,
  DL_DMA_ADDR_STRIDE_3 = 4U,
  DL_DMA_ADDR_STRIDE_4 = 5U,
  DL_DMA_ADDR_STRIDE_5 = 6U,
  DL_DMA_ADDR_STRIDE_6 = 7U,
  DL_DMA_ADDR_STRIDE_7 = 8U,
  DL_DMA_ADDR_STRIDE_8 = 9U,
  DL_DMA_ADDR_STRIDE_9 = 10U,
};

enum DL_DMA_EARLY_INTERRUPT_THRESHOLD : uint32_t
{
  DL_DMA_EARLY_INTERRUPT_THRESHOLD_DISABLED = 0U,
  DL_DMA_EARLY_INTERRUPT_THRESHOLD_1 = 1U,
  DL_DMA_EARLY_INTERRUPT_THRESHOLD_2 = 2U,
  DL_DMA_EARLY_INTERRUPT_THRESHOLD_4 = 3U,
  DL_DMA_EARLY_INTERRUPT_THRESHOLD_8 = 4U,
  DL_DMA_EARLY_INTERRUPT_THRESHOLD_32 = 5U,
  DL_DMA_EARLY_INTERRUPT_THRESHOLD_64 = 6U,
  DL_DMA_EARLY_INTERRUPT_THRESHOLD_HALF = 7U,
};

enum DL_DMA_TRIGGER_TYPE : uint32_t
{
  DL_DMA_TRIGGER_TYPE_INTERNAL = 0U,
  DL_DMA_TRIGGER_TYPE_EXTERNAL = 1U,
};

enum DL_DMA_WIDTH : uint32_t
{
  DL_DMA_WIDTH_BYTE = 0U,
  DL_DMA_WIDTH_HALF_WORD = 1U,
  DL_DMA_WIDTH_WORD = 2U,
  DL_DMA_WIDTH_LONG = 3U,
};

#define DL_DMA_INTERRUPT_CHANNEL0 (1UL << 0U)
#define DL_DMA_INTERRUPT_CHANNEL1 (1UL << 1U)
#define DL_DMA_INTERRUPT_CHANNEL2 (1UL << 2U)
#define DL_DMA_INTERRUPT_CHANNEL3 (1UL << 3U)
#define DL_DMA_INTERRUPT_CHANNEL4 (1UL << 4U)
#define DL_DMA_INTERRUPT_CHANNEL5 (1UL << 5U)
#define DL_DMA_INTERRUPT_CHANNEL6 (1UL << 6U)
#define DL_DMA_INTERRUPT_CHANNEL7 (1UL << 7U)
#define DL_DMA_INTERRUPT_CHANNEL8 (1UL << 8U)
#define DL_DMA_INTERRUPT_CHANNEL9 (1UL << 9U)
#define DL_DMA_INTERRUPT_CHANNEL10 (1UL << 10U)
#define DL_DMA_INTERRUPT_CHANNEL11 (1UL << 11U)

#define DL_DMA_FULL_CH_INTERRUPT_EARLY_CHANNEL0 (1UL << 16U)
#define DL_DMA_FULL_CH_INTERRUPT_EARLY_CHANNEL1 (1UL << 17U)
#define DL_DMA_FULL_CH_INTERRUPT_EARLY_CHANNEL2 (1UL << 18U)
#define DL_DMA_FULL_CH_INTERRUPT_EARLY_CHANNEL3 (1UL << 19U)
#define DL_DMA_FULL_CH_INTERRUPT_EARLY_CHANNEL4 (1UL << 20U)
#define DL_DMA_FULL_CH_INTERRUPT_EARLY_CHANNEL5 (1UL << 21U)
#define DL_DMA_FULL_CH_INTERRUPT_EARLY_CHANNEL6 (1UL << 22U)
#define DL_DMA_FULL_CH_INTERRUPT_EARLY_CHANNEL7 (1UL << 23U)

#define DL_DMA_INTERRUPT_ADDR_ERROR (1UL << 24U)
#define DL_DMA_INTERRUPT_DATA_ERROR (1UL << 25U)

#if defined(FAKE_MSPM0_G3519)
#define DMA_SYS_N_DMA_CHANNEL 12U
#define DMA_SYS_N_DMA_FULL_CHANNEL 6U
#else
#define DMA_SYS_N_DMA_CHANNEL 7U
#define DMA_SYS_N_DMA_FULL_CHANNEL 3U
#endif

struct DL_DMA_Config
{
  uint8_t trigger;
  DL_DMA_TRIGGER_TYPE triggerType;
  DL_DMA_TRANSFER_MODE transferMode;
  DL_DMA_EXTENDED_MODE extendedMode;
  DL_DMA_WIDTH srcWidth;
  DL_DMA_WIDTH destWidth;
  DL_DMA_INCREMENT srcIncrement;
  DL_DMA_INCREMENT destIncrement;
};

namespace FakeMSPM0
{

inline constexpr size_t kDmaChannelCount = DMA_SYS_N_DMA_CHANNEL;
inline constexpr size_t kDmaFullChannelCount = DMA_SYS_N_DMA_FULL_CHANNEL;
inline constexpr size_t kDmaHistoryCapacity = 64U;
inline constexpr size_t kDmaHostRegionCapacity = 64U;
inline constexpr uint8_t kDmaControllerCall = std::numeric_limits<uint8_t>::max();
inline constexpr uint32_t kInvalidMcuAddress = std::numeric_limits<uint32_t>::max();

enum class DmaFault : uint8_t
{
  NONE,
  INVALID_CHANNEL,
  NULL_CONFIG,
  UNSUPPORTED_MODE,
  BASIC_CHANNEL_FULL_FEATURE,
  HISTORY_OVERFLOW,
  HOST_REGION_OVERFLOW,
  HOST_ADDRESS_COLLISION,
  HOST_ADDRESS_WRAP,
  CHANNEL_DISABLED,
  ZERO_TRANSFER_SIZE,
  UNREGISTERED_DESTINATION,
  NON_BYTE_RX_TRANSFER,
};

enum class DmaCall : uint8_t
{
  INIT_CHANNEL,
  CONFIG_TRANSFER,
  CONFIG_MODE,
  SET_TRANSFER_MODE,
  GET_TRANSFER_MODE,
  SET_EXTENDED_MODE,
  GET_EXTENDED_MODE,
  SET_TRIGGER,
  GET_TRIGGER,
  GET_TRIGGER_TYPE,
  SET_SOURCE,
  GET_SOURCE,
  SET_DESTINATION,
  GET_DESTINATION,
  SET_TRANSFER_SIZE,
  GET_TRANSFER_SIZE,
  SET_SOURCE_INCREMENT,
  GET_SOURCE_INCREMENT,
  SET_DESTINATION_INCREMENT,
  GET_DESTINATION_INCREMENT,
  SET_SOURCE_WIDTH,
  GET_SOURCE_WIDTH,
  SET_DESTINATION_WIDTH,
  GET_DESTINATION_WIDTH,
  SET_EARLY_THRESHOLD,
  GET_EARLY_THRESHOLD,
  ENABLE_CHANNEL,
  DISABLE_CHANNEL,
  IS_CHANNEL_ENABLED,
  START_TRANSFER,
  ENABLE_INTERRUPT,
  DISABLE_INTERRUPT,
  GET_ENABLED_INTERRUPTS,
  GET_ENABLED_INTERRUPT_STATUS,
  GET_RAW_INTERRUPT_STATUS,
  CLEAR_INTERRUPT_STATUS,
};

struct DmaChannelState
{
  DL_DMA_Config config{};
  DL_DMA_EARLY_INTERRUPT_THRESHOLD early_threshold =
      DL_DMA_EARLY_INTERRUPT_THRESHOLD_DISABLED;
  uint32_t source = 0U;
  uint32_t destination = 0U;
  uint32_t programmed_source = 0U;
  uint32_t programmed_destination = 0U;
  uint16_t transfer_size = 0U;
  uint16_t programmed_transfer_size = 0U;
  uint32_t init_calls = 0U;
  uint32_t enable_calls = 0U;
  uint32_t rejected_enable_calls = 0U;
  uint32_t disable_calls = 0U;
  uint32_t clear_calls = 0U;
  uint32_t full_clear_calls = 0U;
  uint32_t early_clear_calls = 0U;
  uint32_t full_raise_count = 0U;
  uint32_t early_raise_count = 0U;
  uint32_t wrap_count = 0U;
  size_t transferred_bytes = 0U;
  std::array<uint32_t, kDmaHistoryCapacity> source_history{};
  std::array<uint32_t, kDmaHistoryCapacity> destination_history{};
  std::array<uint16_t, kDmaHistoryCapacity> size_history{};
  size_t history_size = 0U;
  bool full_channel = false;
  bool configuration_valid = true;
  bool enabled = false;
};

struct DmaCallTrace
{
  DmaCall call = DmaCall::INIT_CHANNEL;
  uint8_t channel = kDmaControllerCall;
  UART_Regs* uart_owner = nullptr;
  uint32_t ipsr = 0U;
};

struct HostMemoryRegion
{
  uint32_t mcu_address = 0U;
  std::byte* host_address = nullptr;
  size_t size = 0U;
};

using DmaAccessHook = void (*)(DmaCall, uint8_t);
using DmaClearHook = void (*)(uint32_t);
using DmaEnableHook = void (*)(uint8_t);
using DmaIrqHook = void (*)(uint32_t);

inline DMA_Regs dma_regs{};

inline std::array<DmaChannelState, kDmaChannelCount> MakeDmaChannels()
{
  std::array<DmaChannelState, kDmaChannelCount> channels{};
  for (size_t index = 0U; index < channels.size(); ++index)
  {
    channels[index].full_channel = index < kDmaFullChannelCount;
  }
  return channels;
}

inline std::array<DmaChannelState, kDmaChannelCount> dma_channels = MakeDmaChannels();
inline std::array<HostMemoryRegion, kDmaHostRegionCapacity> dma_host_regions{};
inline size_t dma_host_region_count = 0U;
inline std::array<uint32_t, kDmaHistoryCapacity> dma_clear_history{};
inline size_t dma_clear_history_size = 0U;
inline std::array<uint32_t, kDmaHistoryCapacity> dma_raise_history{};
inline size_t dma_raise_history_size = 0U;
inline uint32_t dma_cleared_interrupts = 0U;
inline uint32_t dma_last_advance_causes = 0U;
inline size_t dma_last_advance_count = 0U;
inline DmaFault dma_last_fault = DmaFault::NONE;
inline size_t dma_fault_count = 0U;
inline DmaAccessHook dma_access_hook = nullptr;
inline DmaClearHook dma_clear_hook = nullptr;
inline DmaEnableHook dma_enable_hook = nullptr;
inline DmaIrqHook dma_irq_hook = nullptr;
inline std::array<DmaCallTrace, 4096U> dma_call_trace{};
inline size_t dma_call_trace_size = 0U;

inline void SetDmaFault(DmaFault fault)
{
  dma_last_fault = fault;
  ++dma_fault_count;
}

inline void RecordDmaCall(DmaCall call, uint8_t channel)
{
  if (dma_call_trace_size < dma_call_trace.size())
  {
    dma_call_trace[dma_call_trace_size++] =
        DmaCallTrace{call, channel, active_uart_owner, ipsr};
  }
  else
  {
    SetDmaFault(DmaFault::HISTORY_OVERFLOW);
  }
  if (dma_access_hook != nullptr)
  {
    dma_access_hook(call, channel);
  }
}

inline bool IsValidChannel(uint8_t channel) { return channel < dma_channels.size(); }

inline bool IsFullChannel(uint8_t channel) { return channel < kDmaFullChannelCount; }

inline uint32_t ChannelCause(uint8_t channel)
{
  return IsValidChannel(channel) ? (1UL << channel) : 0U;
}

inline uint32_t EarlyCause(uint8_t channel)
{
  return channel < 8U ? (1UL << (16U + channel)) : 0U;
}

inline void RefreshMis(DMA_Regs* dma)
{
  dma->CPU_INT.MIS = dma->CPU_INT.RIS & dma->CPU_INT.IMASK;
}

inline void NotifyDmaIrqIfPending(DMA_Regs* dma)
{
  if (dma->CPU_INT.MIS == 0U)
  {
    return;
  }
  SetPeripheralPending(DMA_INT_IRQn);
  if (dma_irq_hook != nullptr)
  {
    dma_irq_hook(dma->CPU_INT.MIS);
  }
}

inline void RepublishLevelOnIrqExit(IRQn_Type irqn)
{
  if (!ValidIrq(irqn))
  {
    return;
  }

  bool level_pending = irqn == DMA_INT_IRQn && dma_regs.CPU_INT.MIS != 0U;
  if (!level_pending)
  {
    for (const auto& uart : uart_regs)
    {
      if (uart.irqn == irqn && uart.CPU_INT.MIS != 0U)
      {
        level_pending = true;
        break;
      }
    }
  }

  const size_t index = static_cast<size_t>(irqn);
  nvic_pending[index] = nvic_software_pending[index] || level_pending;
}

inline bool IsRepeatSingle(const DmaChannelState& state)
{
  return state.config.transferMode == DL_DMA_FULL_CH_REPEAT_SINGLE_TRANSFER_MODE;
}

inline bool ValidateConfiguration(uint8_t channel)
{
  auto& state = dma_channels[channel];
  bool valid = state.config.extendedMode == DL_DMA_NORMAL_MODE;

  switch (state.config.transferMode)
  {
    case DL_DMA_SINGLE_TRANSFER_MODE:
    case DL_DMA_SINGLE_BLOCK_TRANSFER_MODE:
      break;
    case DL_DMA_FULL_CH_REPEAT_SINGLE_TRANSFER_MODE:
      if (!state.full_channel)
      {
        SetDmaFault(DmaFault::BASIC_CHANNEL_FULL_FEATURE);
        valid = false;
      }
      break;
    case DL_DMA_FULL_CH_REPEAT_BLOCK_TRANSFER_MODE:
    default:
      SetDmaFault(DmaFault::UNSUPPORTED_MODE);
      valid = false;
      break;
  }

  if (state.early_threshold != DL_DMA_EARLY_INTERRUPT_THRESHOLD_DISABLED &&
      !state.full_channel)
  {
    SetDmaFault(DmaFault::BASIC_CHANNEL_FULL_FEATURE);
    valid = false;
  }

  state.configuration_valid = valid;
  return valid;
}

inline void ResetDma()
{
  dma_regs = {};
  dma_channels = MakeDmaChannels();
  dma_host_regions = {};
  dma_host_region_count = 0U;
  dma_clear_history = {};
  dma_clear_history_size = 0U;
  dma_raise_history = {};
  dma_raise_history_size = 0U;
  dma_cleared_interrupts = 0U;
  dma_last_advance_causes = 0U;
  dma_last_advance_count = 0U;
  dma_last_fault = DmaFault::NONE;
  dma_fault_count = 0U;
  dma_access_hook = nullptr;
  dma_clear_hook = nullptr;
  dma_enable_hook = nullptr;
  dma_irq_hook = nullptr;
  dma_call_trace = {};
  dma_call_trace_size = 0U;
  irq_exit_hook = RepublishLevelOnIrqExit;
}

inline void ResetDmaCallTrace()
{
  dma_call_trace = {};
  dma_call_trace_size = 0U;
}

inline uint32_t ToMcuAddress(const void* host_address)
{
  return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(host_address));
}

inline bool RegisterHostMemoryAt(uint32_t mcu_address, void* host_address, size_t size)
{
  if (host_address == nullptr || size == 0U)
  {
    SetDmaFault(DmaFault::UNREGISTERED_DESTINATION);
    return false;
  }

  const uint64_t new_begin = mcu_address;
  const uint64_t new_end = new_begin + size;
  if (new_end > (uint64_t{1U} << 32U))
  {
    SetDmaFault(DmaFault::HOST_ADDRESS_WRAP);
    return false;
  }

  auto* const new_host = static_cast<std::byte*>(host_address);
  for (size_t index = 0U; index < dma_host_region_count; ++index)
  {
    const auto& existing = dma_host_regions[index];
    const uint64_t existing_begin = existing.mcu_address;
    const uint64_t existing_end = existing_begin + existing.size;
    const uint64_t overlap_begin =
        new_begin > existing_begin ? new_begin : existing_begin;
    const uint64_t overlap_end = new_end < existing_end ? new_end : existing_end;
    if (overlap_begin >= overlap_end)
    {
      continue;
    }

    const auto* const existing_overlap =
        existing.host_address + static_cast<size_t>(overlap_begin - existing_begin);
    const auto* const new_overlap =
        new_host + static_cast<size_t>(overlap_begin - new_begin);
    if (existing_overlap != new_overlap)
    {
      SetDmaFault(DmaFault::HOST_ADDRESS_COLLISION);
      return false;
    }

    if (new_begin >= existing_begin && new_end <= existing_end)
    {
      return true;
    }
  }

  if (dma_host_region_count >= dma_host_regions.size())
  {
    SetDmaFault(DmaFault::HOST_REGION_OVERFLOW);
    return false;
  }

  dma_host_regions[dma_host_region_count++] =
      HostMemoryRegion{mcu_address, new_host, size};
  return true;
}

inline uint32_t RegisterHostMemory(void* host_address, size_t size)
{
  const uint32_t mcu_address = ToMcuAddress(host_address);
  return RegisterHostMemoryAt(mcu_address, host_address, size) ? mcu_address
                                                               : kInvalidMcuAddress;
}

inline std::byte* ResolveHostMemory(uint32_t mcu_address, size_t size = 1U)
{
  const uint64_t requested_begin = mcu_address;
  const uint64_t requested_end = requested_begin + size;
  if (requested_end > (uint64_t{1U} << 32U))
  {
    return nullptr;
  }

  for (size_t index = 0U; index < dma_host_region_count; ++index)
  {
    const auto& region = dma_host_regions[index];
    const uint64_t region_begin = region.mcu_address;
    const uint64_t region_end = region_begin + region.size;
    if (requested_begin >= region_begin && requested_end <= region_end)
    {
      return region.host_address + static_cast<size_t>(requested_begin - region_begin);
    }
  }
  return nullptr;
}

inline void RaiseDmaInterrupt(uint32_t cause_mask)
{
  if (dma_raise_history_size < dma_raise_history.size())
  {
    dma_raise_history[dma_raise_history_size++] = cause_mask;
  }
  else
  {
    SetDmaFault(DmaFault::HISTORY_OVERFLOW);
  }

  for (uint8_t channel = 0U; channel < dma_channels.size(); ++channel)
  {
    if ((cause_mask & ChannelCause(channel)) != 0U)
    {
      ++dma_channels[channel].full_raise_count;
    }
    if ((cause_mask & EarlyCause(channel)) != 0U)
    {
      ++dma_channels[channel].early_raise_count;
    }
  }

  dma_regs.CPU_INT.RIS |= cause_mask;
  RefreshMis(&dma_regs);
  NotifyDmaIrqIfPending(&dma_regs);
}

inline size_t WidthBytes(DL_DMA_WIDTH width)
{
  switch (width)
  {
    case DL_DMA_WIDTH_BYTE:
      return 1U;
    case DL_DMA_WIDTH_HALF_WORD:
      return 2U;
    case DL_DMA_WIDTH_WORD:
      return 4U;
    case DL_DMA_WIDTH_LONG:
      return 8U;
    default:
      return 0U;
  }
}

inline uint32_t StepAddress(uint32_t address, DL_DMA_INCREMENT increment,
                            DL_DMA_WIDTH width)
{
  const uint32_t width_bytes = static_cast<uint32_t>(WidthBytes(width));
  switch (increment)
  {
    case DL_DMA_ADDR_UNCHANGED:
      return address;
    case DL_DMA_ADDR_DECREMENT:
      return address - width_bytes;
    case DL_DMA_ADDR_INCREMENT:
      return address + width_bytes;
    case DL_DMA_ADDR_STRIDE_2:
      return address + 2U * width_bytes;
    case DL_DMA_ADDR_STRIDE_3:
      return address + 3U * width_bytes;
    case DL_DMA_ADDR_STRIDE_4:
      return address + 4U * width_bytes;
    case DL_DMA_ADDR_STRIDE_5:
      return address + 5U * width_bytes;
    case DL_DMA_ADDR_STRIDE_6:
      return address + 6U * width_bytes;
    case DL_DMA_ADDR_STRIDE_7:
      return address + 7U * width_bytes;
    case DL_DMA_ADDR_STRIDE_8:
      return address + 8U * width_bytes;
    case DL_DMA_ADDR_STRIDE_9:
      return address + 9U * width_bytes;
    default:
      return address;
  }
}

inline bool AdvanceRx(uint8_t channel, std::span<const uint8_t> bytes)
{
  dma_last_advance_causes = 0U;
  dma_last_advance_count = 0U;
  if (!IsValidChannel(channel))
  {
    SetDmaFault(DmaFault::INVALID_CHANNEL);
    return false;
  }

  auto& state = dma_channels[channel];
  if (bytes.empty())
  {
    return true;
  }
  if (!state.enabled)
  {
    SetDmaFault(DmaFault::CHANNEL_DISABLED);
    return false;
  }
  if (!state.configuration_valid)
  {
    SetDmaFault(DmaFault::UNSUPPORTED_MODE);
    return false;
  }
  if (state.config.srcWidth != DL_DMA_WIDTH_BYTE ||
      state.config.destWidth != DL_DMA_WIDTH_BYTE)
  {
    SetDmaFault(DmaFault::NON_BYTE_RX_TRANSFER);
    return false;
  }

  for (const uint8_t value : bytes)
  {
    if (!state.enabled)
    {
      SetDmaFault(DmaFault::CHANNEL_DISABLED);
      return false;
    }
    if (state.transfer_size == 0U)
    {
      SetDmaFault(DmaFault::ZERO_TRANSFER_SIZE);
      return false;
    }

    auto* const destination = ResolveHostMemory(state.destination);
    if (destination == nullptr)
    {
      SetDmaFault(DmaFault::UNREGISTERED_DESTINATION);
      return false;
    }
    *destination = static_cast<std::byte>(value);

    state.source =
        StepAddress(state.source, state.config.srcIncrement, state.config.srcWidth);
    state.destination = StepAddress(state.destination, state.config.destIncrement,
                                    state.config.destWidth);
    --state.transfer_size;
    ++state.transferred_bytes;
    ++dma_last_advance_count;

    const uint16_t half_size = state.programmed_transfer_size / 2U;
    if (state.early_threshold == DL_DMA_EARLY_INTERRUPT_THRESHOLD_HALF &&
        state.transfer_size == half_size)
    {
      const uint32_t cause = EarlyCause(channel);
      dma_last_advance_causes |= cause;
      RaiseDmaInterrupt(cause);
    }

    if (state.transfer_size != 0U)
    {
      continue;
    }

    const uint32_t cause = ChannelCause(channel);
    dma_last_advance_causes |= cause;
    if (IsRepeatSingle(state))
    {
      state.source = state.programmed_source;
      state.destination = state.programmed_destination;
      state.transfer_size = state.programmed_transfer_size;
      ++state.wrap_count;
    }
    else
    {
      state.enabled = false;
    }
    RaiseDmaInterrupt(cause);
  }
  return true;
}

inline bool AdvanceRx(uint8_t channel, std::initializer_list<uint8_t> bytes)
{
  return AdvanceRx(channel, std::span<const uint8_t>{bytes.begin(), bytes.size()});
}

}  // namespace FakeMSPM0

inline DMA_Regs* const DMA = &FakeMSPM0::dma_regs;

inline void DL_DMA_initChannel(DMA_Regs*, uint8_t channel, const DL_DMA_Config* config)
{
  FakeMSPM0::RecordDmaCall(FakeMSPM0::DmaCall::INIT_CHANNEL, channel);
  if (!FakeMSPM0::IsValidChannel(channel))
  {
    FakeMSPM0::SetDmaFault(FakeMSPM0::DmaFault::INVALID_CHANNEL);
    return;
  }
  if (config == nullptr)
  {
    FakeMSPM0::SetDmaFault(FakeMSPM0::DmaFault::NULL_CONFIG);
    return;
  }

  auto& state = FakeMSPM0::dma_channels[channel];
  state.config = *config;
  ++state.init_calls;
  FakeMSPM0::ValidateConfiguration(channel);
}

inline void DL_DMA_configTransfer(DMA_Regs*, uint8_t channel,
                                  DL_DMA_TRANSFER_MODE transfer_mode,
                                  DL_DMA_EXTENDED_MODE extended_mode,
                                  DL_DMA_WIDTH source_width,
                                  DL_DMA_WIDTH destination_width,
                                  DL_DMA_INCREMENT source_increment,
                                  DL_DMA_INCREMENT destination_increment)
{
  FakeMSPM0::RecordDmaCall(FakeMSPM0::DmaCall::CONFIG_TRANSFER, channel);
  if (!FakeMSPM0::IsValidChannel(channel))
  {
    FakeMSPM0::SetDmaFault(FakeMSPM0::DmaFault::INVALID_CHANNEL);
    return;
  }
  auto& config = FakeMSPM0::dma_channels[channel].config;
  config.transferMode = transfer_mode;
  config.extendedMode = extended_mode;
  config.srcWidth = source_width;
  config.destWidth = destination_width;
  config.srcIncrement = source_increment;
  config.destIncrement = destination_increment;
  FakeMSPM0::ValidateConfiguration(channel);
}

inline void DL_DMA_configMode(DMA_Regs*, uint8_t channel,
                              DL_DMA_TRANSFER_MODE transfer_mode,
                              DL_DMA_EXTENDED_MODE extended_mode)
{
  FakeMSPM0::RecordDmaCall(FakeMSPM0::DmaCall::CONFIG_MODE, channel);
  if (!FakeMSPM0::IsValidChannel(channel))
  {
    FakeMSPM0::SetDmaFault(FakeMSPM0::DmaFault::INVALID_CHANNEL);
    return;
  }
  auto& config = FakeMSPM0::dma_channels[channel].config;
  config.transferMode = transfer_mode;
  config.extendedMode = extended_mode;
  FakeMSPM0::ValidateConfiguration(channel);
}

inline void DL_DMA_setTransferMode(DMA_Regs*, uint8_t channel,
                                   DL_DMA_TRANSFER_MODE transfer_mode)
{
  FakeMSPM0::RecordDmaCall(FakeMSPM0::DmaCall::SET_TRANSFER_MODE, channel);
  if (!FakeMSPM0::IsValidChannel(channel))
  {
    FakeMSPM0::SetDmaFault(FakeMSPM0::DmaFault::INVALID_CHANNEL);
    return;
  }
  FakeMSPM0::dma_channels[channel].config.transferMode = transfer_mode;
  FakeMSPM0::ValidateConfiguration(channel);
}

inline DL_DMA_TRANSFER_MODE DL_DMA_getTransferMode(const DMA_Regs*, uint8_t channel)
{
  FakeMSPM0::RecordDmaCall(FakeMSPM0::DmaCall::GET_TRANSFER_MODE, channel);
  if (!FakeMSPM0::IsValidChannel(channel))
  {
    FakeMSPM0::SetDmaFault(FakeMSPM0::DmaFault::INVALID_CHANNEL);
    return DL_DMA_SINGLE_TRANSFER_MODE;
  }
  return FakeMSPM0::dma_channels[channel].config.transferMode;
}

inline void DL_DMA_setExtendedMode(DMA_Regs*, uint8_t channel,
                                   DL_DMA_EXTENDED_MODE extended_mode)
{
  FakeMSPM0::RecordDmaCall(FakeMSPM0::DmaCall::SET_EXTENDED_MODE, channel);
  if (!FakeMSPM0::IsValidChannel(channel))
  {
    FakeMSPM0::SetDmaFault(FakeMSPM0::DmaFault::INVALID_CHANNEL);
    return;
  }
  FakeMSPM0::dma_channels[channel].config.extendedMode = extended_mode;
  FakeMSPM0::ValidateConfiguration(channel);
}

inline DL_DMA_EXTENDED_MODE DL_DMA_getExtendedMode(const DMA_Regs*, uint8_t channel)
{
  FakeMSPM0::RecordDmaCall(FakeMSPM0::DmaCall::GET_EXTENDED_MODE, channel);
  if (!FakeMSPM0::IsValidChannel(channel))
  {
    FakeMSPM0::SetDmaFault(FakeMSPM0::DmaFault::INVALID_CHANNEL);
    return DL_DMA_NORMAL_MODE;
  }
  return FakeMSPM0::dma_channels[channel].config.extendedMode;
}

inline void DL_DMA_setTrigger(DMA_Regs*, uint8_t channel, uint8_t trigger,
                              DL_DMA_TRIGGER_TYPE trigger_type)
{
  FakeMSPM0::RecordDmaCall(FakeMSPM0::DmaCall::SET_TRIGGER, channel);
  if (!FakeMSPM0::IsValidChannel(channel))
  {
    FakeMSPM0::SetDmaFault(FakeMSPM0::DmaFault::INVALID_CHANNEL);
    return;
  }
  auto& config = FakeMSPM0::dma_channels[channel].config;
  config.trigger = trigger;
  config.triggerType = trigger_type;
}

inline uint32_t DL_DMA_getTrigger(const DMA_Regs*, uint8_t channel)
{
  FakeMSPM0::RecordDmaCall(FakeMSPM0::DmaCall::GET_TRIGGER, channel);
  if (!FakeMSPM0::IsValidChannel(channel))
  {
    FakeMSPM0::SetDmaFault(FakeMSPM0::DmaFault::INVALID_CHANNEL);
    return 0U;
  }
  return FakeMSPM0::dma_channels[channel].config.trigger;
}

inline DL_DMA_TRIGGER_TYPE DL_DMA_getTriggerType(const DMA_Regs*, uint8_t channel)
{
  FakeMSPM0::RecordDmaCall(FakeMSPM0::DmaCall::GET_TRIGGER_TYPE, channel);
  if (!FakeMSPM0::IsValidChannel(channel))
  {
    FakeMSPM0::SetDmaFault(FakeMSPM0::DmaFault::INVALID_CHANNEL);
    return DL_DMA_TRIGGER_TYPE_INTERNAL;
  }
  return FakeMSPM0::dma_channels[channel].config.triggerType;
}

inline void DL_DMA_setSrcAddr(DMA_Regs*, uint8_t channel, uint32_t address)
{
  FakeMSPM0::RecordDmaCall(FakeMSPM0::DmaCall::SET_SOURCE, channel);
  if (!FakeMSPM0::IsValidChannel(channel))
  {
    FakeMSPM0::SetDmaFault(FakeMSPM0::DmaFault::INVALID_CHANNEL);
    return;
  }
  auto& state = FakeMSPM0::dma_channels[channel];
  state.programmed_source = address;
  state.source = address;
}

inline uint32_t DL_DMA_getSrcAddr(const DMA_Regs*, uint8_t channel)
{
  FakeMSPM0::RecordDmaCall(FakeMSPM0::DmaCall::GET_SOURCE, channel);
  if (!FakeMSPM0::IsValidChannel(channel))
  {
    FakeMSPM0::SetDmaFault(FakeMSPM0::DmaFault::INVALID_CHANNEL);
    return 0U;
  }
  return FakeMSPM0::dma_channels[channel].source;
}

inline void DL_DMA_setDestAddr(DMA_Regs*, uint8_t channel, uint32_t address)
{
  FakeMSPM0::RecordDmaCall(FakeMSPM0::DmaCall::SET_DESTINATION, channel);
  if (!FakeMSPM0::IsValidChannel(channel))
  {
    FakeMSPM0::SetDmaFault(FakeMSPM0::DmaFault::INVALID_CHANNEL);
    return;
  }
  auto& state = FakeMSPM0::dma_channels[channel];
  state.programmed_destination = address;
  state.destination = address;
}

inline uint32_t DL_DMA_getDestAddr(const DMA_Regs*, uint8_t channel)
{
  FakeMSPM0::RecordDmaCall(FakeMSPM0::DmaCall::GET_DESTINATION, channel);
  if (!FakeMSPM0::IsValidChannel(channel))
  {
    FakeMSPM0::SetDmaFault(FakeMSPM0::DmaFault::INVALID_CHANNEL);
    return 0U;
  }
  return FakeMSPM0::dma_channels[channel].destination;
}

inline void DL_DMA_setTransferSize(DMA_Regs*, uint8_t channel, uint16_t size)
{
  FakeMSPM0::RecordDmaCall(FakeMSPM0::DmaCall::SET_TRANSFER_SIZE, channel);
  if (!FakeMSPM0::IsValidChannel(channel))
  {
    FakeMSPM0::SetDmaFault(FakeMSPM0::DmaFault::INVALID_CHANNEL);
    return;
  }
  auto& state = FakeMSPM0::dma_channels[channel];
  state.programmed_transfer_size = size;
  state.transfer_size = size;
}

inline uint16_t DL_DMA_getTransferSize(const DMA_Regs*, uint8_t channel)
{
  FakeMSPM0::RecordDmaCall(FakeMSPM0::DmaCall::GET_TRANSFER_SIZE, channel);
  if (!FakeMSPM0::IsValidChannel(channel))
  {
    FakeMSPM0::SetDmaFault(FakeMSPM0::DmaFault::INVALID_CHANNEL);
    return 0U;
  }
  return FakeMSPM0::dma_channels[channel].transfer_size;
}

inline void DL_DMA_setSrcIncrement(DMA_Regs*, uint8_t channel, DL_DMA_INCREMENT increment)
{
  FakeMSPM0::RecordDmaCall(FakeMSPM0::DmaCall::SET_SOURCE_INCREMENT, channel);
  if (!FakeMSPM0::IsValidChannel(channel))
  {
    FakeMSPM0::SetDmaFault(FakeMSPM0::DmaFault::INVALID_CHANNEL);
    return;
  }
  FakeMSPM0::dma_channels[channel].config.srcIncrement = increment;
}

inline DL_DMA_INCREMENT DL_DMA_getSrcIncrement(const DMA_Regs*, uint8_t channel)
{
  FakeMSPM0::RecordDmaCall(FakeMSPM0::DmaCall::GET_SOURCE_INCREMENT, channel);
  if (!FakeMSPM0::IsValidChannel(channel))
  {
    FakeMSPM0::SetDmaFault(FakeMSPM0::DmaFault::INVALID_CHANNEL);
    return DL_DMA_ADDR_UNCHANGED;
  }
  return FakeMSPM0::dma_channels[channel].config.srcIncrement;
}

inline void DL_DMA_setDestIncrement(DMA_Regs*, uint8_t channel,
                                    DL_DMA_INCREMENT increment)
{
  FakeMSPM0::RecordDmaCall(FakeMSPM0::DmaCall::SET_DESTINATION_INCREMENT, channel);
  if (!FakeMSPM0::IsValidChannel(channel))
  {
    FakeMSPM0::SetDmaFault(FakeMSPM0::DmaFault::INVALID_CHANNEL);
    return;
  }
  FakeMSPM0::dma_channels[channel].config.destIncrement = increment;
}

inline DL_DMA_INCREMENT DL_DMA_getDestIncrement(const DMA_Regs*, uint8_t channel)
{
  FakeMSPM0::RecordDmaCall(FakeMSPM0::DmaCall::GET_DESTINATION_INCREMENT, channel);
  if (!FakeMSPM0::IsValidChannel(channel))
  {
    FakeMSPM0::SetDmaFault(FakeMSPM0::DmaFault::INVALID_CHANNEL);
    return DL_DMA_ADDR_UNCHANGED;
  }
  return FakeMSPM0::dma_channels[channel].config.destIncrement;
}

inline void DL_DMA_setSrcWidth(DMA_Regs*, uint8_t channel, DL_DMA_WIDTH width)
{
  FakeMSPM0::RecordDmaCall(FakeMSPM0::DmaCall::SET_SOURCE_WIDTH, channel);
  if (!FakeMSPM0::IsValidChannel(channel))
  {
    FakeMSPM0::SetDmaFault(FakeMSPM0::DmaFault::INVALID_CHANNEL);
    return;
  }
  FakeMSPM0::dma_channels[channel].config.srcWidth = width;
}

inline DL_DMA_WIDTH DL_DMA_getSrcWidth(const DMA_Regs*, uint8_t channel)
{
  FakeMSPM0::RecordDmaCall(FakeMSPM0::DmaCall::GET_SOURCE_WIDTH, channel);
  if (!FakeMSPM0::IsValidChannel(channel))
  {
    FakeMSPM0::SetDmaFault(FakeMSPM0::DmaFault::INVALID_CHANNEL);
    return DL_DMA_WIDTH_BYTE;
  }
  return FakeMSPM0::dma_channels[channel].config.srcWidth;
}

inline void DL_DMA_setDestWidth(DMA_Regs*, uint8_t channel, DL_DMA_WIDTH width)
{
  FakeMSPM0::RecordDmaCall(FakeMSPM0::DmaCall::SET_DESTINATION_WIDTH, channel);
  if (!FakeMSPM0::IsValidChannel(channel))
  {
    FakeMSPM0::SetDmaFault(FakeMSPM0::DmaFault::INVALID_CHANNEL);
    return;
  }
  FakeMSPM0::dma_channels[channel].config.destWidth = width;
}

inline DL_DMA_WIDTH DL_DMA_getDestWidth(const DMA_Regs*, uint8_t channel)
{
  FakeMSPM0::RecordDmaCall(FakeMSPM0::DmaCall::GET_DESTINATION_WIDTH, channel);
  if (!FakeMSPM0::IsValidChannel(channel))
  {
    FakeMSPM0::SetDmaFault(FakeMSPM0::DmaFault::INVALID_CHANNEL);
    return DL_DMA_WIDTH_BYTE;
  }
  return FakeMSPM0::dma_channels[channel].config.destWidth;
}

inline void DL_DMA_Full_Ch_setEarlyInterruptThreshold(
    DMA_Regs*, uint8_t channel, DL_DMA_EARLY_INTERRUPT_THRESHOLD threshold)
{
  FakeMSPM0::RecordDmaCall(FakeMSPM0::DmaCall::SET_EARLY_THRESHOLD, channel);
  if (!FakeMSPM0::IsValidChannel(channel))
  {
    FakeMSPM0::SetDmaFault(FakeMSPM0::DmaFault::INVALID_CHANNEL);
    return;
  }
  if (!FakeMSPM0::IsFullChannel(channel))
  {
    FakeMSPM0::dma_channels[channel].configuration_valid = false;
    FakeMSPM0::SetDmaFault(FakeMSPM0::DmaFault::BASIC_CHANNEL_FULL_FEATURE);
    return;
  }
  FakeMSPM0::dma_channels[channel].early_threshold = threshold;
  FakeMSPM0::ValidateConfiguration(channel);
}

inline DL_DMA_EARLY_INTERRUPT_THRESHOLD DL_DMA_Full_Ch_getEarlyInterruptThreshold(
    const DMA_Regs*, uint8_t channel)
{
  FakeMSPM0::RecordDmaCall(FakeMSPM0::DmaCall::GET_EARLY_THRESHOLD, channel);
  if (!FakeMSPM0::IsValidChannel(channel))
  {
    FakeMSPM0::SetDmaFault(FakeMSPM0::DmaFault::INVALID_CHANNEL);
    return DL_DMA_EARLY_INTERRUPT_THRESHOLD_DISABLED;
  }
  return FakeMSPM0::dma_channels[channel].early_threshold;
}

inline void DL_DMA_enableChannel(DMA_Regs*, uint8_t channel)
{
  FakeMSPM0::RecordDmaCall(FakeMSPM0::DmaCall::ENABLE_CHANNEL, channel);
  if (!FakeMSPM0::IsValidChannel(channel))
  {
    FakeMSPM0::SetDmaFault(FakeMSPM0::DmaFault::INVALID_CHANNEL);
    return;
  }

  auto& state = FakeMSPM0::dma_channels[channel];
  if (!FakeMSPM0::ValidateConfiguration(channel))
  {
    ++state.rejected_enable_calls;
    return;
  }
  if (state.history_size < state.source_history.size())
  {
    state.source_history[state.history_size] = state.source;
    state.destination_history[state.history_size] = state.destination;
    state.size_history[state.history_size] = state.transfer_size;
    ++state.history_size;
  }
  else
  {
    FakeMSPM0::SetDmaFault(FakeMSPM0::DmaFault::HISTORY_OVERFLOW);
  }
  state.enabled = true;
  ++state.enable_calls;
  if (FakeMSPM0::dma_enable_hook != nullptr)
  {
    FakeMSPM0::dma_enable_hook(channel);
  }
}

inline void DL_DMA_disableChannel(DMA_Regs*, uint8_t channel)
{
  FakeMSPM0::RecordDmaCall(FakeMSPM0::DmaCall::DISABLE_CHANNEL, channel);
  if (!FakeMSPM0::IsValidChannel(channel))
  {
    FakeMSPM0::SetDmaFault(FakeMSPM0::DmaFault::INVALID_CHANNEL);
    return;
  }
  auto& state = FakeMSPM0::dma_channels[channel];
  state.enabled = false;
  ++state.disable_calls;
}

inline bool DL_DMA_isChannelEnabled(const DMA_Regs*, uint8_t channel)
{
  FakeMSPM0::RecordDmaCall(FakeMSPM0::DmaCall::IS_CHANNEL_ENABLED, channel);
  if (!FakeMSPM0::IsValidChannel(channel))
  {
    FakeMSPM0::SetDmaFault(FakeMSPM0::DmaFault::INVALID_CHANNEL);
    return false;
  }
  return FakeMSPM0::dma_channels[channel].enabled;
}

inline void DL_DMA_startTransfer(DMA_Regs*, uint8_t channel)
{
  FakeMSPM0::RecordDmaCall(FakeMSPM0::DmaCall::START_TRANSFER, channel);
  if (!FakeMSPM0::IsValidChannel(channel))
  {
    FakeMSPM0::SetDmaFault(FakeMSPM0::DmaFault::INVALID_CHANNEL);
  }
}

inline void DL_DMA_enableInterrupt(DMA_Regs* dma, uint32_t interrupt_mask)
{
  FakeMSPM0::RecordDmaCall(FakeMSPM0::DmaCall::ENABLE_INTERRUPT,
                           FakeMSPM0::kDmaControllerCall);
  dma->CPU_INT.IMASK |= interrupt_mask;
  FakeMSPM0::RefreshMis(dma);
  FakeMSPM0::NotifyDmaIrqIfPending(dma);
}

inline void DL_DMA_disableInterrupt(DMA_Regs* dma, uint32_t interrupt_mask)
{
  FakeMSPM0::RecordDmaCall(FakeMSPM0::DmaCall::DISABLE_INTERRUPT,
                           FakeMSPM0::kDmaControllerCall);
  dma->CPU_INT.IMASK &= ~interrupt_mask;
  FakeMSPM0::RefreshMis(dma);
}

inline uint32_t DL_DMA_getEnabledInterrupts(const DMA_Regs* dma, uint32_t interrupt_mask)
{
  FakeMSPM0::RecordDmaCall(FakeMSPM0::DmaCall::GET_ENABLED_INTERRUPTS,
                           FakeMSPM0::kDmaControllerCall);
  return dma->CPU_INT.IMASK & interrupt_mask;
}

inline uint32_t DL_DMA_getEnabledInterruptStatus(const DMA_Regs* dma,
                                                 uint32_t interrupt_mask)
{
  FakeMSPM0::RecordDmaCall(FakeMSPM0::DmaCall::GET_ENABLED_INTERRUPT_STATUS,
                           FakeMSPM0::kDmaControllerCall);
  return dma->CPU_INT.MIS & interrupt_mask;
}

inline uint32_t DL_DMA_getRawInterruptStatus(const DMA_Regs* dma, uint32_t interrupt_mask)
{
  FakeMSPM0::RecordDmaCall(FakeMSPM0::DmaCall::GET_RAW_INTERRUPT_STATUS,
                           FakeMSPM0::kDmaControllerCall);
  return dma->CPU_INT.RIS & interrupt_mask;
}

inline void DL_DMA_clearInterruptStatus(DMA_Regs* dma, uint32_t interrupt_mask)
{
  FakeMSPM0::RecordDmaCall(FakeMSPM0::DmaCall::CLEAR_INTERRUPT_STATUS,
                           FakeMSPM0::kDmaControllerCall);
  dma->CPU_INT.ICLR = interrupt_mask;
  dma->CPU_INT.RIS &= ~interrupt_mask;
  FakeMSPM0::RefreshMis(dma);
  FakeMSPM0::dma_cleared_interrupts |= interrupt_mask;

  if (FakeMSPM0::dma_clear_history_size < FakeMSPM0::dma_clear_history.size())
  {
    FakeMSPM0::dma_clear_history[FakeMSPM0::dma_clear_history_size++] = interrupt_mask;
  }
  else
  {
    FakeMSPM0::SetDmaFault(FakeMSPM0::DmaFault::HISTORY_OVERFLOW);
  }

  for (uint8_t channel = 0U; channel < FakeMSPM0::dma_channels.size(); ++channel)
  {
    auto& state = FakeMSPM0::dma_channels[channel];
    if ((interrupt_mask & FakeMSPM0::ChannelCause(channel)) != 0U)
    {
      ++state.clear_calls;
      ++state.full_clear_calls;
    }
    if ((interrupt_mask & FakeMSPM0::EarlyCause(channel)) != 0U)
    {
      ++state.clear_calls;
      ++state.early_clear_calls;
    }
  }

  FakeMSPM0::NotifyDmaIrqIfPending(dma);
  if (FakeMSPM0::dma_clear_hook != nullptr)
  {
    FakeMSPM0::dma_clear_hook(interrupt_mask);
  }
}
