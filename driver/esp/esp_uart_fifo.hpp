#pragma once

#include <cstddef>
#include <cstdint>

#include "driver/gpio.h"
#include "esp_intr_alloc.h"
#if defined(CONFIG_PM_ENABLE) && CONFIG_PM_ENABLE
#include "esp_pm.h"
#endif
#include "esp_uart_execution_policy.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "hal/uart_hal.h"
#include "hal/uart_types.h"
#include "model/uart_rx_config_gate.hpp"
#include "uart.hpp"

namespace LibXR
{

class ESP32UartFifo;

namespace Detail
{

class ESP32UartFifoReadPort : public ReadPort
{
 public:
  ESP32UartFifoReadPort(size_t size, ESP32UartFifo& owner) : ReadPort(size), owner_(owner)
  {
  }

  void OnRxDequeue(bool in_isr) override;

  ESP32UartFifoReadPort& operator=(ReadFun fun)
  {
    ReadPort::operator=(fun);
    return *this;
  }

 private:
  ESP32UartFifo& owner_;
};

}  // namespace Detail

/**
 * @brief ESP UART backend that streams queued records directly into the hardware FIFO.
 *
 * This class is selected explicitly by the application. It does not allocate DMA
 * storage, stage payloads, or preload a second record. A write completes after all
 * bytes of that record have entered the hardware FIFO; physical line idle is observed
 * separately only when CONFIG needs a framing boundary.
 *
 * @note Construct at most one process-lifetime backend object for each UART peripheral.
 * @note On SMP targets, construct this object from a task pinned to exactly one core so
 *       ESP-IDF allocates its non-shared UART interrupt on that fixed core.
 */
class ESP32UartFifo : public UART
{
  friend class Detail::ESP32UartFifoReadPort;
  friend class Detail::ESP32UartIrqAdapter<ESP32UartFifo>;

 public:
  static constexpr int PIN_NO_CHANGE = -1;

  ESP32UartFifo(uart_port_t uart_num, int tx_pin, int rx_pin, int rts_pin = PIN_NO_CHANGE,
                int cts_pin = PIN_NO_CHANGE, size_t rx_buffer_size = 1024,
                size_t tx_buffer_size = 512, uint32_t tx_queue_size = 5,
                UART::Configuration config = {115200, UART::Parity::NO_PARITY, 8, 1});

  /**
   * @brief Apply one serialized framing and baud configuration.
   * @return `BUSY` while an earlier configuration request is outstanding.
   * @warning On single-core DirectPolicy targets, do not call this method from a
   *          higher-priority ISR that can preempt this UART ISR after its hardware
   *          status read, or from inside that unfinished raw ISR path.
   */
  ErrorCode SetConfig(UART::Configuration config) override;

  /**
   * @brief Toggle the UART peripheral's internal loopback bit.
   * @warning The caller must quiesce traffic and configuration first.
   */
  ErrorCode SetLoopback(bool enable);

  static ErrorCode WriteFun(WritePort& port, bool in_isr);
  static ErrorCode ReadFun(ReadPort& port, bool in_isr);

 private:
  using ExecutionPolicy = Detail::ESP32UartExecutionPolicy<ESP32UartFifo>;

  enum class Event : uint32_t
  {
    WRITE = 1U << 0U,
    TX_SPACE = 1U << 1U,
    TX_IDLE = 1U << 2U,
    CONFIG = 1U << 3U,
    RX_DATA = 1U << 4U,
    RX_SPACE = 1U << 5U,
    RX_PARITY_ERROR = 1U << 6U,
    RX_FRAME_ERROR = 1U << 7U,
    RX_OVERFLOW = 1U << 8U,
    CONTROL_READY = 1U << 9U,
  };

  enum class ConfigState : uint8_t
  {
    NORMAL = 0,
    DRAINING_RECORD,
    WAITING_LINE_IDLE,
  };

