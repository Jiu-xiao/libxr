#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>

#include "mspm0_uart.hpp"

namespace MSPM0UartTest
{

#if !defined(LIBXR_MSPM0_DMA_EXTERNAL_IRQ_HANDLER)
extern "C" void DMA_IRQHandler(void);
#endif

#define MSPM0_CHECK(condition)                                                     \
  do                                                                               \
  {                                                                                \
    if (!(condition))                                                              \
    {                                                                              \
      std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: " << #condition \
                << '\n';                                                           \
      return false;                                                                \
    }                                                                              \
  } while (false)

inline uint32_t LowAddress(const void* address)
{
  return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(address));
}

inline void ResetHarness()
{
  FakeMSPM0::ResetUarts();
  FakeMSPM0::ResetDma();
  LibXR::MSPM0DmaDispatcher::ResetDiagnostics();
}

inline void ServiceUart(UART_Regs* uart, uint8_t index)
{
  FakeMSPM0::InvokeUartIrq(uart, [index]() { LibXR::MSPM0UART::OnInterrupt(index); });
}

inline void ServiceDma()
{
  FakeMSPM0::IrqScope scope(DMA_INT_IRQn);
#if defined(LIBXR_MSPM0_DMA_EXTERNAL_IRQ_HANDLER)
  LibXR::MSPM0DmaDispatcher::Dispatch();
#else
  DMA_IRQHandler();
#endif
}

inline void CompleteTx(UART_Regs* uart, uint8_t index, uint8_t channel,
                       bool line_idle = false)
{
  FakeMSPM0::dma_channels[channel].enabled = false;
  FakeMSPM0::dma_channels[channel].transfer_size = 0U;
  uint32_t events = DL_UART_INTERRUPT_DMA_DONE_TX;
  if (line_idle)
  {
    events |= DL_UART_INTERRUPT_EOT_DONE;
  }
  FakeMSPM0::RaiseCpuInterrupt(uart, events);
  ServiceUart(uart, index);
}

template <size_t StorageSize, size_t PayloadSize>
bool ExpectTxPayload(const LibXR::MSPM0UARTTxBuffer<StorageSize>& storage,
                     uint8_t channel, size_t history_index,
                     const std::array<uint8_t, PayloadSize>& expected)
{
  const auto& dma = FakeMSPM0::dma_channels[channel];
  MSPM0_CHECK(history_index < dma.history_size);
  MSPM0_CHECK(dma.size_history[history_index] == expected.size());

  const size_t half_size = storage.Size() / 2U;
  const uint32_t source = dma.source_history[history_index];
  size_t offset = storage.Size();
  if (source == LowAddress(storage.Data()))
  {
    offset = 0U;
  }
  else if (source == LowAddress(storage.Data() + half_size))
  {
    offset = half_size;
  }
  MSPM0_CHECK(offset < storage.Size());
  MSPM0_CHECK(std::memcmp(storage.Data() + offset, expected.data(), expected.size()) ==
              0);
  return true;
}

template <size_t Size>
bool ExpectQueuedBytes(LibXR::MSPM0UART& uart, const std::array<uint8_t, Size>& expected)
{
  std::array<uint8_t, Size> actual{};
  MSPM0_CHECK(uart._read_port.queue_data_->PopBatch(actual.data(), actual.size()) ==
              LibXR::ErrorCode::OK);
  MSPM0_CHECK(actual == expected);
  return true;
}

inline bool ExpectQueuedBytes(LibXR::MSPM0UART& uart, std::span<const uint8_t> expected)
{
  for (const uint8_t value : expected)
  {
    uint8_t actual = 0U;
    MSPM0_CHECK(uart._read_port.queue_data_->Pop(actual) == LibXR::ErrorCode::OK);
    MSPM0_CHECK(actual == value);
  }
  return true;
}

}  // namespace MSPM0UartTest
