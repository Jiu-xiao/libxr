#include "mspm0_dma_shared.hpp"

#include <array>

namespace LibXR::MSPM0DmaShared
{
namespace
{
std::array<ChannelRegistration, MAX_DMA_CHANNELS> channel_callbacks{};
}

void EnableDmaIRQ() { NVIC_EnableIRQ(DMA_INT_IRQn); }

int ChannelFromIIDX(DL_DMA_EVENT_IIDX iidx)
{
  switch (iidx)
  {
    case DL_DMA_EVENT_IIDX_DMACH0:
      return 0;
    case DL_DMA_EVENT_IIDX_DMACH1:
      return 1;
    case DL_DMA_EVENT_IIDX_DMACH2:
      return 2;
    case DL_DMA_EVENT_IIDX_DMACH3:
      return 3;
    case DL_DMA_EVENT_IIDX_DMACH4:
      return 4;
    case DL_DMA_EVENT_IIDX_DMACH5:
      return 5;
    case DL_DMA_EVENT_IIDX_DMACH6:
      return 6;
    case DL_DMA_EVENT_IIDX_DMACH7:
      return 7;
    case DL_DMA_EVENT_IIDX_DMACH8:
      return 8;
    case DL_DMA_EVENT_IIDX_DMACH9:
      return 9;
    case DL_DMA_EVENT_IIDX_DMACH10:
      return 10;
    case DL_DMA_EVENT_IIDX_DMACH11:
      return 11;
    case DL_DMA_EVENT_IIDX_DMACH12:
      return 12;
    case DL_DMA_EVENT_IIDX_DMACH13:
      return 13;
    case DL_DMA_EVENT_IIDX_DMACH14:
      return 14;
    case DL_DMA_EVENT_IIDX_DMACH15:
      return 15;
    default:
      return -1;
  }
}

std::uint32_t ChannelInterruptMask(std::uint8_t channel)
{
  return (channel < MAX_DMA_CHANNELS) ? (1UL << channel) : 0U;
}

bool RegisterChannel(std::uint8_t channel, ChannelCallback callback, void* context)
{
  ASSERT(channel < MAX_DMA_CHANNELS);
  if (channel >= MAX_DMA_CHANNELS)
  {
    return false;
  }

  const auto existing = channel_callbacks[channel];
  ASSERT(existing.callback == nullptr || existing.context == context);
  if (existing.callback != nullptr && existing.context != context)
  {
    return false;
  }

  channel_callbacks[channel] = {callback, context};
  if (callback != nullptr)
  {
    DL_DMA_clearInterruptStatus(DMA, ChannelInterruptMask(channel));
    DL_DMA_enableInterrupt(DMA, ChannelInterruptMask(channel));
    EnableDmaIRQ();
  }

  return true;
}

bool DispatchPending(DMA_Regs* dma)
{
  bool handled = false;

  while (true)
  {
    const auto iidx = DL_DMA_getPendingInterrupt(dma);
    if (iidx == DL_DMA_EVENT_IIDX_NO_INTR)
    {
      return handled;
    }

    const int channel = ChannelFromIIDX(iidx);
    if (channel < 0 || static_cast<std::size_t>(channel) >= MAX_DMA_CHANNELS)
    {
      return handled;
    }

    const auto channel_index = static_cast<std::size_t>(channel);
    const std::uint32_t mask = ChannelInterruptMask(static_cast<std::uint8_t>(channel));
    const auto registration = channel_callbacks[channel_index];
    if (registration.callback != nullptr)
    {
      registration.callback(registration.context);
      handled = true;
    }

    if (mask != 0U)
    {
      DL_DMA_clearInterruptStatus(dma, mask);
    }
  }
}
}  // namespace LibXR::MSPM0DmaShared

// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void DMA_IRQHandler(void)
{
  (void)LibXR::MSPM0DmaShared::DispatchPending(DMA);
}
