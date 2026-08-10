#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>

using IRQn_Type = int32_t;
inline constexpr IRQn_Type DMA_INT_IRQn = 31;

struct UART_Regs;

struct FakeInterruptPublisher
{
  uint32_t IMASK = 0U;
  uint32_t RIS = 0U;
  uint32_t MIS = 0U;
  uint32_t ICLR = 0U;
  uint32_t ISET = 0U;
};

struct FakeRxDataRegister
{
  UART_Regs* owner = nullptr;
  operator uint32_t() const;
};

struct UART_Regs
{
  FakeInterruptPublisher CPU_INT{};
  FakeInterruptPublisher DMA_TRIG_RX{};
  FakeInterruptPublisher DMA_TRIG_TX{};

  uint32_t TXDATA = 0U;
  FakeRxDataRegister RXDATA{};
  uint32_t baudrate = 0U;
  uint32_t baudrate_changes = 0U;
  uint32_t rx_interrupt_timeout = 0U;
  uint32_t rx_fifo_threshold = 0U;
  uint32_t tx_fifo_threshold = 0U;
  uint32_t lin_counter_value = 0U;
  uint32_t lin_compare_value = 0U;
  uint32_t address_mask = 0U;
  uint32_t address = 0U;
  uint32_t word_length = 8U;
  uint32_t parity = 0U;
  uint32_t stop_bits = 1U;
  uint32_t direction = 1U;
  uint32_t communication_mode = 0U;
  IRQn_Type irqn = -1;

  bool enabled = false;
  bool fifos_enabled = false;
  bool busy = false;
  bool tx_fifo_empty = true;
  bool tx_fifo_full = false;
  bool lin_counter_enabled = false;
  bool lin_compare_enabled = false;
  bool lin_falling_edge_capture_enabled = false;
  bool lin_counter_clear_on_falling_edge = false;
  bool lin_count_while_low = true;

  std::array<uint32_t, 4U> rx_fifo{};
  size_t rx_head = 0U;
  size_t rx_size = 0U;
  std::array<uint8_t, 256U> tx_history{};
  size_t tx_history_size = 0U;
};

namespace FakeMSPM0
{

struct UartMmioCall
{
  const char* operation = nullptr;
  UART_Regs* uart = nullptr;
  bool in_owner = false;
};

inline std::array<UART_Regs, 8U> uart_regs{};
inline std::array<bool, 64U> nvic_enabled{};
inline std::array<bool, 64U> nvic_pending{};
inline std::array<bool, 64U> nvic_software_pending{};
inline std::array<UartMmioCall, 2048U> uart_mmio_calls{};
inline size_t uart_mmio_call_count = 0U;
inline size_t uart_owner_violation_count = 0U;
inline size_t uart_irq_entry_count = 0U;
inline size_t dmb_count = 0U;
inline size_t dsb_count = 0U;
inline size_t isb_count = 0U;
inline uint32_t primask = 0U;
inline uint32_t ipsr = 0U;
inline void (*primask_restore_hook)() = nullptr;
inline void (*irq_exit_hook)(IRQn_Type) = nullptr;
inline void (*rxdata_read_hook)(UART_Regs*) = nullptr;
inline bool primask_restore_hook_active = false;
inline bool enforce_uart_owner = false;
inline UART_Regs* active_uart_owner = nullptr;

inline bool ValidIrq(IRQn_Type irqn)
{
  return irqn >= 0 && static_cast<size_t>(irqn) < nvic_enabled.size();
}

inline void SyncPublisher(FakeInterruptPublisher& publisher)
{
  publisher.MIS = publisher.RIS & publisher.IMASK;
}

inline void ResetRuntime()
{
  for (auto& uart : uart_regs)
  {
    uart = UART_Regs{};
    uart.RXDATA.owner = &uart;
  }
  nvic_enabled.fill(false);
  nvic_pending.fill(false);
  nvic_software_pending.fill(false);
  uart_mmio_calls = {};
  uart_mmio_call_count = 0U;
  uart_owner_violation_count = 0U;
  uart_irq_entry_count = 0U;
  dmb_count = 0U;
  dsb_count = 0U;
  isb_count = 0U;
  primask = 0U;
  ipsr = 0U;
  primask_restore_hook = nullptr;
  irq_exit_hook = nullptr;
  rxdata_read_hook = nullptr;
  primask_restore_hook_active = false;
  enforce_uart_owner = false;
  active_uart_owner = nullptr;
}

inline void BindIrq(UART_Regs* uart, IRQn_Type irqn) { uart->irqn = irqn; }

inline void BeginUartOwnerEnforcement()
{
  enforce_uart_owner = true;
  uart_owner_violation_count = 0U;
  uart_mmio_call_count = 0U;
}

inline void EndUartOwnerEnforcement() { enforce_uart_owner = false; }

inline void TraceUartMmio(UART_Regs* uart, const char* operation)
{
  const bool in_owner = active_uart_owner == uart;
  if (uart_mmio_call_count < uart_mmio_calls.size())
  {
    uart_mmio_calls[uart_mmio_call_count++] = {operation, uart, in_owner};
  }
  if (enforce_uart_owner && !in_owner)
  {
    ++uart_owner_violation_count;
  }
}

class UartIrqScope
{
 public:
  explicit UartIrqScope(UART_Regs* uart)
      : uart_(uart), previous_(active_uart_owner), previous_ipsr_(ipsr)
  {
    active_uart_owner = uart;
    ipsr = static_cast<uint32_t>(uart->irqn) + 16U;
    ++uart_irq_entry_count;
    if (ValidIrq(uart->irqn))
    {
      const size_t index = static_cast<size_t>(uart->irqn);
      nvic_pending[index] = false;
      nvic_software_pending[index] = false;
    }
  }

