#pragma once

#include <ti/driverlib/dl_uart_main.h>

#include <atomic>

#include "ti_msp_dl_config.h"
#include "uart.hpp"

namespace LibXR
{

/**
 * @brief MSPM0 UART interrupt backend / MSPM0 UART 中断后端
 *
 * @note 每个硬件 UART index 只能绑定一个 application-lifetime 实例；无效或重复绑定会
 * 触发强运行时要求。 / Each hardware UART index may be bound to only one
 * application-lifetime instance. Invalid or duplicate binding fails a strong runtime
 * requirement.
 * Runtime configuration, TX, RX, and the raw UART interrupt are serialized by one IRQ
 * service owner. Public submissions only publish work and pend that IRQ, so task and
 * ordinary maskable-ISR callers never wait for hardware to become idle.
 * A TX record already committed by the IRQ owner finishes with the old configuration;
 * records not yet committed when CONFIG is admitted start with the new configuration.
 * The software RX queue is preserved, while bytes still in the hardware RX path during
 * the transition may be discarded.
 */
class MSPM0UART : public UART
{
 public:
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

  /**
   * @brief Submit one serialized UART configuration
   * @return `OK` when accepted, `BUSY` while another configuration is outstanding, or
   * `ARG_ERR` for an invalid configuration
   * @note Hardware application may complete after this function returns. This function
   * is safe in task context and ordinary maskable ISRs.
   * @note The admission protocol is single-core and does not support NMI, HardFault, or
   * concurrent callers from another CPU.
   * @note An already committed TX record keeps the old framing. Records not yet
   * committed use the new framing. Software-queued RX bytes remain available, but
   * hardware RX bytes crossing the reconfiguration boundary may be discarded.
   */
  ErrorCode SetConfig(UART::Configuration config) override;

  static ErrorCode WriteFun(WritePort& port, bool in_isr);

  static void OnInterrupt(uint8_t index);
  static UART::Configuration BuildConfigFromSysCfg(UART_Regs* instance,
                                                   uint32_t baudrate);

  RxTimeoutMode GetRxTimeoutMode() const { return rx_timeout_mode_; }
  uint32_t GetRxTimeoutCount() const
  {
    return rx_timeout_count_.load(std::memory_order_relaxed);
  }
  uint32_t GetRxDropCount() const
  {
    return rx_drop_count_.load(std::memory_order_relaxed);
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

  enum class Event : uint32_t
  {
    WRITE = 1U << 0U,
    RX_DATA = 1U << 3U,
    RX_TIMEOUT = 1U << 4U,
    TX_SPACE = 1U << 5U,
    TX_EOT = 1U << 6U,
    ERROR = 1U << 7U,
    CONFIG = 1U << 8U,
  };

  enum class ConfigState : uint8_t
  {
    NORMAL = 0,
    TX_DRAIN,
    WAITING_DISABLED_IDLE,
  };

  enum class ConfigAdmission : uint32_t
  {
    IDLE = 0U,
    RESERVED,
    PENDING,
  };

  enum class TxRecordState : uint8_t
  {
    EMPTY = 0U,
    HELD,
    ACTIVE,
  };

  static constexpr uint32_t EventMask(Event event)
  {
    return static_cast<uint32_t>(event);
  }

  void HandleInterrupt();

  [[nodiscard]] ErrorCode ValidateConfig(UART::Configuration config) const;
  bool TryReserveConfig();
  void PublishConfig();
  [[nodiscard]] bool ConfigPublished() const;
  [[nodiscard]] bool ConfigRequested() const;
  void CompleteConfig();
  void ApplyInitialConfig(UART::Configuration config);
  void ApplyDisabledConfig(UART::Configuration config);

  void Notify(Event event) noexcept;
  uint32_t CaptureIrqEvents() noexcept;
  uint32_t ServiceEvents(uint32_t events, bool& pushed_any) noexcept;

  void BeginConfiguration();
  uint32_t ContinueConfiguration(uint32_t events);

  void PauseRxTimeout();
  void ArmRxTimeout();
  uint32_t ServiceRx(uint32_t events, bool& pushed_any);
  void DrainRxFIFO(bool& received, bool& pushed);
  void DiscardRxFIFO();

  void ProgressTx();
  bool ClaimNextRecord();
  bool TryCommitCurrentRecord();
  bool FillCurrentRecord();
  [[nodiscard]] bool HasCurrentRecord() const;
  [[nodiscard]] bool HasActiveRecord() const;
  void CompleteCurrentRecord(ErrorCode result);
  void ClearCurrentRecord();

  void ApplyRxTimeoutMode();

  RxTimeoutMode ResolveRxTimeoutMode() const;

  uint32_t GetTimeoutInterruptMask() const;

  void ResetLinCounter();

  void ConfigureRxInterruptPath();
  void SetRxInterruptPathEnabled(bool enabled);
  void ArmTxSpaceInterrupt();
  void DisarmTxSpaceInterrupt();
  void ArmConfigEotInterrupt();
  void DisarmConfigEotInterrupt();

  Resources res_;
  UART::Configuration requested_config_{};
  std::atomic<uint32_t> pending_events_{0U};
  std::atomic<uint32_t> config_admission_{static_cast<uint32_t>(ConfigAdmission::IDLE)};

  WriteInfoBlock tx_record_info_;
  size_t tx_record_remaining_ = 0;
  TxRecordState tx_record_state_ = TxRecordState::EMPTY;
  ConfigState config_state_ = ConfigState::NORMAL;
  bool tx_interrupt_armed_ = false;
  bool config_eot_interrupt_armed_ = false;
  bool rx_interrupt_path_enabled_ = false;
  bool rx_timeout_interrupt_armed_ = false;
  bool tx_line_active_ = false;
  RxTimeoutMode rx_timeout_mode_ = RxTimeoutMode::BYTE_INTERRUPT;
  std::atomic<uint32_t> rx_drop_count_{0U};
  std::atomic<uint32_t> rx_timeout_count_{0U};

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
