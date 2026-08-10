#pragma once

#include <cstdint>

#include "../../fake_mspm0_runtime.hpp"

using DL_UART_WORD_LENGTH = uint32_t;
using DL_UART_PARITY = uint32_t;
using DL_UART_STOP_BITS = uint32_t;
using DL_UART_RX_FIFO_LEVEL = uint32_t;
using DL_UART_TX_FIFO_LEVEL = uint32_t;
using DL_UART_COMMUNICATION_MODE = uint32_t;

enum DL_UART_DIRECTION : uint32_t
{
  DL_UART_DIRECTION_NONE = 0U,
  DL_UART_DIRECTION_TX = 1U,
  DL_UART_DIRECTION_RX = 2U,
  DL_UART_DIRECTION_TX_RX = 3U,
};

inline constexpr DL_UART_WORD_LENGTH DL_UART_WORD_LENGTH_5_BITS = 5U;
inline constexpr DL_UART_WORD_LENGTH DL_UART_WORD_LENGTH_6_BITS = 6U;
inline constexpr DL_UART_WORD_LENGTH DL_UART_WORD_LENGTH_7_BITS = 7U;
inline constexpr DL_UART_WORD_LENGTH DL_UART_WORD_LENGTH_8_BITS = 8U;
inline constexpr DL_UART_PARITY DL_UART_PARITY_NONE = 0U;
inline constexpr DL_UART_PARITY DL_UART_PARITY_EVEN = 1U;
inline constexpr DL_UART_PARITY DL_UART_PARITY_ODD = 2U;
inline constexpr DL_UART_STOP_BITS DL_UART_STOP_BITS_ONE = 1U;
inline constexpr DL_UART_STOP_BITS DL_UART_STOP_BITS_TWO = 2U;
inline constexpr DL_UART_RX_FIFO_LEVEL DL_UART_RX_FIFO_LEVEL_ONE_ENTRY = 1U;
inline constexpr DL_UART_RX_FIFO_LEVEL DL_UART_RX_FIFO_LEVEL_TWO_ENTRIES = 2U;
inline constexpr DL_UART_TX_FIFO_LEVEL DL_UART_TX_FIFO_LEVEL_ONE_ENTRY = 1U;
inline constexpr DL_UART_COMMUNICATION_MODE DL_UART_MODE_NORMAL = 0U;
inline constexpr DL_UART_COMMUNICATION_MODE DL_UART_MODE_IDLE_LINE = 1U;

inline constexpr uint32_t DL_UART_INTERRUPT_RX = 1UL << 0U;
inline constexpr uint32_t DL_UART_INTERRUPT_ADDRESS_MATCH = 1UL << 1U;
inline constexpr uint32_t DL_UART_INTERRUPT_OVERRUN_ERROR = 1UL << 2U;
inline constexpr uint32_t DL_UART_INTERRUPT_BREAK_ERROR = 1UL << 3U;
inline constexpr uint32_t DL_UART_INTERRUPT_PARITY_ERROR = 1UL << 4U;
inline constexpr uint32_t DL_UART_INTERRUPT_FRAMING_ERROR = 1UL << 5U;
inline constexpr uint32_t DL_UART_INTERRUPT_NOISE_ERROR = 1UL << 6U;
inline constexpr uint32_t DL_UART_INTERRUPT_TX = 1UL << 7U;
inline constexpr uint32_t DL_UART_INTERRUPT_DMA_DONE_TX = 1UL << 8U;
inline constexpr uint32_t DL_UART_INTERRUPT_EOT_DONE = 1UL << 9U;
inline constexpr uint32_t DL_UART_INTERRUPT_LINC0_MATCH = 1UL << 10U;
inline constexpr uint32_t DL_UART_INTERRUPT_DMA_DONE_RX = 1UL << 11U;
inline constexpr uint32_t DL_UART_INTERRUPT_RXD_NEG_EDGE = 1UL << 12U;
inline constexpr uint32_t DL_UART_INTERRUPT_RX_TIMEOUT_ERROR = 1UL << 13U;