  ~UartIrqScope()
  {
    if (irq_exit_hook != nullptr)
    {
      irq_exit_hook(uart_->irqn);
    }
    active_uart_owner = previous_;
    ipsr = previous_ipsr_;
  }

  UartIrqScope(const UartIrqScope&) = delete;
  UartIrqScope& operator=(const UartIrqScope&) = delete;

 private:
  UART_Regs* uart_;
  UART_Regs* previous_;
  uint32_t previous_ipsr_;
};

class IrqScope
{
 public:
  explicit IrqScope(IRQn_Type irqn)
      : irqn_(irqn), previous_owner_(active_uart_owner), previous_ipsr_(ipsr)
  {
    active_uart_owner = nullptr;
    ipsr = static_cast<uint32_t>(irqn) + 16U;
    if (ValidIrq(irqn))
    {
      const size_t index = static_cast<size_t>(irqn);
      nvic_pending[index] = false;
      nvic_software_pending[index] = false;
    }
  }

  ~IrqScope()
  {
    if (irq_exit_hook != nullptr)
    {
      irq_exit_hook(irqn_);
    }
    active_uart_owner = previous_owner_;
    ipsr = previous_ipsr_;
  }

  IrqScope(const IrqScope&) = delete;
  IrqScope& operator=(const IrqScope&) = delete;

 private:
  IRQn_Type irqn_;
  UART_Regs* previous_owner_;
  uint32_t previous_ipsr_;
};

template <typename Function>
decltype(auto) InvokeUartIrq(UART_Regs* uart, Function&& function)
{
  UartIrqScope scope(uart);
  return std::forward<Function>(function)();
}

inline void RefreshRxData(UART_Regs* uart)
{
  if (uart->rx_size == 0U)
  {
    return;
  }
  const uint32_t word = uart->rx_fifo[uart->rx_head];
  (void)word;
}

inline uint32_t PopRxWord(UART_Regs* uart)
{
  TraceUartMmio(uart, "RXDATA.read");
  if (rxdata_read_hook != nullptr)
  {
    rxdata_read_hook(uart);
  }
  if (uart->rx_size == 0U)
  {
    return 0U;
  }

  const uint32_t word = uart->rx_fifo[uart->rx_head];
  uart->rx_head = (uart->rx_head + 1U) % uart->rx_fifo.size();
  --uart->rx_size;
  if (uart->rx_size == 0U)
  {
    uart->CPU_INT.RIS &= ~((uint32_t{1U} << 0U) | (uint32_t{1U} << 1U));
    uart->DMA_TRIG_RX.RIS = 0U;
    SyncPublisher(uart->CPU_INT);
    SyncPublisher(uart->DMA_TRIG_RX);
  }
  return word;
}

inline void SetPendingForPublisher(UART_Regs* uart)
{
  SyncPublisher(uart->CPU_INT);
  if (uart->CPU_INT.MIS != 0U && ValidIrq(uart->irqn))
  {
    nvic_pending[static_cast<size_t>(uart->irqn)] = true;
  }
}

inline void SetPeripheralPending(IRQn_Type irqn)
{
  if (ValidIrq(irqn))
  {
    nvic_pending[static_cast<size_t>(irqn)] = true;
  }
}

}  // namespace FakeMSPM0

