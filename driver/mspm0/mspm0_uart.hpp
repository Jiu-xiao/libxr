#pragma once

#include <atomic>

#include "dl_uart_main.h"
#include "ti_msp_dl_config.h"
#include "timer.hpp"
#include "uart.hpp"

namespace LibXR
{

class MSPM0UART : public UART
{
 public:
  using UART::Write;

  enum class RxTimeoutMode : uint8_t
  {
    LIN_COMPARE,
    BYTE_INTERRUPT
  };

  struct Resources
  {
    UART_Regs* instance;
    IRQn_Type irqn;
    uint32_t clock_freq;
    uint8_t index;
    bool use_lin_compare = false;
    uint16_t lin_compare_value = 0U;
  };

  MSPM0UART(Resources res, RawData rx_stage_buffer, uint32_t tx_queue_size = 5,
            uint32_t tx_buffer_size = 128,
            UART::Configuration config = {115200, UART::Parity::NO_PARITY, 8, 1});

  ErrorCode SetConfig(UART::Configuration config) override;

  static ErrorCode WriteFun(WritePort& port);

  static ErrorCode ReadFun(ReadPort& port);

  static void OnInterrupt(uint8_t index);
  static UART::Configuration BuildConfigFromSysCfg(UART_Regs* instance,
                                                   uint32_t baudrate);

  RxTimeoutMode GetRxTimeoutMode() const
  {
    return rx_timeout_mode_.load(std::memory_order_acquire);
  }
  uint32_t GetRxTimeoutCount() const
  {
    return rx_timeout_count_.load(std::memory_order_acquire);
  }
  uint32_t GetRxDropCount() const
  {
    return rx_drop_count_.load(std::memory_order_acquire);
  }
  uint16_t GetLinCompareWindow() const
  {
    return lin_compare_window_.load(std::memory_order_acquire);
  }
  uint32_t GetLastTimeoutConsumedSize() const
  {
    return last_timeout_consumed_size_.load(std::memory_order_acquire);
  }
  uint32_t GetTimeoutInterruptEnabledMask() const;
  uint32_t GetTimeoutInterruptMaskedStatus() const;
  uint32_t GetTimeoutInterruptRawStatus() const;
  uint32_t GetRxInterruptTimeoutValue() const;
  uint32_t GetRxFifoThresholdValue() const;

  ReadPort _read_port;    // NOLINT
  WritePort _write_port;  // NOLINT

  static constexpr uint8_t ResolveIndex(IRQn_Type irqn)
  {
    switch (irqn)
    {
#if defined(UART0_INT_IRQn)
      case UART0_INT_IRQn:
        return 0;
#endif
#if defined(UART1_INT_IRQn)
      case UART1_INT_IRQn:
        return 1;
#endif
#if defined(UART2_INT_IRQn)
      case UART2_INT_IRQn:
        return 2;
#endif
#if defined(UART3_INT_IRQn)
      case UART3_INT_IRQn:
        return 3;
#endif
#if defined(UART4_INT_IRQn)
      case UART4_INT_IRQn:
        return 4;
#endif
#if defined(UART5_INT_IRQn)
      case UART5_INT_IRQn:
        return 5;
#endif
#if defined(UART6_INT_IRQn)
      case UART6_INT_IRQn:
        return 6;
#endif
#if defined(UART7_INT_IRQn)
      case UART7_INT_IRQn:
        return 7;
#endif
      default:
        return INVALID_INSTANCE_INDEX;
    }
  }

