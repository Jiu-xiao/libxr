#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

#include "libxr_def.hpp"
#include "libxr_type.hpp"
#include "uart.hpp"

namespace LibXRTest
{

/**
 * @brief Shared lifecycle bits for one concurrent UART stress phase.
 *
 * The board runner owns task creation and calls one worker method from each task.
 * Both workers publish READY, the coordinator publishes START and STOP, and each
 * worker publishes DONE immediately before it returns. The test object, its TX scratch
 * storage, and the backing storage referenced by the configurations span must outlive
 * both worker calls, including a stop-timeout result.
 */
enum class UartConcurrentConfigStressState : uint32_t
{
  NONE = 0U,
  WRITER_READY = 1U << 0U,
  CONFIGURATOR_READY = 1U << 1U,
  START = 1U << 2U,
  STOP = 1U << 3U,
  WRITER_DONE = 1U << 4U,
  CONFIGURATOR_DONE = 1U << 5U,
};

enum class UartConcurrentConfigStressFailure : uint8_t
{
  NONE,
  INVALID_ARGUMENT,
  EXTERNAL_ABORT,
  WORKER_READY_TIMEOUT,
  WRITE_RESULT,
  CONFIG_RESULT,
  WRITE_CALL_TIMEOUT,
  CONFIG_CALL_TIMEOUT,
  WRITE_ATTEMPT_LIMIT,
  CONFIG_ATTEMPT_LIMIT,
  BOUNDARY_COVERAGE,
  BATCH_DEPTH_COVERAGE,
  RX_CLEAR,
  WRITE_PROGRESS_TIMEOUT,
  CONFIG_PROGRESS_TIMEOUT,
  RX_PROGRESS_TIMEOUT,
  WORKER_STOP_TIMEOUT,
  INSUFFICIENT_WRITE_PROGRESS,
  INSUFFICIENT_CONFIG_PROGRESS,
  INSUFFICIENT_RX_PROGRESS,
};

struct UartConcurrentConfigStressCounters
{
  uint32_t write_attempts = 0U;
  uint32_t accepted_writes = 0U;
  uint32_t accepted_write_bytes = 0U;
  uint32_t write_busy_retries = 0U;
  uint32_t write_full_retries = 0U;
  uint32_t max_write_call_us = 0U;
  // Bits 0..8 correspond to 31/32/33, 511/512/513, and 1023/1024/1025.
  uint32_t required_boundary_length_mask = 0U;
  // Bit 0 is one accepted record; bit 1 is one complete two-record batch.
  uint32_t completed_batch_depth_mask = 0U;
  uint32_t config_attempts = 0U;
  uint32_t accepted_configurations = 0U;
  uint32_t config_busy_retries = 0U;
  uint32_t max_config_call_us = 0U;
  uint32_t observed_rx_bytes = 0U;
};

/**
 * @brief Parameters for one Write-vs-CONFIG concurrent stress phase.
 *
 * The write worker first deterministically covers 31/32/33, 511/512/513, and
 * 1023/1024/1025, then uses a mixture: most lengths are uniformly sampled from
 * `[1, max_write_size]`; a smaller deterministic share samples queue and DMA
 * boundaries. The first two complete batches have depths one and two; later batches
 * choose either depth uniformly.
 * `tx_scratch` supplied to the test constructor must provide two `max_write_size`
 * slots because a batch of two can be accepted concurrently.
 */
struct UartConcurrentConfigStressCase
{
  std::span<const LibXR::UART::Configuration> configurations{};
  size_t max_write_size = 0U;
  uint32_t duration_ms = 0U;
  uint32_t worker_ready_timeout_ms = 0U;
  uint32_t worker_stop_timeout_ms = 0U;
  uint32_t api_call_timeout_ms = 0U;
  uint32_t progress_timeout_ms = 0U;
  uint32_t retry_interval_ms = 0U;
  uint32_t rx_drain_interval_ms = 0U;
  uint32_t config_burst_size = 0U;
  uint32_t config_fairness_quiet_ms = 0U;
  uint32_t max_write_attempts = 0U;
  uint32_t max_config_attempts = 0U;
  uint32_t min_accepted_writes = 0U;
  uint32_t min_accepted_configurations = 0U;
  uint32_t min_observed_rx_bytes = 0U;
  uint32_t write_seed = 0U;
  uint32_t config_seed = 0U;
};

struct UartConcurrentConfigStressResult
{
  UartConcurrentConfigStressFailure failure = UartConcurrentConfigStressFailure::NONE;
  LibXR::ErrorCode error = LibXR::ErrorCode::OK;
  UartConcurrentConfigStressCounters counters{};
  uint32_t final_state = 0U;
  uint64_t elapsed_us = 0U;