inline constexpr uint32_t DL_UART_DMA_INTERRUPT_RX = 1UL << 0U;
inline constexpr uint32_t DL_UART_DMA_INTERRUPT_RX_TIMEOUT = 1UL << 1U;
inline constexpr uint32_t DL_UART_DMA_INTERRUPT_TX = 1UL << 0U;

inline constexpr uint32_t UART_RXDATA_DATA_MASK = 0x000000FFU;
inline constexpr uint32_t UART_RXDATA_FRMERR_MASK = 0x00000100U;
inline constexpr uint32_t UART_RXDATA_PARERR_MASK = 0x00000200U;
inline constexpr uint32_t UART_RXDATA_BRKERR_MASK = 0x00000400U;
inline constexpr uint32_t UART_RXDATA_OVRERR_MASK = 0x00000800U;
inline constexpr uint32_t UART_RXDATA_NERR_MASK = 0x00001000U;
inline constexpr uint32_t UART_RXDATA_FRMERR_SET = UART_RXDATA_FRMERR_MASK;
inline constexpr uint32_t UART_RXDATA_PARERR_SET = UART_RXDATA_PARERR_MASK;
inline constexpr uint32_t UART_RXDATA_BRKERR_SET = UART_RXDATA_BRKERR_MASK;
inline constexpr uint32_t UART_RXDATA_OVRERR_SET = UART_RXDATA_OVRERR_MASK;
inline constexpr uint32_t UART_RXDATA_NERR_SET = UART_RXDATA_NERR_MASK;
inline constexpr uint32_t DL_UART_ERROR_OVERRUN = UART_RXDATA_OVRERR_SET;
inline constexpr uint32_t DL_UART_ERROR_BREAK = UART_RXDATA_BRKERR_SET;
inline constexpr uint32_t DL_UART_ERROR_PARITY = UART_RXDATA_PARERR_SET;
inline constexpr uint32_t DL_UART_ERROR_FRAMING = UART_RXDATA_FRMERR_SET;
inline constexpr uint32_t DL_UART_ERROR_NOISE = UART_RXDATA_NERR_SET;

#if defined(FAKE_MSPM0_G3519)
#define UART0_BASE 0x40108000U
#define UART1_BASE 0x40100000U
#define UART3_BASE 0x40500000U
#define UART4_BASE 0x40502000U
#define UART5_BASE 0x40504000U
#define UART6_BASE 0x40506000U
#define UART7_BASE 0x4010A000U
inline constexpr IRQn_Type UART0_INT_IRQn = 15;
inline constexpr IRQn_Type UART1_INT_IRQn = 13;
inline constexpr IRQn_Type UART3_INT_IRQn = 3;
inline constexpr IRQn_Type UART4_INT_IRQn = 14;
inline constexpr IRQn_Type UART5_INT_IRQn = 23;
inline constexpr IRQn_Type UART6_INT_IRQn = 29;
inline constexpr IRQn_Type UART7_INT_IRQn = 27;
inline constexpr uint32_t DMA_UART3_TX_TRIG = 19U;
inline constexpr uint32_t DMA_UART3_RX_TRIG = 18U;
inline constexpr uint32_t DMA_UART4_TX_TRIG = 21U;
inline constexpr uint32_t DMA_UART4_RX_TRIG = 20U;
inline constexpr uint32_t DMA_UART5_TX_TRIG = 23U;
inline constexpr uint32_t DMA_UART5_RX_TRIG = 22U;
inline constexpr uint32_t DMA_UART6_TX_TRIG = 25U;
inline constexpr uint32_t DMA_UART6_RX_TRIG = 24U;
inline constexpr uint32_t DMA_UART0_TX_TRIG = 27U;
inline constexpr uint32_t DMA_UART0_RX_TRIG = 26U;
inline constexpr uint32_t DMA_UART7_TX_TRIG = 29U;
inline constexpr uint32_t DMA_UART7_RX_TRIG = 28U;
inline constexpr uint32_t DMA_UART1_TX_TRIG = 31U;
inline constexpr uint32_t DMA_UART1_RX_TRIG = 30U;
#else
#define UART0_BASE 0x40108000U
#define UART1_BASE 0x40100000U
#define UART2_BASE 0x40102000U
#define UART3_BASE 0x40500000U
inline constexpr IRQn_Type UART0_INT_IRQn = 15;
inline constexpr IRQn_Type UART1_INT_IRQn = 13;
inline constexpr IRQn_Type UART2_INT_IRQn = 14;
inline constexpr IRQn_Type UART3_INT_IRQn = 3;
inline constexpr uint32_t DMA_UART3_TX_TRIG = 16U;
inline constexpr uint32_t DMA_UART3_RX_TRIG = 15U;
inline constexpr uint32_t DMA_UART0_TX_TRIG = 18U;
inline constexpr uint32_t DMA_UART0_RX_TRIG = 17U;
inline constexpr uint32_t DMA_UART1_TX_TRIG = 20U;
inline constexpr uint32_t DMA_UART1_RX_TRIG = 19U;
inline constexpr uint32_t DMA_UART2_TX_TRIG = 22U;
inline constexpr uint32_t DMA_UART2_RX_TRIG = 21U;
#endif