  static uint8_t ResolveIndex(UART_Regs* instance)
  {
    if (instance == nullptr)
    {
      return INVALID_INSTANCE_INDEX;
    }
#if defined(UART0)
    if (instance == UART0)
    {
      return 0;
    }
#elif defined(UART0_BASE)
    if (instance == reinterpret_cast<UART_Regs*>(UART0_BASE))  // NOLINT
    {
      return 0;
    }
#endif
#if defined(UART1)
    if (instance == UART1)
    {
      return 1;
    }
#elif defined(UART1_BASE)
    if (instance == reinterpret_cast<UART_Regs*>(UART1_BASE))  // NOLINT
    {
      return 1;
    }
#endif
#if defined(UART2)
    if (instance == UART2)
    {
      return 2;
    }
#elif defined(UART2_BASE)
    if (instance == reinterpret_cast<UART_Regs*>(UART2_BASE))  // NOLINT
    {
      return 2;
    }
#endif
#if defined(UART3)
    if (instance == UART3)
    {
      return 3;
    }
#elif defined(UART3_BASE)
    if (instance == reinterpret_cast<UART_Regs*>(UART3_BASE))  // NOLINT
    {
      return 3;
    }
#endif
#if defined(UART4)
    if (instance == UART4)
    {
      return 4;
    }
#elif defined(UART4_BASE)
    if (instance == reinterpret_cast<UART_Regs*>(UART4_BASE))  // NOLINT
    {
      return 4;
    }
#endif
#if defined(UART5)
    if (instance == UART5)
    {
      return 5;
    }
#elif defined(UART5_BASE)
    if (instance == reinterpret_cast<UART_Regs*>(UART5_BASE))  // NOLINT
    {
      return 5;
    }
#endif
#if defined(UART6)
    if (instance == UART6)
    {
      return 6;
    }
#elif defined(UART6_BASE)
    if (instance == reinterpret_cast<UART_Regs*>(UART6_BASE))  // NOLINT
    {
      return 6;
    }
#endif
#if defined(UART7)
    if (instance == UART7)
    {
      return 7;
    }
#elif defined(UART7_BASE)
    if (instance == reinterpret_cast<UART_Regs*>(UART7_BASE))  // NOLINT
    {
      return 7;
    }
#endif
    return INVALID_INSTANCE_INDEX;
  }

 private:
  static constexpr uint8_t MAX_UART_INSTANCES = 8;
  static constexpr uint8_t INVALID_INSTANCE_INDEX = 0xFF;

  void HandleInterrupt();

  void HandleRxInterrupt(uint32_t timeout_mask);

  void HandleRxTimeoutInterrupt(uint32_t pending, uint32_t timeout_mask);

  void DrainRxFIFO(bool& received, bool& pushed);

  void HandleTxInterrupt(bool in_isr);

  void HandleErrorInterrupt(DL_UART_IIDX iidx);

  void CompletePendingReadOnTimeout(bool in_isr);

  ErrorCode ApplyRxTimeoutMode();

  RxTimeoutMode ResolveRxTimeoutMode() const;

  bool IsTxBusy() const;

  bool IsRxBusy() const;

  void KickTxIfPending();

  uint16_t ResolveLinCompareWindow() const;

  void EnsureByteModeBlockTimeoutTask();

  void ArmByteModeBlockTimeout(uint32_t timeout_ms);

  void CancelByteModeBlockTimeout();

  static void OnByteModeBlockTimeout(MSPM0UART* uart);

  uint32_t NormalizeByteModeBlockTimeout(uint32_t timeout_ms) const;

  uint32_t GetTimeoutInterruptMask() const;

  void RearmLinCompareTimeout();

  size_t ConsumeTimedOutReadData(bool in_isr, bool copy_to_buffer,
                                 size_t size_limit = static_cast<size_t>(-1));

  void ResetLinCounter();

  void DisableTxInterrupt();

  bool IsZeroTimeoutPendingBlockRead() const;