inline FakeRxDataRegister::operator uint32_t() const
{
  return owner == nullptr ? 0U : FakeMSPM0::PopRxWord(owner);
}

inline void NVIC_DisableIRQ(IRQn_Type irqn)
{
  if (FakeMSPM0::ValidIrq(irqn))
  {
    FakeMSPM0::nvic_enabled[static_cast<size_t>(irqn)] = false;
  }
}

inline void NVIC_EnableIRQ(IRQn_Type irqn)
{
  if (FakeMSPM0::ValidIrq(irqn))
  {
    FakeMSPM0::nvic_enabled[static_cast<size_t>(irqn)] = true;
  }
}

inline uint32_t NVIC_GetEnableIRQ(IRQn_Type irqn)
{
  return FakeMSPM0::ValidIrq(irqn) && FakeMSPM0::nvic_enabled[static_cast<size_t>(irqn)]
             ? 1U
             : 0U;
}

inline void NVIC_ClearPendingIRQ(IRQn_Type irqn)
{
  if (FakeMSPM0::ValidIrq(irqn))
  {
    const size_t index = static_cast<size_t>(irqn);
    FakeMSPM0::nvic_pending[index] = false;
    FakeMSPM0::nvic_software_pending[index] = false;
  }
}

inline void NVIC_SetPendingIRQ(IRQn_Type irqn)
{
  if (FakeMSPM0::ValidIrq(irqn))
  {
    const size_t index = static_cast<size_t>(irqn);
    FakeMSPM0::nvic_software_pending[index] = true;
    FakeMSPM0::nvic_pending[index] = true;
  }
}

inline uint32_t NVIC_GetPendingIRQ(IRQn_Type irqn)
{
  return FakeMSPM0::ValidIrq(irqn) && FakeMSPM0::nvic_pending[static_cast<size_t>(irqn)]
             ? 1U
             : 0U;
}

inline void __DMB()
{
  ++FakeMSPM0::dmb_count;
  std::atomic_thread_fence(std::memory_order_seq_cst);
}

inline void __DSB()
{
  ++FakeMSPM0::dsb_count;
  std::atomic_thread_fence(std::memory_order_seq_cst);
}

inline void __ISB()
{
  ++FakeMSPM0::isb_count;
  std::atomic_thread_fence(std::memory_order_seq_cst);
}

inline uint32_t __get_PRIMASK() { return FakeMSPM0::primask; }

inline uint32_t __get_IPSR() { return FakeMSPM0::ipsr; }

inline void __disable_irq() { FakeMSPM0::primask = 1U; }

inline void __set_PRIMASK(uint32_t value)
{
  const uint32_t previous = FakeMSPM0::primask;
  FakeMSPM0::primask = value & 1U;
  if (previous != 0U && FakeMSPM0::primask == 0U &&
      FakeMSPM0::primask_restore_hook != nullptr &&
      !FakeMSPM0::primask_restore_hook_active)
  {
    FakeMSPM0::primask_restore_hook_active = true;
    FakeMSPM0::primask_restore_hook();
    FakeMSPM0::primask_restore_hook_active = false;
  }
}
