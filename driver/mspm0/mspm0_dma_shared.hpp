#pragma once

#include <ti/driverlib/dl_dma.h>

#include <cstddef>
#include <cstdint>

#include "libxr.hpp"
#include "ti_msp_dl_config.h"

namespace LibXR::MSPM0DmaShared
{
using ChannelCallback = void (*)(void*);

struct ChannelRegistration
{
  ChannelCallback callback;
  void* context;
};

inline constexpr std::size_t MAX_DMA_CHANNELS = 16U;

// NOLINTNEXTLINE(readability-identifier-naming)
void EnableDmaIRQ();

int ChannelFromIIDX(DL_DMA_EVENT_IIDX iidx);
std::uint32_t ChannelInterruptMask(std::uint8_t channel);
bool DispatchPending(DMA_Regs* dma);
bool RegisterChannel(std::uint8_t channel, ChannelCallback callback, void* context);
}  // namespace LibXR::MSPM0DmaShared
