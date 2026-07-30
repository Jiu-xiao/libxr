#pragma once

#include <cstddef>
#include <cstdint>

#include "driver/uart_concurrency_stress_test.hpp"
#include "driver/uart_loopback_test.hpp"
#include "esp_uart_fifo.hpp"

namespace LibXRTest
{

enum class EspUartFifoPartialConfigFailure : uint8_t
{
  NONE,
  INVALID_ARGUMENT,
  PRECONDITION,
  OBSERVER_SETUP,
  WRITE_SUBMIT,
  NOT_PARTIALLY_LOADED,
  CONFIG_ADMISSION,
  OVERLAPPING_CONFIG,
  CALLBACK_TIMEOUT,
  CALLBACK_STATUS,
  CALLBACK_COUNT,
  OBSERVER_READ,
  OBSERVER_MISMATCH,
  OBSERVER_TRAILING,
  OBSERVER_TEARDOWN,
  RETIREMENT,
  RECOVERY,
};

struct EspUartFifoPartialConfigCase
{
  uart_port_t observer_uart_num = UART_NUM_1;
  int observer_rx_pin = -1;
  LibXR::UART::Configuration initial_config{};
  LibXR::UART::Configuration requested_config{};
  LibXR::UART::Configuration overlapping_config{};
  LibXR::UART::Configuration final_config{};
  size_t frame_size = 0U;
  uint32_t operation_timeout_ms = 0U;
  uint32_t callback_quiet_time_ms = 0U;
  uint32_t rx_quiet_time_ms = 0U;
  uint32_t pattern_seed = 0U;
};

struct EspUartFifoPartialConfigResult
{
  EspUartFifoPartialConfigFailure failure = EspUartFifoPartialConfigFailure::NONE;
  LibXR::ErrorCode error = LibXR::ErrorCode::OK;
  size_t queued_after_submit = 0U;
  uint32_t callback_count = 0U;
  LibXR::ErrorCode callback_error = LibXR::ErrorCode::OK;
  LibXR::ErrorCode first_config_result = LibXR::ErrorCode::OK;
  LibXR::ErrorCode overlapping_config_result = LibXR::ErrorCode::OK;
  size_t observer_bytes = 0U;
  size_t observer_mismatch_offset = 0U;
  size_t observer_unexpected_bytes = 0U;
  UartStressTrafficBarrierResult retirement{};
  UartLoopbackTestResult recovery{};
  uint64_t elapsed_us = 0U;

  [[nodiscard]] bool Passed() const
  {
    return failure == EspUartFifoPartialConfigFailure::NONE;
  }
};

EspUartFifoPartialConfigResult RunEspUartFifoPartialConfigTest(
    LibXR::ESP32UartFifo& uart, const EspUartFifoPartialConfigCase& test_case,
    LibXR::RawData tx_scratch, LibXR::RawData rx_scratch);

const char* EspUartFifoPartialConfigFailureName(EspUartFifoPartialConfigFailure failure);

enum class EspUartFifoRxBackpressureFailure : uint8_t
{
  NONE,
  INVALID_ARGUMENT,
  PRECONDITION,
  WRITE,
  RX_QUEUE_TIMEOUT,
  RX_INTERRUPT_NOT_MASKED,
  FIFO_TAIL_TIMEOUT,
  RX_OVERFLOW,
  QUEUE_READ,
  PREFIX_MISMATCH,
  RX_RESUME_TIMEOUT,
  RX_INTERRUPT_NOT_ENABLED,
  TAIL_READ,
  TAIL_MISMATCH,
  RECOVERY,
};

struct EspUartFifoRxBackpressureCase
{
  uart_port_t uart_num = UART_NUM_0;
  LibXR::UART::Configuration config{};
  LibXR::UART::Configuration recovery_config{};
  size_t rx_queue_size = 0U;
  size_t retained_fifo_bytes = 0U;
  size_t write_chunk_size = 0U;
  uint32_t operation_timeout_ms = 0U;
  uint32_t rx_quiet_time_ms = 0U;
  uint32_t pattern_seed = 0U;
};

struct EspUartFifoRxBackpressureResult
{
  EspUartFifoRxBackpressureFailure failure = EspUartFifoRxBackpressureFailure::NONE;
  LibXR::ErrorCode error = LibXR::ErrorCode::OK;
  size_t queued_at_saturation = 0U;
  size_t fifo_tail_bytes = 0U;
  uint32_t rx_interrupts_when_full = 0U;
  uint32_t rx_interrupts_after_resume = 0U;
  uint32_t raw_status_at_saturation = 0U;
  size_t verified_bytes = 0U;
  UartStressTrafficBarrierResult precondition{};
  UartLoopbackTestResult recovery{};
  uint64_t elapsed_us = 0U;

  [[nodiscard]] bool Passed() const
  {
    return failure == EspUartFifoRxBackpressureFailure::NONE;
  }
};

EspUartFifoRxBackpressureResult RunEspUartFifoRxBackpressureTest(
    LibXR::ESP32UartFifo& uart, const EspUartFifoRxBackpressureCase& test_case,
    LibXR::RawData tx_scratch, LibXR::RawData rx_scratch);

const char* EspUartFifoRxBackpressureFailureName(
    EspUartFifoRxBackpressureFailure failure);

}  // namespace LibXRTest