inline UART_Regs* const UART0 = &FakeMSPM0::uart_regs[0U];
inline UART_Regs* const UART1 = &FakeMSPM0::uart_regs[1U];
#if defined(UART2_BASE)
inline UART_Regs* const UART2 = &FakeMSPM0::uart_regs[2U];
#endif
inline UART_Regs* const UART3 = &FakeMSPM0::uart_regs[3U];
#if defined(UART4_BASE)
inline UART_Regs* const UART4 = &FakeMSPM0::uart_regs[4U];
inline UART_Regs* const UART5 = &FakeMSPM0::uart_regs[5U];
inline UART_Regs* const UART6 = &FakeMSPM0::uart_regs[6U];
inline UART_Regs* const UART7 = &FakeMSPM0::uart_regs[7U];
#endif

namespace FakeMSPM0
{

inline void (*uart_clear_interrupt_hook)(UART_Regs*, uint32_t) = nullptr;
inline void (*uart_clear_dma_rx_hook)(UART_Regs*, uint32_t) = nullptr;
inline void (*uart_enable_dma_rx_hook)(UART_Regs*, uint32_t) = nullptr;
inline void (*uart_enable_hook)(UART_Regs*) = nullptr;
inline void (*uart_is_tx_fifo_empty_hook)(UART_Regs*) = nullptr;

inline void ResetUarts()
{
  ResetRuntime();
  BindIrq(UART0, UART0_INT_IRQn);
  BindIrq(UART1, UART1_INT_IRQn);
#if defined(UART2_BASE)
  BindIrq(UART2, UART2_INT_IRQn);
#endif
  BindIrq(UART3, UART3_INT_IRQn);
#if defined(UART4_BASE)
  BindIrq(UART4, UART4_INT_IRQn);
  BindIrq(UART5, UART5_INT_IRQn);
  BindIrq(UART6, UART6_INT_IRQn);
  BindIrq(UART7, UART7_INT_IRQn);
#endif
  uart_clear_interrupt_hook = nullptr;
  uart_clear_dma_rx_hook = nullptr;
  uart_enable_dma_rx_hook = nullptr;
  uart_enable_hook = nullptr;
  uart_is_tx_fifo_empty_hook = nullptr;
}

inline uint32_t ErrorWordToInterrupt(uint32_t word)
{
  uint32_t mask = 0U;
  if ((word & UART_RXDATA_OVRERR_MASK) != 0U)
  {
    mask |= DL_UART_INTERRUPT_OVERRUN_ERROR;
  }
  if ((word & UART_RXDATA_BRKERR_MASK) != 0U)
  {
    mask |= DL_UART_INTERRUPT_BREAK_ERROR;
  }
  if ((word & UART_RXDATA_PARERR_MASK) != 0U)
  {
    mask |= DL_UART_INTERRUPT_PARITY_ERROR;
  }
  if ((word & UART_RXDATA_FRMERR_MASK) != 0U)
  {
    mask |= DL_UART_INTERRUPT_FRAMING_ERROR;
  }
  if ((word & UART_RXDATA_NERR_MASK) != 0U)
  {
    mask |= DL_UART_INTERRUPT_NOISE_ERROR;
  }
  return mask;
}

inline void RaiseCpuInterrupt(UART_Regs* uart, uint32_t mask)
{
  uart->CPU_INT.RIS |= mask;
  SetPendingForPublisher(uart);
}

inline void RaiseInterrupt(UART_Regs* uart, uint32_t mask)
{
  RaiseCpuInterrupt(uart, mask);
}

inline void RaiseDmaReceiveEvent(UART_Regs* uart, uint32_t mask)
{
  uart->DMA_TRIG_RX.RIS |= mask;
  SyncPublisher(uart->DMA_TRIG_RX);
}

inline void RaiseDmaTransmitEvent(UART_Regs* uart)
{
  uart->DMA_TRIG_TX.RIS |= DL_UART_DMA_INTERRUPT_TX;
  SyncPublisher(uart->DMA_TRIG_TX);
}

inline bool InjectRxWord(UART_Regs* uart, uint32_t word)
{
  if (uart->rx_size == uart->rx_fifo.size())
  {
    RaiseCpuInterrupt(uart, DL_UART_INTERRUPT_OVERRUN_ERROR);
    return false;
  }

  const size_t tail = (uart->rx_head + uart->rx_size) % uart->rx_fifo.size();
  uart->rx_fifo[tail] = word;
  ++uart->rx_size;
  const size_t threshold = uart->rx_fifo_threshold == 0U ? 1U : uart->rx_fifo_threshold;
  if (uart->rx_size >= threshold)
  {
    RaiseCpuInterrupt(uart, DL_UART_INTERRUPT_RX | ErrorWordToInterrupt(word));
    RaiseDmaReceiveEvent(uart, DL_UART_DMA_INTERRUPT_RX);
  }
  return true;
}

inline bool InjectRx(UART_Regs* uart, uint8_t value, uint32_t error = 0U)
{
  return InjectRxWord(uart, static_cast<uint32_t>(value) | error);
}

}  // namespace FakeMSPM0

