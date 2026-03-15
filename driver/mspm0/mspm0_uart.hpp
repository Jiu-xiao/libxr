#pragma once

#include "timer.hpp"
#include "dl_uart_main.h"
#include "ti_msp_dl_config.h"
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
  };

  MSPM0UART(Resources res, RawData rx_stage_buffer, uint32_t tx_queue_size = 5,
            uint32_t tx_buffer_size = 128,
            UART::Configuration config = {115200, UART::Parity::NO_PARITY, 8, 1});

  ErrorCode SetConfig(UART::Configuration config) override;

  template <typename OperationType, typename = std::enable_if_t<std::is_base_of_v<
                                        ReadOperation, std::decay_t<OperationType>>>>
  ErrorCode Read(RawData data, OperationType&& op, bool in_isr = false)
  {
    auto& read_op = op;
    bool block_timeout_modified = false;
    uint32_t original_block_timeout = UINT32_MAX;

    if ((rx_timeout_mode_ == RxTimeoutMode::BYTE_INTERRUPT) &&
        (read_op.type == ReadOperation::OperationType::BLOCK) &&
        (read_op.data.sem_info.timeout != UINT32_MAX))
    {
      block_timeout_modified = true;
      original_block_timeout = read_op.data.sem_info.timeout;
      read_op.data.sem_info.timeout = NormalizeByteModeBlockTimeout(original_block_timeout);
    }

    const ErrorCode ans = (*read_port_)(data, read_op, in_isr);

    if (block_timeout_modified)
    {
      if ((ans == ErrorCode::TIMEOUT) && (rx_timeout_mode_ == RxTimeoutMode::BYTE_INTERRUPT))
      {
        CancelByteModeBlockTimeout();
        if (read_port_->busy_.load(std::memory_order_acquire) == ReadPort::BusyState::PENDING)
        {
          read_port_->ProcessPendingReads(false);
          if (read_port_->busy_.load(std::memory_order_acquire) ==
              ReadPort::BusyState::PENDING)
          {
            read_port_->busy_.store(ReadPort::BusyState::IDLE,
                                    std::memory_order_release);
          }
        }
      }
      read_op.data.sem_info.timeout = original_block_timeout;
    }

    return ans;
  }

  static ErrorCode WriteFun(WritePort& port, bool in_isr);

  static ErrorCode ReadFun(ReadPort& port, bool in_isr);

  static void OnInterrupt(uint8_t index);
  static UART::Configuration BuildConfigFromSysCfg(UART_Regs* instance,
                                                   uint32_t baudrate);

  RxTimeoutMode GetRxTimeoutMode() const { return rx_timeout_mode_; }
  uint32_t GetRxTimeoutCount() const { return rx_timeout_count_; }
  uint32_t GetRxDropCount() const { return rx_drop_count_; }
  uint16_t GetLinCompareWindow() const { return lin_compare_window_; }
  uint32_t GetLastTimeoutConsumedSize() const { return last_timeout_consumed_size_; }
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
#if defined(UART0_BASE)
      case UART0_INT_IRQn:
        return 0;
#endif
#if defined(UART1_BASE)
      case UART1_INT_IRQn:
        return 1;
#endif
#if defined(UART2_BASE)
      case UART2_INT_IRQn:
        return 2;
#endif
#if defined(UART3_BASE)
      case UART3_INT_IRQn:
        return 3;
#endif
#if defined(UART4_BASE)
      case UART4_INT_IRQn:
        return 4;
#endif
#if defined(UART5_BASE)
      case UART5_INT_IRQn:
        return 5;
#endif
#if defined(UART6_BASE)
      case UART6_INT_IRQn:
        return 6;
#endif
#if defined(UART7_BASE)
      case UART7_INT_IRQn:
        return 7;
#endif
      default:
        return INVALID_INSTANCE_INDEX;
    }
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

  void ApplyRxTimeoutMode();

  RxTimeoutMode ResolveRxTimeoutMode() const;

  void EnsureByteModeBlockTimeoutTask();

  void ArmByteModeBlockTimeout(uint32_t timeout_ms);

  void CancelByteModeBlockTimeout();

  static void OnByteModeBlockTimeout(MSPM0UART* uart);

  uint32_t NormalizeByteModeBlockTimeout(uint32_t timeout_ms) const;

  uint32_t GetTimeoutInterruptMask() const;

  void RearmLinCompareTimeout();

  uint16_t GetLinCompareRegisterValue() const;

  size_t ConsumeTimedOutReadData(bool in_isr, bool copy_to_buffer);

  void ResetLinCounter();

  void DisableTxInterrupt();

  Resources res_;
  WriteInfoBlock tx_active_info_;
  bool tx_active_valid_ = false;
  size_t tx_active_remaining_ = 0;
  size_t tx_active_total_ = 0;
  RxTimeoutMode rx_timeout_mode_ = RxTimeoutMode::BYTE_INTERRUPT;
  uint16_t lin_compare_window_ = 0U;
  uint32_t rx_drop_count_ = 0;
  uint32_t rx_timeout_count_ = 0;
  uint32_t last_timeout_consumed_size_ = 0U;
  std::atomic<bool> lin_compare_timeout_latched_{false};
  Timer::TimerHandle byte_mode_block_timeout_task_ = nullptr;
  std::atomic<bool> byte_mode_block_timeout_armed_{false};

  static MSPM0UART* instance_map_[MAX_UART_INSTANCES];
};

// Helper macro to initialize MSPM0UART from SysConfig in one shot
#define MSPM0_UART_INIT(name, rx_stage_addr, rx_stage_size, tx_queue_size,               \
                        tx_buffer_size)                                                  \
  ::LibXR::MSPM0UART::Resources{name##_INST, name##_INST_INT_IRQN,                       \
                                name##_INST_FREQUENCY,                                   \
                                ::LibXR::MSPM0UART::ResolveIndex(name##_INST_INT_IRQN)}, \
      ::LibXR::RawData{(rx_stage_addr), (rx_stage_size)}, (tx_queue_size),               \
      (tx_buffer_size),                                                                  \
      ::LibXR::MSPM0UART::BuildConfigFromSysCfg(name##_INST,                             \
                                                static_cast<uint32_t>(name##_BAUD_RATE))

}  // namespace LibXR