  [[nodiscard]] bool Passed() const
  {
    return failure == UartConcurrentConfigStressFailure::NONE;
  }
};

/**
 * @brief Scheduler-neutral Write-vs-CONFIG stress phase.
 *
 * Construct one instance per phase. A board runner creates two ordinary task/thread
 * contexts and calls `RunWriteWorker()` and `RunConfigWorker()` once. The coordinator
 * context calls `RunCoordinator()` once; it is the sole RX consumer and uses only
 * `ClearQueuedData()` during the stress interval. No worker method is ISR-safe.
 *
 * Accepted writes use default `WriteOperation{}`. The WritePort owns the copied queue
 * record after `Write()` returns `OK`, so a worker may reuse its slot. `BUSY` and
 * `FULL` leave that record unaccepted; the worker retries the same bytes and length.
 * CONFIG similarly retries the same configuration after `BUSY` and only chooses a new
 * one after `OK` admission.
 *
 * This phase proves bounded API progress and lifecycle behavior only. CONFIG may
 * restart a partial active transfer and RX transition windows may discard bytes, so it
 * deliberately does not compare the stress byte stream. Use
 * `RetireUartStressTraffic()` before a strict loopback case.
 */
class UartConcurrentConfigStressTest
{
 public:
  UartConcurrentConfigStressTest(LibXR::UART& uart,
                                 const UartConcurrentConfigStressCase& test_case,
                                 LibXR::RawData tx_scratch);

  UartConcurrentConfigStressTest(const UartConcurrentConfigStressTest&) = delete;
  UartConcurrentConfigStressTest& operator=(const UartConcurrentConfigStressTest&) =
      delete;

  /** @brief Run exactly once from the task that submits randomized Write records. */
  void RunWriteWorker();

  /** @brief Run exactly once from the task that submits CONFIG requests. */
  void RunConfigWorker();

  /**
   * @brief Synchronize and run the phase from its coordinator context.
   *
   * It waits for both READY bits, publishes START, repeatedly clears queued RX bytes,
   * monitors independent Write/CONFIG/RX progress, publishes STOP, then waits for both
   * DONE bits. A stop timeout does not make it safe to destroy the instance; the board
   * runner must retain its context until the worker tasks have actually returned.
   */
  UartConcurrentConfigStressResult RunCoordinator();

  /** @brief Request a cooperative stop after an external task-launch failure. */
  void Abort(LibXR::ErrorCode error = LibXR::ErrorCode::FAILED);

  [[nodiscard]] uint32_t State() const;

 private:
  struct AtomicCounters
  {
    std::atomic<uint32_t> write_attempts{0U};
    std::atomic<uint32_t> accepted_writes{0U};
    std::atomic<uint32_t> accepted_write_bytes{0U};
    std::atomic<uint32_t> write_busy_retries{0U};
    std::atomic<uint32_t> write_full_retries{0U};
    std::atomic<uint32_t> max_write_call_us{0U};
    std::atomic<uint32_t> required_boundary_length_mask{0U};
    std::atomic<uint32_t> completed_batch_depth_mask{0U};
    std::atomic<uint32_t> config_attempts{0U};
    std::atomic<uint32_t> accepted_configurations{0U};
    std::atomic<uint32_t> config_busy_retries{0U};
    std::atomic<uint32_t> max_config_call_us{0U};
    std::atomic<uint32_t> observed_rx_bytes{0U};
  };