inline DL_UART_WORD_LENGTH DL_UART_getWordLength(const UART_Regs* uart)
{
  FakeMSPM0::TraceUartMmio(const_cast<UART_Regs*>(uart), "getWordLength");
  return uart->word_length;
}

inline DL_UART_PARITY DL_UART_getParityMode(const UART_Regs* uart)
{
  FakeMSPM0::TraceUartMmio(const_cast<UART_Regs*>(uart), "getParityMode");
  return uart->parity;
}

inline DL_UART_STOP_BITS DL_UART_getStopBits(const UART_Regs* uart)
{
  FakeMSPM0::TraceUartMmio(const_cast<UART_Regs*>(uart), "getStopBits");
  return uart->stop_bits;
}

inline DL_UART_DIRECTION DL_UART_getDirection(const UART_Regs* uart)
{
  FakeMSPM0::TraceUartMmio(const_cast<UART_Regs*>(uart), "getDirection");
  return static_cast<DL_UART_DIRECTION>(uart->direction);
}

inline void DL_UART_changeConfig(UART_Regs* uart)
{
  FakeMSPM0::TraceUartMmio(uart, "changeConfig");
}

inline void DL_UART_setWordLength(UART_Regs* uart, DL_UART_WORD_LENGTH value)
{
  FakeMSPM0::TraceUartMmio(uart, "setWordLength");
  uart->word_length = value;
}

inline void DL_UART_setParityMode(UART_Regs* uart, DL_UART_PARITY value)
{
  FakeMSPM0::TraceUartMmio(uart, "setParityMode");
  uart->parity = value;
}

inline void DL_UART_setStopBits(UART_Regs* uart, DL_UART_STOP_BITS value)
{
  FakeMSPM0::TraceUartMmio(uart, "setStopBits");
  uart->stop_bits = value;
}

