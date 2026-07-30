#include "esp_uart_fifo_test.hpp"

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstring>

#include "driver/uart.h"
#include "hal/uart_ll.h"
#include "libxr.hpp"
#include "soc/soc_caps.h"

namespace LibXRTest
{
namespace
{

constexpr uint32_t RX_DATA_INTR_MASK = UART_INTR_RXFIFO_FULL | UART_INTR_RXFIFO_TOUT;
constexpr uint32_t RX_ERROR_INTR_MASK =
    UART_INTR_PARITY_ERR | UART_INTR_FRAM_ERR | UART_INTR_RXFIFO_OVF;
constexpr uint32_t RX_INTR_MASK = RX_DATA_INTR_MASK | RX_ERROR_INTR_MASK;

uint64_t ElapsedMicroseconds(uint64_t start_us)
{
  return static_cast<uint64_t>(LibXR::Timebase::GetMicroseconds()) - start_us;
}

bool TimedOut(uint32_t start_ms, uint32_t timeout_ms)
{
  const uint32_t now_ms = static_cast<uint32_t>(LibXR::Timebase::GetMilliseconds());
  return static_cast<uint32_t>(now_ms - start_ms) >= timeout_ms;
}

template <typename Predicate>
bool WaitUntil(Predicate predicate, uint32_t timeout_ms)
{
  const uint32_t start_ms = static_cast<uint32_t>(LibXR::Timebase::GetMilliseconds());
  do
  {
    if (predicate())
    {
      return true;
    }
    LibXR::Thread::Sleep(1U);
  } while (!TimedOut(start_ms, timeout_ms));
  return predicate();
}

uint8_t PatternByte(size_t offset, uint32_t seed)
{
  uint32_t value = seed ^ static_cast<uint32_t>(offset * 0x45D9F3BU);
  value ^= value >> 16U;
  value *= 0x7FEB352DU;
  value ^= value >> 15U;
  return static_cast<uint8_t>(value >> 24U);
}

void FillPattern(uint8_t* output, size_t size, size_t offset, uint32_t seed)
{
  for (size_t i = 0U; i < size; ++i)
  {
    output[i] = PatternByte(offset + i, seed);
  }
}

bool MatchesPattern(const uint8_t* input, size_t size, size_t offset, uint32_t seed)
{
  for (size_t i = 0U; i < size; ++i)
  {
    if (input[i] != PatternByte(offset + i, seed))
    {
      return false;
    }
  }
  return true;
}

size_t FindPatternMismatch(const uint8_t* input, size_t size, uint32_t seed)
{
  for (size_t i = 0U; i < size; ++i)
  {
    if (input[i] != PatternByte(i, seed))
    {
      return i;
    }
  }
  return size;
}

bool MakeObserverConfig(LibXR::UART::Configuration config, uart_config_t& output)
{
  if ((config.baudrate == 0U) || (config.baudrate > static_cast<uint32_t>(INT_MAX)))
  {
    return false;
  }

  output = {};
  output.baud_rate = static_cast<int>(config.baudrate);
  switch (config.data_bits)
  {
    case 5U:
      output.data_bits = UART_DATA_5_BITS;
      break;
    case 6U:
      output.data_bits = UART_DATA_6_BITS;
      break;
    case 7U:
      output.data_bits = UART_DATA_7_BITS;
      break;
    case 8U:
      output.data_bits = UART_DATA_8_BITS;
      break;
    default:
      return false;
  }

  switch (config.parity)
  {
    case LibXR::UART::Parity::NO_PARITY:
      output.parity = UART_PARITY_DISABLE;
      break;
    case LibXR::UART::Parity::EVEN:
      output.parity = UART_PARITY_EVEN;
      break;
    case LibXR::UART::Parity::ODD:
      output.parity = UART_PARITY_ODD;
      break;
    default:
      return false;
  }

  switch (config.stop_bits)
  {
    case 1U:
      output.stop_bits = UART_STOP_BITS_1;
      break;
    case 2U:
      output.stop_bits = UART_STOP_BITS_2;
      break;
    default:
      return false;
  }
  output.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  output.source_clk = UART_SCLK_DEFAULT;
  return true;
}

class UartRxObserver
{
 public:
  explicit UartRxObserver(uart_port_t uart_num) : uart_num_(uart_num) {}