  Resources res_;
  WriteInfoBlock tx_active_info_;
  std::atomic<bool> tx_active_valid_{false};
  size_t tx_active_remaining_ = 0;
  size_t tx_active_total_ = 0;
  std::atomic<RxTimeoutMode> rx_timeout_mode_{RxTimeoutMode::BYTE_INTERRUPT};
  std::atomic<uint16_t> lin_compare_window_{0U};
  std::atomic<uint32_t> rx_drop_count_{0U};
  std::atomic<uint32_t> rx_timeout_count_{0U};
  std::atomic<uint32_t> last_timeout_consumed_size_{0U};
  std::atomic<bool> reconfig_in_progress_{false};
  std::atomic<uint32_t> io_handler_inflight_{0U};
  std::atomic<uint32_t> timeout_cb_inflight_{0U};
  std::atomic<bool> read_completion_inflight_{false};
  Timer::TimerHandle byte_mode_block_timeout_task_ = nullptr;
  std::atomic<uint32_t> byte_mode_block_timeout_epoch_{0U};
  std::atomic<uint32_t> byte_mode_block_timeout_start_ms_{0U};
  std::atomic<uint32_t> byte_mode_block_timeout_cycle_ms_{0U};
  std::atomic<bool> byte_mode_drop_detached_rx_{false};