inline void DL_UART_setDirection(UART_Regs* uart, DL_UART_DIRECTION value)
{
  FakeMSPM0::TraceUartMmio(uart, "setDirection");
  uart->direction = value;
}

inline void DL_UART_enableFIFOs(UART_Regs* uart)
{
  FakeMSPM0::TraceUartMmio(uart, "enableFIFOs");
  uart->fifos_enabled = true;
}

inline void DL_UART_disableFIFOs(UART_Regs* uart)
{
  FakeMSPM0::TraceUartMmio(uart, "disableFIFOs");
  uart->fifos_enabled = false;
}

inline bool DL_UART_isFIFOsEnabled(const UART_Regs* uart)
{
  FakeMSPM0::TraceUartMmio(const_cast<UART_Regs*>(uart), "isFIFOsEnabled");
  return uart->fifos_enabled;
}

inline void DL_UART_setTXFIFOThreshold(UART_Regs* uart, DL_UART_TX_FIFO_LEVEL value)
{
  FakeMSPM0::TraceUartMmio(uart, "setTXFIFOThreshold");
  uart->tx_fifo_threshold = value;
}

inline DL_UART_TX_FIFO_LEVEL DL_UART_getTXFIFOThreshold(const UART_Regs* uart)
{
  FakeMSPM0::TraceUartMmio(const_cast<UART_Regs*>(uart), "getTXFIFOThreshold");
  return uart->tx_fifo_threshold;
}

inline void DL_UART_setRXFIFOThreshold(UART_Regs* uart, DL_UART_RX_FIFO_LEVEL value)
{
  FakeMSPM0::TraceUartMmio(uart, "setRXFIFOThreshold");
  uart->rx_fifo_threshold = value;
}

inline DL_UART_RX_FIFO_LEVEL DL_UART_getRXFIFOThreshold(const UART_Regs* uart)
{
  FakeMSPM0::TraceUartMmio(const_cast<UART_Regs*>(uart), "getRXFIFOThreshold");
  return uart->rx_fifo_threshold;
}

inline void DL_UART_setRXInterruptTimeout(UART_Regs* uart, uint32_t value)
{
  FakeMSPM0::TraceUartMmio(uart, "setRXInterruptTimeout");
  uart->rx_interrupt_timeout = value;
}

inline uint32_t DL_UART_getRXInterruptTimeout(const UART_Regs* uart)
{
  FakeMSPM0::TraceUartMmio(const_cast<UART_Regs*>(uart), "getRXInterruptTimeout");
  return uart->rx_interrupt_timeout;
}

inline void DL_UART_configBaudRate(UART_Regs* uart, uint32_t, uint32_t baudrate)
{
  FakeMSPM0::TraceUartMmio(uart, "configBaudRate");
  uart->baudrate = baudrate;
  ++uart->baudrate_changes;
}

inline void DL_UART_enable(UART_Regs* uart)
{
  FakeMSPM0::TraceUartMmio(uart, "enable");
  uart->enabled = true;
  if (FakeMSPM0::uart_enable_hook != nullptr)
  {
    FakeMSPM0::uart_enable_hook(uart);
  }
}

inline void DL_UART_disable(UART_Regs* uart)
{
  FakeMSPM0::TraceUartMmio(uart, "disable");
  uart->enabled = false;
}

inline bool DL_UART_isEnabled(const UART_Regs* uart)
{
  FakeMSPM0::TraceUartMmio(const_cast<UART_Regs*>(uart), "isEnabled");
  return uart->enabled;
}

inline bool DL_UART_isBusy(const UART_Regs* uart)
{
  FakeMSPM0::TraceUartMmio(const_cast<UART_Regs*>(uart), "isBusy");
  return uart->busy;
}

inline bool DL_UART_isRXFIFOEmpty(const UART_Regs* uart)
{
  FakeMSPM0::TraceUartMmio(const_cast<UART_Regs*>(uart), "isRXFIFOEmpty");
  return uart->rx_size == 0U;
}

inline bool DL_UART_isTXFIFOFull(const UART_Regs* uart)
{
  FakeMSPM0::TraceUartMmio(const_cast<UART_Regs*>(uart), "isTXFIFOFull");
  return uart->tx_fifo_full;
}