  [[nodiscard]] bool ArgumentsValid() const;
  [[nodiscard]] bool StopRequested() const;
  void PublishFailure(UartConcurrentConfigStressFailure failure, LibXR::ErrorCode error);
  void PublishState(UartConcurrentConfigStressState state);
  void RequestStop();
  void WaitRetry(uint32_t milliseconds) const;
  void WaitForStart();
  [[nodiscard]] bool WaitForState(uint32_t required_state, uint32_t timeout_ms) const;
  [[nodiscard]] UartConcurrentConfigStressResult Snapshot(uint64_t elapsed_us) const;

  LibXR::UART& uart_;
  UartConcurrentConfigStressCase test_case_;
  LibXR::RawData tx_scratch_;
  std::atomic<uint32_t> state_{0U};
  // The packed failure/error value gives readers one coherent terminal snapshot.
  std::atomic<uint32_t> failure_info_{0U};
  std::atomic<uint32_t> coordinator_started_{0U};
  AtomicCounters counters_{};
};

const char* UartConcurrentConfigStressFailureName(
    UartConcurrentConfigStressFailure failure);

enum class UartStressTrafficBarrierFailure : uint8_t
{
  NONE,
  INVALID_ARGUMENT,
  CLEAR_RX,
  FINAL_CONFIG,
  MARKER_WRITE,
  MARKER_READ_TIMEOUT,
  MARKER_READ,
  MARKER_TRAILING_DATA,
};

/**
 * @brief Configuration and timeout parameters for retiring concurrent stress traffic.
 *
 * The final CONFIG is retried until admitted, then a unique zero/magic/zero marker is
 * queued after all earlier writes. The marker is recognized across arbitrary read
 * chunks. Stress payloads are nonzero by contract, making the zero guards a practical
 * delimiter; this is a hardware-test synchronization barrier, not a framing protocol.
 */
struct UartStressTrafficBarrierCase
{
  LibXR::UART::Configuration final_config{};
  uint32_t config_timeout_ms = 0U;
  uint32_t marker_timeout_ms = 0U;
  uint32_t retry_interval_ms = 0U;
  uint32_t rx_quiet_time_ms = 0U;
};

struct UartStressTrafficBarrierResult
{
  UartStressTrafficBarrierFailure failure = UartStressTrafficBarrierFailure::NONE;
  LibXR::ErrorCode error = LibXR::ErrorCode::OK;
  uint32_t config_busy_retries = 0U;
  uint32_t marker_busy_retries = 0U;
  uint32_t marker_full_retries = 0U;
  size_t discarded_prefix_bytes = 0U;
  size_t unexpected_trailing_bytes = 0U;
  uint64_t elapsed_us = 0U;

  [[nodiscard]] bool Passed() const
  {
    return failure == UartStressTrafficBarrierFailure::NONE;
  }
};

/**
 * @brief Retire all stress traffic before a strict UART loopback case.
 *
 * Call only after both stress workers have reported DONE and while no other code calls
 * `Read()` or `ClearQueuedData()`. The function first drops the current RX snapshot,
 * waits until `final_config` is admitted, submits a static marker with a default
 * `WriteOperation{}`, discards all bytes before the marker, and rejects any byte seen
 * during the subsequent quiet window. The caller may run an exact loopback test only
 * after this function returns success.
 */
UartStressTrafficBarrierResult RetireUartStressTraffic(
    LibXR::UART& uart, const UartStressTrafficBarrierCase& test_case,
    LibXR::RawData rx_scratch);

const char* UartStressTrafficBarrierFailureName(UartStressTrafficBarrierFailure failure);

}  // namespace LibXRTest