  ~UartRxObserver() { (void)Close(); }

  LibXR::ErrorCode Open(int rx_pin, size_t buffer_size, LibXR::UART::Configuration config)
  {
    uart_config_t idf_config{};
    if ((rx_pin < 0) || (buffer_size <= UART_HW_FIFO_LEN(uart_num_)) ||
        (buffer_size > static_cast<size_t>(INT_MAX)) ||
        !MakeObserverConfig(config, idf_config) || uart_is_driver_installed(uart_num_))
    {
      return LibXR::ErrorCode::ARG_ERR;
    }
    if (uart_driver_install(uart_num_, static_cast<int>(buffer_size), 0, 0, nullptr, 0) !=
        ESP_OK)
    {
      return LibXR::ErrorCode::INIT_ERR;
    }
    installed_ = true;
    if ((uart_param_config(uart_num_, &idf_config) != ESP_OK) ||
        (uart_set_pin(uart_num_, UART_PIN_NO_CHANGE, rx_pin, UART_PIN_NO_CHANGE,
                      UART_PIN_NO_CHANGE) != ESP_OK) ||
        (uart_flush_input(uart_num_) != ESP_OK))
    {
      (void)Close();
      return LibXR::ErrorCode::INIT_ERR;
    }
    return LibXR::ErrorCode::OK;
  }

  size_t Read(uint8_t* output, size_t size, uint32_t timeout_ms, bool& read_failed)
  {
    size_t total = 0U;
    const uint32_t start_ms = static_cast<uint32_t>(LibXR::Timebase::GetMilliseconds());
    while ((total < size) && !TimedOut(start_ms, timeout_ms))
    {
      const int read_size =
          uart_read_bytes(uart_num_, output + total, static_cast<uint32_t>(size - total),
                          pdMS_TO_TICKS(1U));
      if (read_size < 0)
      {
        read_failed = true;
        break;
      }
      total += static_cast<size_t>(read_size);
    }
    return total;
  }

  LibXR::ErrorCode Close()
  {
    if (!installed_)
    {
      return LibXR::ErrorCode::OK;
    }
    if (uart_driver_delete(uart_num_) != ESP_OK)
    {
      return LibXR::ErrorCode::FAILED;
    }
    installed_ = false;
    return LibXR::ErrorCode::OK;
  }