inline bool DL_UART_isTXFIFOEmpty(const UART_Regs* uart)
{
  auto* mutable_uart = const_cast<UART_Regs*>(uart);
  FakeMSPM0::TraceUartMmio(mutable_uart, "isTXFIFOEmpty");
  if (FakeMSPM0::uart_is_tx_fifo_empty_hook != nullptr)
  {
    FakeMSPM0::uart_is_tx_fifo_empty_hook(mutable_uart);
  }
  return uart->tx_fifo_empty;
}

inline uint8_t DL_UART_receiveData(const UART_Regs* uart)
{
  return static_cast<uint8_t>(static_cast<uint32_t>(uart->RXDATA) &
                              UART_RXDATA_DATA_MASK);
}

inline uint32_t DL_UART_getErrorStatus(const UART_Regs* uart, uint32_t error_mask)
{
  return static_cast<uint32_t>(uart->RXDATA) & error_mask;
}

inline void DL_UART_transmitData(UART_Regs* uart, uint8_t data)
{
  FakeMSPM0::TraceUartMmio(uart, "TXDATA.write");
  uart->TXDATA = data;
  if (uart->tx_history_size < uart->tx_history.size())
  {
    uart->tx_history[uart->tx_history_size++] = data;
  }
}

inline uint32_t DL_UART_drainRXFIFO(UART_Regs* uart, uint8_t* buffer, uint32_t max_count)
{
  uint32_t count = 0U;
  while (count < max_count && !DL_UART_isRXFIFOEmpty(uart))
  {
    buffer[count++] = DL_UART_receiveData(uart);
  }
  return count;
}

inline void DL_UART_enableInterrupt(UART_Regs* uart, uint32_t mask)
{
  FakeMSPM0::TraceUartMmio(uart, "enableInterrupt");
  uart->CPU_INT.IMASK |= mask;
  FakeMSPM0::SetPendingForPublisher(uart);
}

inline void DL_UART_disableInterrupt(UART_Regs* uart, uint32_t mask)
{
  FakeMSPM0::TraceUartMmio(uart, "disableInterrupt");
  uart->CPU_INT.IMASK &= ~mask;
  FakeMSPM0::SyncPublisher(uart->CPU_INT);
}

inline uint32_t DL_UART_getEnabledInterrupts(const UART_Regs* uart, uint32_t mask)
{
  FakeMSPM0::TraceUartMmio(const_cast<UART_Regs*>(uart), "getEnabledInterrupts");
  return uart->CPU_INT.IMASK & mask;
}

inline uint32_t DL_UART_getEnabledInterruptStatus(const UART_Regs* uart, uint32_t mask)
{
  FakeMSPM0::TraceUartMmio(const_cast<UART_Regs*>(uart), "getEnabledInterruptStatus");
  return uart->CPU_INT.MIS & mask;
}

inline uint32_t DL_UART_getRawInterruptStatus(const UART_Regs* uart, uint32_t mask)
{
  FakeMSPM0::TraceUartMmio(const_cast<UART_Regs*>(uart), "getRawInterruptStatus");
  return uart->CPU_INT.RIS & mask;
}

inline void DL_UART_clearInterruptStatus(UART_Regs* uart, uint32_t mask)
{
  FakeMSPM0::TraceUartMmio(uart, "clearInterruptStatus");
  uart->CPU_INT.ICLR = mask;
  uart->CPU_INT.RIS &= ~mask;
  if (FakeMSPM0::uart_clear_interrupt_hook != nullptr)
  {
    FakeMSPM0::uart_clear_interrupt_hook(uart, mask);
  }

  // On the legacy UART block, clearing an RX error interrupt also clears the
  // corresponding status bit on the FIFO head word. Tests rely on this ordering
  // so a backend must inspect RXDATA before acknowledging its associated errors.
  if (uart->rx_size != 0U)
  {
    uint32_t& word = uart->rx_fifo[uart->rx_head];
    if ((mask & DL_UART_INTERRUPT_OVERRUN_ERROR) != 0U)
    {
      word &= ~UART_RXDATA_OVRERR_MASK;
    }
    if ((mask & DL_UART_INTERRUPT_BREAK_ERROR) != 0U)
    {
      word &= ~UART_RXDATA_BRKERR_MASK;
    }
    if ((mask & DL_UART_INTERRUPT_PARITY_ERROR) != 0U)
    {
      word &= ~UART_RXDATA_PARERR_MASK;
    }
    if ((mask & DL_UART_INTERRUPT_FRAMING_ERROR) != 0U)
    {
      word &= ~UART_RXDATA_FRMERR_MASK;
    }
    if ((mask & DL_UART_INTERRUPT_NOISE_ERROR) != 0U)
    {
      word &= ~UART_RXDATA_NERR_MASK;
    }
  }
  FakeMSPM0::SyncPublisher(uart->CPU_INT);
}

