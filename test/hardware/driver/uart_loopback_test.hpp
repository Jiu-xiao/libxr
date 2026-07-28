#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "libxr_def.hpp"
#include "libxr_type.hpp"
#include "uart.hpp"

namespace LibXRTest
{

enum class UartLoopbackFailure : uint8_t
{
  NONE,
  INVALID_ARGUMENT,
  CLEAR_RX,
  SET_CONFIG,
  WRITE_SUBMIT,
  WRITE_COMPLETE,
  READ,
  DATA_MISMATCH,
  UNEXPECTED_RX_DATA,
};

struct UartLoopbackTestCase
{
  LibXR::UART::Configuration uart_config{};
  size_t frame_size = 0U;
  uint32_t rounds = 0U;
  uint32_t operation_timeout_ms = 0U;
  uint32_t rx_quiet_time_ms = 1U;
  uint32_t pattern_seed = 0U;
  uint8_t batch_depth = 1U;
};

struct UartLoopbackTestResult
{
  UartLoopbackFailure failure = UartLoopbackFailure::NONE;
  LibXR::ErrorCode error = LibXR::ErrorCode::OK;
  uint32_t completed_rounds = 0U;
  uint32_t failed_round = UINT32_MAX;
  uint8_t failed_batch = UINT8_MAX;
  size_t verified_bytes = 0U;
  size_t mismatch_offset = SIZE_MAX;
  size_t unexpected_rx_bytes = 0U;
  uint64_t elapsed_us = 0U;

  [[nodiscard]] bool Passed() const { return failure == UartLoopbackFailure::NONE; }
};

/**
 * @brief Run one deterministic physical UART loopback case.
 *
 * `batch_depth == 1` uses blocking write/read operations. `batch_depth == 2`
 * submits two writes before waiting for their echoed bytes, which exercises the
 * driver's active/pending TX path. RX is cleared once before the first round; all
 * rounds then form one continuous stream so duplicate bytes cannot be hidden at a
 * round boundary. After the final read, the function observes the RX queue for
 * `rx_quiet_time_ms` and rejects any trailing byte.
 *
 * The caller owns both scratch buffers. Each must contain at least
 * `frame_size * batch_depth` bytes, and both UART byte queues must have at least that
 * much free space before the case starts. The function does not allocate payload
 * buffers and stops at the first failure so a timeout cannot corrupt later stream
 * boundaries.
 *
 * Call only from task or main context after `PlatformInit()` and timebase setup. The
 * UART must not receive unrelated traffic while the test runs.
 */
UartLoopbackTestResult RunUartLoopbackTest(LibXR::UART& uart,
                                           const UartLoopbackTestCase& test_case,
                                           LibXR::RawData tx_scratch,
                                           LibXR::RawData rx_scratch);

struct UartIdleReconfigureTestCase
{
  std::span<const LibXR::UART::Configuration> configurations{};
  uint32_t transitions = 0U;
  uint32_t transition_timeout_ms = 0U;
  uint32_t retry_interval_ms = 1U;
};

struct UartIdleReconfigureTestResult
{
  LibXR::ErrorCode error = LibXR::ErrorCode::OK;
  uint32_t completed_transitions = 0U;
  uint32_t busy_retries = 0U;
  uint32_t failed_transition = UINT32_MAX;
  uint64_t elapsed_us = 0U;

  [[nodiscard]] bool Passed() const
  {
    return error == LibXR::ErrorCode::OK && failed_transition == UINT32_MAX;
  }
};

/**
 * @brief Verify that serialized CONFIG requests complete without TX traffic.
 *
 * A request may temporarily return `BUSY` while the preceding accepted CONFIG is
 * still completing. The function retries the same payload until it is accepted or
 * its per-transition timeout expires. No write is issued between transitions.
 * Call only from task or main context after `PlatformInit()` and timebase setup.
 */
UartIdleReconfigureTestResult RunUartIdleReconfigureTest(
    LibXR::UART& uart, const UartIdleReconfigureTestCase& test_case);

const char* UartLoopbackFailureName(UartLoopbackFailure failure);

}  // namespace LibXRTest