 private:
  uart_port_t uart_num_;
  bool installed_ = false;
};

UartStressTrafficBarrierCase MakeBarrierCase(LibXR::UART::Configuration config,
                                             uint32_t timeout_ms, uint32_t quiet_time_ms)
{
  return {
      .final_config = config,
      .config_timeout_ms = timeout_ms,
      .marker_timeout_ms = timeout_ms,
      .retry_interval_ms = 1U,
      .rx_quiet_time_ms = quiet_time_ms,
  };
}

UartLoopbackTestCase MakeRecoveryCase(LibXR::UART::Configuration config,
                                      uint32_t timeout_ms, uint32_t quiet_time_ms,
                                      uint32_t seed)
{
  return {
      .uart_config = config,
      .frame_size = 1023U,
      .rounds = 2U,
      .operation_timeout_ms = timeout_ms,
      .rx_quiet_time_ms = quiet_time_ms,
      .pattern_seed = seed,
      .batch_depth = 2U,
  };
}

struct CallbackState
{
  std::atomic<uint32_t> count{0U};
  std::atomic<int32_t> last_error{static_cast<int32_t>(LibXR::ErrorCode::OK)};
};

void SetPartialFailure(EspUartFifoPartialConfigResult& result,
                       EspUartFifoPartialConfigFailure failure, LibXR::ErrorCode error)
{
  result.failure = failure;
  result.error = error;
}

void SetBackpressureFailure(EspUartFifoRxBackpressureResult& result,
                            EspUartFifoRxBackpressureFailure failure,
                            LibXR::ErrorCode error)
{
  result.failure = failure;
  result.error = error;
}

}  // namespace

EspUartFifoPartialConfigResult RunEspUartFifoPartialConfigTest(
    LibXR::ESP32UartFifo& uart, const EspUartFifoPartialConfigCase& test_case,
    LibXR::RawData tx_scratch, LibXR::RawData rx_scratch)
{
  EspUartFifoPartialConfigResult result;
  const uint64_t start_us = static_cast<uint64_t>(LibXR::Timebase::GetMicroseconds());

  if (test_case.observer_uart_num >= UART_NUM_MAX || test_case.observer_rx_pin < 0 ||
      test_case.frame_size <= SOC_UART_FIFO_LEN || test_case.operation_timeout_ms == 0U ||
      test_case.callback_quiet_time_ms == 0U || test_case.rx_quiet_time_ms == 0U ||
      tx_scratch.addr_ == nullptr || tx_scratch.size_ < test_case.frame_size ||
      rx_scratch.addr_ == nullptr ||
      rx_scratch.size_ < std::max(test_case.frame_size, size_t{2046U}))
  {
    SetPartialFailure(result, EspUartFifoPartialConfigFailure::INVALID_ARGUMENT,
                      LibXR::ErrorCode::ARG_ERR);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }

  result.retirement = RetireUartStressTraffic(
      uart,
      MakeBarrierCase(test_case.initial_config, test_case.operation_timeout_ms,
                      test_case.rx_quiet_time_ms),
      rx_scratch);
  if (!result.retirement.Passed())
  {
    SetPartialFailure(result, EspUartFifoPartialConfigFailure::PRECONDITION,
                      result.retirement.error);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }

  UartRxObserver observer(test_case.observer_uart_num);
  const LibXR::ErrorCode observer_setup = observer.Open(
      test_case.observer_rx_pin, test_case.frame_size * 2U, test_case.initial_config);
  if (observer_setup != LibXR::ErrorCode::OK)
  {
    SetPartialFailure(result, EspUartFifoPartialConfigFailure::OBSERVER_SETUP,
                      observer_setup);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }

  auto* tx = static_cast<uint8_t*>(tx_scratch.addr_);
  FillPattern(tx, test_case.frame_size, 0U, test_case.pattern_seed);

  // A queued operation can outlive a timed-out test call. Retain both the callback
  // object and its state for the startup lifetime so every diagnostic return remains
  // memory-safe.
  auto* callback_state = new CallbackState;
  auto* callback =
      new LibXR::Callback<LibXR::ErrorCode>(LibXR::Callback<LibXR::ErrorCode>::Create(
          [](bool, CallbackState* state, LibXR::ErrorCode error)
          {
            state->last_error.store(static_cast<int32_t>(error),
                                    std::memory_order_relaxed);
            state->count.fetch_add(1U, std::memory_order_release);
          },
          callback_state));
  LibXR::WriteOperation operation(*callback);

  const auto write_result = uart.Write({tx, test_case.frame_size}, operation, false);
  if (write_result != LibXR::ErrorCode::OK)
  {
    SetPartialFailure(result, EspUartFifoPartialConfigFailure::WRITE_SUBMIT,
                      write_result);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }

  result.queued_after_submit = uart.write_port_->Size();
  if (callback_state->count.load(std::memory_order_acquire) != 0U ||
      result.queued_after_submit == 0U ||
      result.queued_after_submit >= test_case.frame_size)
  {
    SetPartialFailure(result, EspUartFifoPartialConfigFailure::NOT_PARTIALLY_LOADED,
                      LibXR::ErrorCode::CHECK_ERR);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }

  result.first_config_result = uart.SetConfig(test_case.requested_config);
  if (result.first_config_result != LibXR::ErrorCode::OK)
  {
    SetPartialFailure(result, EspUartFifoPartialConfigFailure::CONFIG_ADMISSION,
                      result.first_config_result);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }

  result.overlapping_config_result = uart.SetConfig(test_case.overlapping_config);
  if (result.overlapping_config_result != LibXR::ErrorCode::BUSY)
  {
    SetPartialFailure(result, EspUartFifoPartialConfigFailure::OVERLAPPING_CONFIG,
                      result.overlapping_config_result);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }

  if (!WaitUntil([callback_state]()
                 { return callback_state->count.load(std::memory_order_acquire) != 0U; },
                 test_case.operation_timeout_ms))
  {
    SetPartialFailure(result, EspUartFifoPartialConfigFailure::CALLBACK_TIMEOUT,
                      LibXR::ErrorCode::TIMEOUT);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }

  result.callback_count = callback_state->count.load(std::memory_order_acquire);
  result.callback_error = static_cast<LibXR::ErrorCode>(
      callback_state->last_error.load(std::memory_order_relaxed));
  if (result.callback_error != LibXR::ErrorCode::OK)
  {
    SetPartialFailure(result, EspUartFifoPartialConfigFailure::CALLBACK_STATUS,
                      result.callback_error);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }
  if (result.callback_count != 1U)
  {
    SetPartialFailure(result, EspUartFifoPartialConfigFailure::CALLBACK_COUNT,
                      LibXR::ErrorCode::CHECK_ERR);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }

  bool observer_read_failed = false;
  auto* const observed = static_cast<uint8_t*>(rx_scratch.addr_);
  result.observer_bytes =
      observer.Read(observed, test_case.frame_size, test_case.operation_timeout_ms,
                    observer_read_failed);
  if (observer_read_failed || (result.observer_bytes != test_case.frame_size))
  {
    SetPartialFailure(
        result, EspUartFifoPartialConfigFailure::OBSERVER_READ,
        observer_read_failed ? LibXR::ErrorCode::FAILED : LibXR::ErrorCode::TIMEOUT);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }

  result.observer_mismatch_offset =
      FindPatternMismatch(observed, test_case.frame_size, test_case.pattern_seed);
  if (result.observer_mismatch_offset != test_case.frame_size)
  {
    SetPartialFailure(result, EspUartFifoPartialConfigFailure::OBSERVER_MISMATCH,
                      LibXR::ErrorCode::CHECK_ERR);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }

  uint8_t unexpected = 0U;
  result.observer_unexpected_bytes =
      observer.Read(&unexpected, 1U, test_case.rx_quiet_time_ms, observer_read_failed);
  if (observer_read_failed || (result.observer_unexpected_bytes != 0U))
  {
    SetPartialFailure(result, EspUartFifoPartialConfigFailure::OBSERVER_TRAILING,
                      LibXR::ErrorCode::CHECK_ERR);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }
  if (observer.Close() != LibXR::ErrorCode::OK)
  {
    SetPartialFailure(result, EspUartFifoPartialConfigFailure::OBSERVER_TEARDOWN,
                      LibXR::ErrorCode::FAILED);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }

  result.retirement = RetireUartStressTraffic(
      uart,
      MakeBarrierCase(test_case.final_config, test_case.operation_timeout_ms,
                      test_case.rx_quiet_time_ms),
      rx_scratch);
  if (!result.retirement.Passed())
  {
    SetPartialFailure(result, EspUartFifoPartialConfigFailure::RETIREMENT,
                      result.retirement.error);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }

  LibXR::Thread::Sleep(test_case.callback_quiet_time_ms);
  result.callback_count = callback_state->count.load(std::memory_order_acquire);
  if (result.callback_count != 1U)
  {
    SetPartialFailure(result, EspUartFifoPartialConfigFailure::CALLBACK_COUNT,
                      LibXR::ErrorCode::CHECK_ERR);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }

  result.recovery = RunUartLoopbackTest(
      uart,
      MakeRecoveryCase(test_case.final_config, test_case.operation_timeout_ms,
                       test_case.rx_quiet_time_ms, test_case.pattern_seed ^ 0xA11CE001U),
      tx_scratch, rx_scratch);
  result.callback_count = callback_state->count.load(std::memory_order_acquire);
  if (result.callback_count != 1U)
  {
    SetPartialFailure(result, EspUartFifoPartialConfigFailure::CALLBACK_COUNT,
                      LibXR::ErrorCode::CHECK_ERR);
  }
  else if (!result.recovery.Passed())
  {
    SetPartialFailure(result, EspUartFifoPartialConfigFailure::RECOVERY,
                      result.recovery.error);
  }
  result.elapsed_us = ElapsedMicroseconds(start_us);
  return result;
}

EspUartFifoRxBackpressureResult RunEspUartFifoRxBackpressureTest(
    LibXR::ESP32UartFifo& uart, const EspUartFifoRxBackpressureCase& test_case,
    LibXR::RawData tx_scratch, LibXR::RawData rx_scratch)
{
  EspUartFifoRxBackpressureResult result;
  const uint64_t start_us = static_cast<uint64_t>(LibXR::Timebase::GetMicroseconds());
  const size_t total_size = test_case.rx_queue_size + test_case.retained_fifo_bytes;

  if (test_case.rx_queue_size == 0U || test_case.retained_fifo_bytes == 0U ||
      test_case.retained_fifo_bytes >= SOC_UART_FIFO_LEN ||
      test_case.write_chunk_size == 0U || test_case.operation_timeout_ms == 0U ||
      test_case.rx_quiet_time_ms == 0U || tx_scratch.addr_ == nullptr ||
      tx_scratch.size_ < test_case.write_chunk_size || rx_scratch.addr_ == nullptr ||
      rx_scratch.size_ < test_case.rx_queue_size ||
      uart.read_port_->Size() + uart.read_port_->EmptySize() < test_case.rx_queue_size ||
      total_size < test_case.rx_queue_size)
  {
    SetBackpressureFailure(result, EspUartFifoRxBackpressureFailure::INVALID_ARGUMENT,
                           LibXR::ErrorCode::ARG_ERR);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }

  result.precondition = RetireUartStressTraffic(
      uart,
      MakeBarrierCase(test_case.config, test_case.operation_timeout_ms,
                      test_case.rx_quiet_time_ms),
      rx_scratch);
  if (!result.precondition.Passed())
  {
    SetBackpressureFailure(result, EspUartFifoRxBackpressureFailure::PRECONDITION,
                           result.precondition.error);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }

  auto* hw = UART_LL_GET_HW(test_case.uart_num);
  auto* tx = static_cast<uint8_t*>(tx_scratch.addr_);
  auto* rx = static_cast<uint8_t*>(rx_scratch.addr_);
  LibXR::Semaphore write_sem(0U);
  LibXR::WriteOperation write_operation(write_sem, test_case.operation_timeout_ms);

  for (size_t offset = 0U; offset < total_size;)
  {
    const size_t write_size = std::min(test_case.write_chunk_size, total_size - offset);
    FillPattern(tx, write_size, offset, test_case.pattern_seed);
    const auto write_result = uart.Write({tx, write_size}, write_operation, false);
    if (write_result != LibXR::ErrorCode::OK)
    {
      SetBackpressureFailure(result, EspUartFifoRxBackpressureFailure::WRITE,
                             write_result);
      result.elapsed_us = ElapsedMicroseconds(start_us);
      return result;
    }
    offset += write_size;
  }

  // Until this predicate succeeds, only non-destructive INT_ENA is sampled. FIFO
  // length and raw status are read below after the RX interrupt domain is masked.
  if (!WaitUntil(
          [&uart, hw, &test_case]()
          {
            return uart.read_port_->Size() == test_case.rx_queue_size &&
                   (uart_ll_get_intr_ena_status(hw) & RX_INTR_MASK) == 0U;
          },
          test_case.operation_timeout_ms))
  {
    result.queued_at_saturation = uart.read_port_->Size();
    result.rx_interrupts_when_full = uart_ll_get_intr_ena_status(hw) & RX_INTR_MASK;
    const auto failure = result.queued_at_saturation == test_case.rx_queue_size
                             ? EspUartFifoRxBackpressureFailure::RX_INTERRUPT_NOT_MASKED
                             : EspUartFifoRxBackpressureFailure::RX_QUEUE_TIMEOUT;
    SetBackpressureFailure(result, failure, LibXR::ErrorCode::TIMEOUT);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }

  result.queued_at_saturation = uart.read_port_->Size();
  result.rx_interrupts_when_full = uart_ll_get_intr_ena_status(hw) & RX_INTR_MASK;
  if (result.rx_interrupts_when_full != 0U)
  {
    SetBackpressureFailure(result,
                           EspUartFifoRxBackpressureFailure::RX_INTERRUPT_NOT_MASKED,
                           LibXR::ErrorCode::CHECK_ERR);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }

  if (!WaitUntil(
          [hw, &test_case]()
          {
            return static_cast<size_t>(uart_ll_get_rxfifo_len(hw)) ==
                   test_case.retained_fifo_bytes;
          },
          test_case.operation_timeout_ms))
  {
    result.fifo_tail_bytes = uart_ll_get_rxfifo_len(hw);
    SetBackpressureFailure(result, EspUartFifoRxBackpressureFailure::FIFO_TAIL_TIMEOUT,
                           LibXR::ErrorCode::TIMEOUT);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }

  result.fifo_tail_bytes = uart_ll_get_rxfifo_len(hw);
  result.raw_status_at_saturation = uart_ll_get_intraw_mask(hw) & RX_INTR_MASK;
  if ((result.raw_status_at_saturation & UART_INTR_RXFIFO_OVF) != 0U)
  {
    SetBackpressureFailure(result, EspUartFifoRxBackpressureFailure::RX_OVERFLOW,
                           LibXR::ErrorCode::CHECK_ERR);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }

  LibXR::Semaphore read_sem(0U);
  LibXR::ReadOperation read_operation(read_sem, test_case.operation_timeout_ms);
  const auto prefix_read =
      uart.Read({rx, test_case.rx_queue_size}, read_operation, false);
  if (prefix_read != LibXR::ErrorCode::OK)
  {
    SetBackpressureFailure(result, EspUartFifoRxBackpressureFailure::QUEUE_READ,
                           prefix_read);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }
  if (!MatchesPattern(rx, test_case.rx_queue_size, 0U, test_case.pattern_seed))
  {
    SetBackpressureFailure(result, EspUartFifoRxBackpressureFailure::PREFIX_MISMATCH,
                           LibXR::ErrorCode::CHECK_ERR);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }
  result.verified_bytes = test_case.rx_queue_size;

  if (!WaitUntil([&uart, &test_case]()
                 { return uart.read_port_->Size() == test_case.retained_fifo_bytes; },
                 test_case.operation_timeout_ms))
  {
    SetBackpressureFailure(result, EspUartFifoRxBackpressureFailure::RX_RESUME_TIMEOUT,
                           LibXR::ErrorCode::TIMEOUT);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }

  result.rx_interrupts_after_resume = uart_ll_get_intr_ena_status(hw) & RX_INTR_MASK;
  if (result.rx_interrupts_after_resume != RX_INTR_MASK)
  {
    SetBackpressureFailure(result,
                           EspUartFifoRxBackpressureFailure::RX_INTERRUPT_NOT_ENABLED,
                           LibXR::ErrorCode::CHECK_ERR);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }

  const auto tail_read =
      uart.Read({rx, test_case.retained_fifo_bytes}, read_operation, false);
  if (tail_read != LibXR::ErrorCode::OK)
  {
    SetBackpressureFailure(result, EspUartFifoRxBackpressureFailure::TAIL_READ,
                           tail_read);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }
  if (!MatchesPattern(rx, test_case.retained_fifo_bytes, test_case.rx_queue_size,
                      test_case.pattern_seed))
  {
    SetBackpressureFailure(result, EspUartFifoRxBackpressureFailure::TAIL_MISMATCH,
                           LibXR::ErrorCode::CHECK_ERR);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }
  result.verified_bytes += test_case.retained_fifo_bytes;

  result.recovery = RunUartLoopbackTest(
      uart,
      MakeRecoveryCase(test_case.recovery_config, test_case.operation_timeout_ms,
                       test_case.rx_quiet_time_ms, test_case.pattern_seed ^ 0xBACC0001U),
      tx_scratch, rx_scratch);
  if (!result.recovery.Passed())
  {
    SetBackpressureFailure(result, EspUartFifoRxBackpressureFailure::RECOVERY,
                           result.recovery.error);
  }
  result.elapsed_us = ElapsedMicroseconds(start_us);
  return result;
}

const char* EspUartFifoPartialConfigFailureName(EspUartFifoPartialConfigFailure failure)
{
  switch (failure)
  {
    case EspUartFifoPartialConfigFailure::NONE:
      return "NONE";
    case EspUartFifoPartialConfigFailure::INVALID_ARGUMENT:
      return "INVALID_ARGUMENT";
    case EspUartFifoPartialConfigFailure::PRECONDITION:
      return "PRECONDITION";
    case EspUartFifoPartialConfigFailure::OBSERVER_SETUP:
      return "OBSERVER_SETUP";
    case EspUartFifoPartialConfigFailure::WRITE_SUBMIT:
      return "WRITE_SUBMIT";
    case EspUartFifoPartialConfigFailure::NOT_PARTIALLY_LOADED:
      return "NOT_PARTIALLY_LOADED";
    case EspUartFifoPartialConfigFailure::CONFIG_ADMISSION:
      return "CONFIG_ADMISSION";
    case EspUartFifoPartialConfigFailure::OVERLAPPING_CONFIG:
      return "OVERLAPPING_CONFIG";
    case EspUartFifoPartialConfigFailure::CALLBACK_TIMEOUT:
      return "CALLBACK_TIMEOUT";
    case EspUartFifoPartialConfigFailure::CALLBACK_STATUS:
      return "CALLBACK_STATUS";
    case EspUartFifoPartialConfigFailure::CALLBACK_COUNT:
      return "CALLBACK_COUNT";
    case EspUartFifoPartialConfigFailure::OBSERVER_READ:
      return "OBSERVER_READ";
    case EspUartFifoPartialConfigFailure::OBSERVER_MISMATCH:
      return "OBSERVER_MISMATCH";
    case EspUartFifoPartialConfigFailure::OBSERVER_TRAILING:
      return "OBSERVER_TRAILING";
    case EspUartFifoPartialConfigFailure::OBSERVER_TEARDOWN:
      return "OBSERVER_TEARDOWN";
    case EspUartFifoPartialConfigFailure::RETIREMENT:
      return "RETIREMENT";
    case EspUartFifoPartialConfigFailure::RECOVERY:
      return "RECOVERY";
  }
  return "UNKNOWN";
}

const char* EspUartFifoRxBackpressureFailureName(EspUartFifoRxBackpressureFailure failure)
{
  switch (failure)
  {
    case EspUartFifoRxBackpressureFailure::NONE:
      return "NONE";
    case EspUartFifoRxBackpressureFailure::INVALID_ARGUMENT:
      return "INVALID_ARGUMENT";
    case EspUartFifoRxBackpressureFailure::PRECONDITION:
      return "PRECONDITION";
    case EspUartFifoRxBackpressureFailure::WRITE:
      return "WRITE";
    case EspUartFifoRxBackpressureFailure::RX_QUEUE_TIMEOUT:
      return "RX_QUEUE_TIMEOUT";
    case EspUartFifoRxBackpressureFailure::RX_INTERRUPT_NOT_MASKED:
      return "RX_INTERRUPT_NOT_MASKED";
    case EspUartFifoRxBackpressureFailure::FIFO_TAIL_TIMEOUT:
      return "FIFO_TAIL_TIMEOUT";
    case EspUartFifoRxBackpressureFailure::RX_OVERFLOW:
      return "RX_OVERFLOW";
    case EspUartFifoRxBackpressureFailure::QUEUE_READ:
      return "QUEUE_READ";
    case EspUartFifoRxBackpressureFailure::PREFIX_MISMATCH:
      return "PREFIX_MISMATCH";
    case EspUartFifoRxBackpressureFailure::RX_RESUME_TIMEOUT:
      return "RX_RESUME_TIMEOUT";
    case EspUartFifoRxBackpressureFailure::RX_INTERRUPT_NOT_ENABLED:
      return "RX_INTERRUPT_NOT_ENABLED";
    case EspUartFifoRxBackpressureFailure::TAIL_READ:
      return "TAIL_READ";
    case EspUartFifoRxBackpressureFailure::TAIL_MISMATCH:
      return "TAIL_MISMATCH";
    case EspUartFifoRxBackpressureFailure::RECOVERY:
      return "RECOVERY";
  }
  return "UNKNOWN";
}

}  // namespace LibXRTest