inline void DL_UART_enableDMAReceiveEvent(UART_Regs* uart, uint32_t interrupt)
{
  FakeMSPM0::TraceUartMmio(uart, "enableDMAReceiveEvent");
  uart->DMA_TRIG_RX.IMASK = interrupt;
  FakeMSPM0::SyncPublisher(uart->DMA_TRIG_RX);
  if (FakeMSPM0::uart_enable_dma_rx_hook != nullptr)
  {
    FakeMSPM0::uart_enable_dma_rx_hook(uart, interrupt);
  }
}

inline void DL_UART_disableDMAReceiveEvent(UART_Regs* uart, uint32_t interrupt)
{
  FakeMSPM0::TraceUartMmio(uart, "disableDMAReceiveEvent");
  uart->DMA_TRIG_RX.IMASK &= ~interrupt;
  FakeMSPM0::SyncPublisher(uart->DMA_TRIG_RX);
}

inline uint32_t DL_UART_getEnabledDMAReceiveEvent(const UART_Regs* uart, uint32_t mask)
{
  FakeMSPM0::TraceUartMmio(const_cast<UART_Regs*>(uart), "getEnabledDMAReceiveEvent");
  return uart->DMA_TRIG_RX.IMASK & mask;
}

inline uint32_t DL_UART_getEnabledDMAReceiveEventStatus(const UART_Regs* uart,
                                                        uint32_t mask)
{
  FakeMSPM0::TraceUartMmio(const_cast<UART_Regs*>(uart),
                           "getEnabledDMAReceiveEventStatus");
  return uart->DMA_TRIG_RX.MIS & mask;
}

inline uint32_t DL_UART_getRawDMAReceiveEventStatus(const UART_Regs* uart, uint32_t mask)
{
  FakeMSPM0::TraceUartMmio(const_cast<UART_Regs*>(uart), "getRawDMAReceiveEventStatus");
  return uart->DMA_TRIG_RX.RIS & mask;
}

inline void DL_UART_clearDMAReceiveEventStatus(UART_Regs* uart, uint32_t mask)
{
  FakeMSPM0::TraceUartMmio(uart, "clearDMAReceiveEventStatus");
  uart->DMA_TRIG_RX.ICLR = mask;
  uart->DMA_TRIG_RX.RIS &= ~mask;
  FakeMSPM0::SyncPublisher(uart->DMA_TRIG_RX);
  if (FakeMSPM0::uart_clear_dma_rx_hook != nullptr)
  {
    FakeMSPM0::uart_clear_dma_rx_hook(uart, mask);
  }
}

inline void DL_UART_enableDMATransmitEvent(UART_Regs* uart)
{
  FakeMSPM0::TraceUartMmio(uart, "enableDMATransmitEvent");
  uart->DMA_TRIG_TX.IMASK = DL_UART_DMA_INTERRUPT_TX;
  FakeMSPM0::SyncPublisher(uart->DMA_TRIG_TX);
}

inline void DL_UART_disableDMATransmitEvent(UART_Regs* uart)
{
  FakeMSPM0::TraceUartMmio(uart, "disableDMATransmitEvent");
  uart->DMA_TRIG_TX.IMASK = 0U;
  FakeMSPM0::SyncPublisher(uart->DMA_TRIG_TX);
}