  static std::atomic<MSPM0UART*> instance_map_[MAX_UART_INSTANCES];
};

// 按 SysConfig 宏推导 UARTx 的 LIN compare 开关和值（SysConfig 为唯一来源）/
// Resolve LIN compare enable/value from SysConfig macros (SysConfig is SSOT).
#if defined(UART_0_COUNTER_COMPARE_VALUE)
#define MSPM0_UART_LIN_COMPARE_ENABLED_UART_0 true
#define MSPM0_UART_LIN_COMPARE_VALUE_UART_0 \
  static_cast<uint16_t>(UART_0_COUNTER_COMPARE_VALUE)
#else
#define MSPM0_UART_LIN_COMPARE_ENABLED_UART_0 false
#define MSPM0_UART_LIN_COMPARE_VALUE_UART_0 0U
#endif

#if defined(UART_1_COUNTER_COMPARE_VALUE)
#define MSPM0_UART_LIN_COMPARE_ENABLED_UART_1 true
#define MSPM0_UART_LIN_COMPARE_VALUE_UART_1 \
  static_cast<uint16_t>(UART_1_COUNTER_COMPARE_VALUE)
#else
#define MSPM0_UART_LIN_COMPARE_ENABLED_UART_1 false
#define MSPM0_UART_LIN_COMPARE_VALUE_UART_1 0U
#endif

#if defined(UART_2_COUNTER_COMPARE_VALUE)
#define MSPM0_UART_LIN_COMPARE_ENABLED_UART_2 true
#define MSPM0_UART_LIN_COMPARE_VALUE_UART_2 \
  static_cast<uint16_t>(UART_2_COUNTER_COMPARE_VALUE)
#else
#define MSPM0_UART_LIN_COMPARE_ENABLED_UART_2 false
#define MSPM0_UART_LIN_COMPARE_VALUE_UART_2 0U
#endif

#if defined(UART_3_COUNTER_COMPARE_VALUE)
#define MSPM0_UART_LIN_COMPARE_ENABLED_UART_3 true
#define MSPM0_UART_LIN_COMPARE_VALUE_UART_3 \
  static_cast<uint16_t>(UART_3_COUNTER_COMPARE_VALUE)
#else
#define MSPM0_UART_LIN_COMPARE_ENABLED_UART_3 false
#define MSPM0_UART_LIN_COMPARE_VALUE_UART_3 0U
#endif

#if defined(UART_4_COUNTER_COMPARE_VALUE)
#define MSPM0_UART_LIN_COMPARE_ENABLED_UART_4 true
#define MSPM0_UART_LIN_COMPARE_VALUE_UART_4 \
  static_cast<uint16_t>(UART_4_COUNTER_COMPARE_VALUE)
#else
#define MSPM0_UART_LIN_COMPARE_ENABLED_UART_4 false
#define MSPM0_UART_LIN_COMPARE_VALUE_UART_4 0U
#endif

#if defined(UART_5_COUNTER_COMPARE_VALUE)
#define MSPM0_UART_LIN_COMPARE_ENABLED_UART_5 true
#define MSPM0_UART_LIN_COMPARE_VALUE_UART_5 \
  static_cast<uint16_t>(UART_5_COUNTER_COMPARE_VALUE)
#else
#define MSPM0_UART_LIN_COMPARE_ENABLED_UART_5 false
#define MSPM0_UART_LIN_COMPARE_VALUE_UART_5 0U
#endif

#if defined(UART_6_COUNTER_COMPARE_VALUE)
#define MSPM0_UART_LIN_COMPARE_ENABLED_UART_6 true
#define MSPM0_UART_LIN_COMPARE_VALUE_UART_6 \
  static_cast<uint16_t>(UART_6_COUNTER_COMPARE_VALUE)
#else
#define MSPM0_UART_LIN_COMPARE_ENABLED_UART_6 false
#define MSPM0_UART_LIN_COMPARE_VALUE_UART_6 0U
#endif

#if defined(UART_7_COUNTER_COMPARE_VALUE)
#define MSPM0_UART_LIN_COMPARE_ENABLED_UART_7 true
#define MSPM0_UART_LIN_COMPARE_VALUE_UART_7 \
  static_cast<uint16_t>(UART_7_COUNTER_COMPARE_VALUE)
#else
#define MSPM0_UART_LIN_COMPARE_ENABLED_UART_7 false
#define MSPM0_UART_LIN_COMPARE_VALUE_UART_7 0U
#endif

#define MSPM0_UART_LIN_COMPARE_ENABLED_IMPL(name) MSPM0_UART_LIN_COMPARE_ENABLED_##name
#define MSPM0_UART_LIN_COMPARE_ENABLED(name) MSPM0_UART_LIN_COMPARE_ENABLED_IMPL(name)
#define MSPM0_UART_LIN_COMPARE_VALUE_IMPL(name) MSPM0_UART_LIN_COMPARE_VALUE_##name
#define MSPM0_UART_LIN_COMPARE_VALUE(name) MSPM0_UART_LIN_COMPARE_VALUE_IMPL(name)
#define MSPM0_UART_TOKEN_CAT_IMPL(left, right) left##right
#define MSPM0_UART_TOKEN_CAT(left, right) MSPM0_UART_TOKEN_CAT_IMPL(left, right)
#define MSPM0_UART_INST(name) MSPM0_UART_TOKEN_CAT(name, _INST)
#define MSPM0_UART_INST_INT_IRQN(name) MSPM0_UART_TOKEN_CAT(name, _INST_INT_IRQN)
#define MSPM0_UART_INST_FREQUENCY(name) MSPM0_UART_TOKEN_CAT(name, _INST_FREQUENCY)
#define MSPM0_UART_BAUD_RATE(name) MSPM0_UART_TOKEN_CAT(name, _BAUD_RATE)

#define MSPM0_UART_INIT(name, rx_stage_addr, rx_stage_size, tx_queue_size, \
                        tx_buffer_size)                                    \
  ::LibXR::MSPM0UART::Resources{                                           \
      MSPM0_UART_INST(name),                                               \
      MSPM0_UART_INST_INT_IRQN(name),                                      \
      MSPM0_UART_INST_FREQUENCY(name),                                     \
      ::LibXR::MSPM0UART::ResolveIndex(MSPM0_UART_INST_INT_IRQN(name)),    \
      MSPM0_UART_LIN_COMPARE_ENABLED(name),                                \
      MSPM0_UART_LIN_COMPARE_VALUE(name),                                  \
      ::LibXR::RawData{(rx_stage_addr), (rx_stage_size)}, (tx_queue_size), \
      (tx_buffer_size),                                                    \
      ::LibXR::MSPM0UART::BuildConfigFromSysCfg(                           \
          MSPM0_UART_INST(name), static_cast<uint32_t>(MSPM0_UART_BAUD_RATE(name)))

}  // namespace LibXR