  struct SubmitContext
  {
    ErrorCode result = ErrorCode::PENDING;
    bool resolved = false;
    bool synchronous_completion_allowed = true;
  };

  static constexpr uint32_t EventMask(Event event)
  {
    return static_cast<uint32_t>(event);
  }

  static bool ResolveWordLength(uint8_t data_bits, uart_word_length_t& out);
  static bool ResolveStopBits(uint8_t stop_bits, uart_stop_bits_t& out);
  static uart_parity_t ResolveParity(UART::Parity parity);
  static bool IsBaudrateRepresentable(uint32_t baudrate, uint32_t source_clock_hz);
  static bool IsCurrentTaskPinned();

  [[nodiscard]] ErrorCode ValidateConfig(UART::Configuration config) const;
  bool ApplyConfigPayload(UART::Configuration config);
  ErrorCode InitPowerManagement();
  ErrorCode InitUartHardware(int tx_pin, int rx_pin, int rts_pin, int cts_pin);
  ErrorCode ConfigurePins(int tx_pin, int rx_pin, int rts_pin, int cts_pin);
  ErrorCode InstallUartIsr();

  void ConfigureRxInterruptPath();
  void SetOwnedInterruptsEnabled(uint32_t mask, bool enabled) noexcept;
  void SetRxInterruptPathEnabled(bool enabled);
  void ArmTxSpaceInterrupt();
  void DisarmTxSpaceInterrupt();
  void ArmConfigTxIdleInterrupt();
  void DisarmConfigTxIdleInterrupt();

  void SetIrqDomainEnabled(bool enabled) noexcept;
  void SetIrqDomainEnabledLocked(bool enabled) noexcept;

  static void UartIsrEntry(void* arg);
  uint32_t ServiceIrqSource(bool in_isr) noexcept;
  uint32_t ServiceEvents(uint32_t events, bool in_isr, SubmitContext* submit,
                         bool& pushed_any) noexcept;

  ErrorCode SubmitWrite(bool in_isr);
  void ResumeRx(bool in_isr);
  uint32_t ServiceRx(uint32_t events, bool in_isr, bool& pushed_any);
  bool DrainRxFifo(bool in_isr);

  void BeginConfiguration();
  uint32_t ContinueConfiguration(bool in_isr);

  void ProgressTx(bool in_isr, SubmitContext* submit);
  bool ClaimNextRecord(bool in_isr, SubmitContext* submit, bool& synchronous_submission);
  [[nodiscard]] bool HasCurrentRecord() const;
  bool FillCurrentRecord(bool in_isr, bool synchronous_submission, SubmitContext* submit);
  void CompleteCurrentRecord(bool in_isr, bool synchronous_submission,
                             SubmitContext* submit);
  void ClearCurrentRecord();

  uart_port_t uart_num_;

  UART::Configuration requested_config_{};
  uint32_t uart_sclk_hz_ = 0U;

  portMUX_TYPE irq_domain_lock_ = portMUX_INITIALIZER_UNLOCKED;
  bool irq_domain_masked_ = true;
  ExecutionPolicy execution_policy_;
  UartRxConfigGate rx_config_gate_{};

  ConfigState config_state_ = ConfigState::NORMAL;
  bool tx_space_interrupt_armed_ = false;
  bool config_tx_idle_interrupt_armed_ = false;
  bool rx_interrupt_path_enabled_ = false;

  WriteInfoBlock current_record_{};
  size_t current_record_offset_ = 0U;

  bool uart_hw_enabled_ = false;
  uart_hal_context_t uart_hal_ = {};
  intr_handle_t uart_intr_handle_ = nullptr;
  bool uart_isr_installed_ = false;
#if defined(CONFIG_PM_ENABLE) && CONFIG_PM_ENABLE
  esp_pm_lock_handle_t pm_lock_ = nullptr;
#endif

  Detail::ESP32UartFifoReadPort _read_port;
  WritePort _write_port;
};

}  // namespace LibXR