inline uint32_t DL_UART_getEnabledDMATransmitEvent(const UART_Regs* uart)
{
  FakeMSPM0::TraceUartMmio(const_cast<UART_Regs*>(uart), "getEnabledDMATransmitEvent");
  return uart->DMA_TRIG_TX.IMASK;
}

inline void DL_UART_clearDMATransmitEventStatus(UART_Regs* uart)
{
  FakeMSPM0::TraceUartMmio(uart, "clearDMATransmitEventStatus");
  uart->DMA_TRIG_TX.ICLR = DL_UART_DMA_INTERRUPT_TX;
  uart->DMA_TRIG_TX.RIS &= ~DL_UART_DMA_INTERRUPT_TX;
  FakeMSPM0::SyncPublisher(uart->DMA_TRIG_TX);
}

inline void DL_UART_setCommunicationMode(UART_Regs* uart, DL_UART_COMMUNICATION_MODE mode)
{
  FakeMSPM0::TraceUartMmio(uart, "setCommunicationMode");
  uart->communication_mode = mode;
}

inline void DL_UART_setAddressMask(UART_Regs* uart, uint32_t value)
{
  FakeMSPM0::TraceUartMmio(uart, "setAddressMask");
  uart->address_mask = value;
}

inline void DL_UART_setAddress(UART_Regs* uart, uint32_t value)
{
  FakeMSPM0::TraceUartMmio(uart, "setAddress");
  uart->address = value;
}

inline bool DL_UART_isLINCounterEnabled(const UART_Regs* uart)
{
  FakeMSPM0::TraceUartMmio(const_cast<UART_Regs*>(uart), "isLINCounterEnabled");
  return uart->lin_counter_enabled;
}

inline bool DL_UART_isLINCounterCompareMatchEnabled(const UART_Regs* uart)
{
  FakeMSPM0::TraceUartMmio(const_cast<UART_Regs*>(uart),
                           "isLINCounterCompareMatchEnabled");
  return uart->lin_compare_enabled;
}

inline void DL_UART_enableLINCounter(UART_Regs* uart)
{
  FakeMSPM0::TraceUartMmio(uart, "enableLINCounter");
  uart->lin_counter_enabled = true;
}

inline void DL_UART_enableLINCounterCompareMatch(UART_Regs* uart)
{
  FakeMSPM0::TraceUartMmio(uart, "enableLINCounterCompareMatch");
  uart->lin_compare_enabled = true;
  uart->lin_falling_edge_capture_enabled = false;
}

inline void DL_UART_enableLINCounterClearOnFallingEdge(UART_Regs* uart)
{
  FakeMSPM0::TraceUartMmio(uart, "enableLINCounterClearOnFallingEdge");
  uart->lin_counter_clear_on_falling_edge = true;
}

inline void DL_UART_enableLINFallingEdgeCapture(UART_Regs* uart)
{
  FakeMSPM0::TraceUartMmio(uart, "enableLINFallingEdgeCapture");
  uart->lin_falling_edge_capture_enabled = true;
  uart->lin_compare_enabled = false;
}

inline void DL_UART_setLINCounterValue(UART_Regs* uart, uint16_t value)
{
  FakeMSPM0::TraceUartMmio(uart, "setLINCounterValue");
  uart->lin_counter_value = value;
}

inline uint16_t DL_UART_getLINCounterValue(const UART_Regs* uart)
{
  FakeMSPM0::TraceUartMmio(const_cast<UART_Regs*>(uart), "getLINCounterValue");
  return static_cast<uint16_t>(uart->lin_counter_value);
}

inline void DL_UART_setLINCounterCompareValue(UART_Regs* uart, uint16_t value)
{
  FakeMSPM0::TraceUartMmio(uart, "setLINCounterCompareValue");
  uart->lin_compare_value = value;
}

inline void DL_UART_disableLINCountWhileLow(UART_Regs* uart)
{
  FakeMSPM0::TraceUartMmio(uart, "disableLINCountWhileLow");
  uart->lin_count_while_low = false;
}
