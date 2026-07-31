#include "driver/uart_concurrency_stress_test.hpp"

#include <algorithm>
#include <array>
#include <limits>

#include "libxr.hpp"

namespace LibXRTest
{
namespace
{

constexpr uint32_t StateValue(UartConcurrentConfigStressState state)
{
  return static_cast<uint32_t>(state);
}

constexpr uint32_t kWorkersReady =
    StateValue(UartConcurrentConfigStressState::WRITER_READY) |
    StateValue(UartConcurrentConfigStressState::CONFIGURATOR_READY);
constexpr uint32_t kWorkersDone =
    StateValue(UartConcurrentConfigStressState::WRITER_DONE) |
    StateValue(UartConcurrentConfigStressState::CONFIGURATOR_DONE);

constexpr uint32_t EncodeFailure(UartConcurrentConfigStressFailure failure,
                                 LibXR::ErrorCode error)
{
  return (static_cast<uint32_t>(failure) << 8U) |
         static_cast<uint8_t>(static_cast<int8_t>(error));
}

constexpr UartConcurrentConfigStressFailure DecodeFailure(uint32_t value)
{
  return static_cast<UartConcurrentConfigStressFailure>(value >> 8U);
}

constexpr LibXR::ErrorCode DecodeError(uint32_t value)
{
  return static_cast<LibXR::ErrorCode>(static_cast<int8_t>(value & 0xFFU));
}

uint32_t NowMilliseconds()
{
  return static_cast<uint32_t>(LibXR::Timebase::GetMilliseconds());
}

uint64_t ElapsedMicroseconds(uint64_t start_us)
{
  return static_cast<uint64_t>(LibXR::Timebase::GetMicroseconds()) - start_us;
}

bool TimedOut(uint32_t start_ms, uint32_t timeout_ms)
{
  return static_cast<uint32_t>(NowMilliseconds() - start_ms) >= timeout_ms;
}

uint32_t NextRandom(uint32_t& state)
{
  if (state == 0U)
  {
    state = 0x6D2B79F5U;
  }
  state ^= state << 13U;
  state ^= state >> 17U;
  state ^= state << 5U;
  return state;
}

uint32_t UniformBelow(uint32_t& state, uint32_t upper_bound)
{
  ASSERT(upper_bound != 0U);

  // Dropping the short low range makes modulo selection exactly uniform over
  // the full uint32_t random domain.
  const uint32_t threshold = static_cast<uint32_t>(-upper_bound) % upper_bound;
  uint32_t value = 0U;
  do
  {
    value = NextRandom(state);
  } while (value < threshold);
  return value % upper_bound;
}

constexpr std::array<size_t, 28U> kBoundaryLengths = {
    1U,    2U,    3U,    30U,   31U,   32U,   33U,   34U,   63U,  64U,
    65U,   127U,  128U,  129U,  255U,  256U,  257U,  511U,  512U, 513U,
    1023U, 1024U, 1025U, 2047U, 2048U, 2049U, 4095U, 4096U,
};

constexpr std::array<size_t, 9U> kRequiredBoundaryLengths = {
    31U, 32U, 33U, 511U, 512U, 513U, 1023U, 1024U, 1025U,
};
constexpr uint32_t kRequiredBoundaryLengthMask =
    (1U << kRequiredBoundaryLengths.size()) - 1U;
constexpr uint32_t kRequiredBatchDepthMask = (1U << 0U) | (1U << 1U);

size_t SelectWriteLength(uint32_t& state, size_t maximum, uint32_t& coverage_cursor)
{
  if (coverage_cursor < kRequiredBoundaryLengths.size())
  {
    return kRequiredBoundaryLengths[coverage_cursor++];
  }

  // Keep a deterministic 1/8 boundary share while the remaining selections are
  // uniformly distributed over every representable test length.
  if (UniformBelow(state, 8U) == 0U)
  {
    size_t boundary_count = 0U;
    while (boundary_count < kBoundaryLengths.size() &&
           kBoundaryLengths[boundary_count] <= maximum)
    {
      ++boundary_count;
    }
    if (boundary_count != 0U)
    {
      return kBoundaryLengths[UniformBelow(state, static_cast<uint32_t>(boundary_count))];
    }
  }
  return static_cast<size_t>(UniformBelow(state, static_cast<uint32_t>(maximum))) + 1U;
}

uint8_t SelectBatchDepth(uint32_t& state, uint32_t& coverage_cursor)
{
  if (coverage_cursor < 2U)
  {
    return static_cast<uint8_t>(++coverage_cursor);
  }
  return static_cast<uint8_t>(UniformBelow(state, 2U) + 1U);
}

uint32_t RequiredBoundaryLengthBit(size_t length)
{
  for (size_t index = 0U; index < kRequiredBoundaryLengths.size(); ++index)
  {
    if (length == kRequiredBoundaryLengths[index])
    {
      return 1U << index;
    }
  }
  return 0U;
}

void FillNonzeroPayload(uint8_t* output, size_t size, uint32_t& state)
{
  for (size_t index = 0U; index < size; ++index)
  {
    output[index] = static_cast<uint8_t>(UniformBelow(state, 255U) + 1U);
  }
}

bool TryTakeAttempt(std::atomic<uint32_t>& counter, uint32_t limit)
{
  uint32_t current = counter.load(std::memory_order_relaxed);
  while (current < limit)
  {
    if (counter.compare_exchange_weak(current, current + 1U, std::memory_order_relaxed,
                                      std::memory_order_relaxed))
    {
      return true;
    }
  }
  return false;
}

void SaturatingAdd(std::atomic<uint32_t>& counter, uint32_t amount)
{
  uint32_t current = counter.load(std::memory_order_relaxed);
  while (true)
  {
    const uint32_t next = current > std::numeric_limits<uint32_t>::max() - amount
                              ? std::numeric_limits<uint32_t>::max()
                              : current + amount;
    if (counter.compare_exchange_weak(current, next, std::memory_order_relaxed,
                                      std::memory_order_relaxed))
    {
      return;
    }
  }
}

void AtomicMax(std::atomic<uint32_t>& counter, uint32_t value)
{
  uint32_t current = counter.load(std::memory_order_relaxed);
  while (current < value &&
         !counter.compare_exchange_weak(current, value, std::memory_order_relaxed,
                                        std::memory_order_relaxed))
  {
  }
}

void SetBarrierFailure(UartStressTrafficBarrierResult& result,
                       UartStressTrafficBarrierFailure failure, LibXR::ErrorCode error)
{
  if (result.failure == UartStressTrafficBarrierFailure::NONE)
  {
    result.failure = failure;
    result.error = error;
  }
}

constexpr size_t kMarkerGuardSize = 32U;
constexpr std::array<uint8_t, 16U> kMarkerMagic = {
    0xB7U, 0x49U, 0x58U, 0xD3U, 0x61U, 0xAFU, 0x2EU, 0x95U,
    0x74U, 0xC8U, 0x3BU, 0xE1U, 0x56U, 0x9DU, 0x27U, 0xF4U,
};
constexpr size_t kMarkerSize = kMarkerGuardSize + kMarkerMagic.size() + kMarkerGuardSize;

constexpr std::array<uint8_t, kMarkerSize> MakeMarker()
{
  std::array<uint8_t, kMarkerSize> marker{};
  for (size_t index = 0U; index < kMarkerMagic.size(); ++index)
  {
    marker[kMarkerGuardSize + index] = kMarkerMagic[index];
  }
  return marker;
}

constexpr std::array<size_t, kMarkerSize> MakeMarkerPrefixTable(
    const std::array<uint8_t, kMarkerSize>& marker)
{
  std::array<size_t, kMarkerSize> prefix{};
  size_t matched = 0U;
  for (size_t index = 1U; index < marker.size(); ++index)
  {
    while (matched > 0U && marker[index] != marker[matched])
    {
      matched = prefix[matched - 1U];
    }
    if (marker[index] == marker[matched])
    {
      ++matched;
    }
    prefix[index] = matched;
  }
  return prefix;
}

constexpr auto kMarker = MakeMarker();
constexpr auto kMarkerPrefixTable = MakeMarkerPrefixTable(kMarker);

class MarkerScanner
{
 public:
  [[nodiscard]] bool Consume(uint8_t byte)
  {
    while (matched_ > 0U && byte != kMarker[matched_])
    {
      matched_ = kMarkerPrefixTable[matched_ - 1U];
    }
    if (byte == kMarker[matched_])
    {
      ++matched_;
    }
    if (matched_ != kMarker.size())
    {
      return false;
    }

    matched_ = kMarkerPrefixTable[matched_ - 1U];
    return true;
  }

 private:
  size_t matched_ = 0U;
};

}  // namespace

UartConcurrentConfigStressTest::UartConcurrentConfigStressTest(
    LibXR::UART& uart, const UartConcurrentConfigStressCase& test_case,
    LibXR::RawData tx_scratch)
    : uart_(uart), test_case_(test_case), tx_scratch_(tx_scratch)
{
}

bool UartConcurrentConfigStressTest::ArgumentsValid() const
{
  if (uart_.read_port_ == nullptr || uart_.write_port_ == nullptr ||
      !uart_.read_port_->Readable() || !uart_.write_port_->Writable() ||
      test_case_.configurations.empty() ||
      test_case_.configurations.size() > std::numeric_limits<uint32_t>::max() ||
      test_case_.max_write_size < kRequiredBoundaryLengths.back() ||
      test_case_.max_write_size > std::numeric_limits<uint32_t>::max() ||
      tx_scratch_.addr_ == nullptr ||
      test_case_.max_write_size > tx_scratch_.size_ / 2U ||
      test_case_.duration_ms == 0U || test_case_.worker_ready_timeout_ms == 0U ||
      test_case_.worker_stop_timeout_ms == 0U || test_case_.api_call_timeout_ms == 0U ||
      test_case_.progress_timeout_ms == 0U || test_case_.rx_drain_interval_ms == 0U ||
      test_case_.config_burst_size == 0U || test_case_.config_fairness_quiet_ms == 0U ||
      test_case_.max_write_attempts == 0U || test_case_.max_config_attempts == 0U ||
      test_case_.min_accepted_writes > test_case_.max_write_attempts ||
      test_case_.min_accepted_configurations > test_case_.max_config_attempts ||
      test_case_.max_write_attempts >
          std::numeric_limits<uint32_t>::max() /
              static_cast<uint32_t>(test_case_.max_write_size))
  {
    return false;
  }
  return true;
}

bool UartConcurrentConfigStressTest::StopRequested() const
{
  return (state_.load(std::memory_order_acquire) &
          StateValue(UartConcurrentConfigStressState::STOP)) != 0U;
}

void UartConcurrentConfigStressTest::PublishFailure(
    UartConcurrentConfigStressFailure failure, LibXR::ErrorCode error)
{
  uint32_t expected =
      EncodeFailure(UartConcurrentConfigStressFailure::NONE, LibXR::ErrorCode::OK);
  const uint32_t desired = EncodeFailure(failure, error);
  (void)failure_info_.compare_exchange_strong(
      expected, desired, std::memory_order_acq_rel, std::memory_order_acquire);
  RequestStop();
}

void UartConcurrentConfigStressTest::PublishState(UartConcurrentConfigStressState state)
{
  state_.fetch_or(StateValue(state), std::memory_order_release);
}

void UartConcurrentConfigStressTest::RequestStop()
{
  PublishState(UartConcurrentConfigStressState::START);
  PublishState(UartConcurrentConfigStressState::STOP);
}

void UartConcurrentConfigStressTest::WaitRetry(uint32_t milliseconds) const
{
  if (milliseconds == 0U)
  {
    LibXR::Thread::Yield();
    return;
  }

  const uint32_t start_ms = NowMilliseconds();
  while (!StopRequested() && !TimedOut(start_ms, milliseconds))
  {
    LibXR::Thread::Sleep(1U);
  }
}

void UartConcurrentConfigStressTest::WaitForStart()
{
  while ((state_.load(std::memory_order_acquire) &
          StateValue(UartConcurrentConfigStressState::START)) == 0U)
  {
    if (StopRequested())
    {
      return;
    }
    LibXR::Thread::Yield();
  }
}

bool UartConcurrentConfigStressTest::WaitForState(uint32_t required_state,
                                                  uint32_t timeout_ms) const
{
  const uint32_t start_ms = NowMilliseconds();
  while ((state_.load(std::memory_order_acquire) & required_state) != required_state)
  {
    if (TimedOut(start_ms, timeout_ms))
    {
      return false;
    }
    LibXR::Thread::Sleep(1U);
  }
  return true;
}

void UartConcurrentConfigStressTest::RunWriteWorker()
{
  PublishState(UartConcurrentConfigStressState::WRITER_READY);

  if (!ArgumentsValid())
  {
    PublishFailure(UartConcurrentConfigStressFailure::INVALID_ARGUMENT,
                   LibXR::ErrorCode::ARG_ERR);
    PublishState(UartConcurrentConfigStressState::WRITER_DONE);
    return;
  }

  WaitForStart();
  uint32_t random_state = test_case_.write_seed;
  uint32_t boundary_length_cursor = 0U;
  uint32_t batch_depth_cursor = 0U;
  auto* slots = static_cast<uint8_t*>(tx_scratch_.addr_);

  while (!StopRequested())
  {
    const uint8_t batch_depth = SelectBatchDepth(random_state, batch_depth_cursor);
    uint8_t accepted_records = 0U;
    for (uint8_t batch_index = 0U; batch_index < batch_depth && !StopRequested();
         ++batch_index)
    {
      const size_t length = SelectWriteLength(random_state, test_case_.max_write_size,
                                              boundary_length_cursor);
      auto* payload =
          slots + static_cast<size_t>(batch_index) * test_case_.max_write_size;
      FillNonzeroPayload(payload, length, random_state);

      bool accepted = false;
      while (!accepted && !StopRequested())
      {
        if (!TryTakeAttempt(counters_.write_attempts, test_case_.max_write_attempts))
        {
          PublishFailure(UartConcurrentConfigStressFailure::WRITE_ATTEMPT_LIMIT,
                         LibXR::ErrorCode::TIMEOUT);
          break;
        }

        LibXR::WriteOperation operation{};
        const uint64_t call_start_us =
            static_cast<uint64_t>(LibXR::Timebase::GetMicroseconds());
        const auto answer = uart_.Write({payload, length}, operation, false);
        const uint64_t call_elapsed_us = ElapsedMicroseconds(call_start_us);
        const uint32_t bounded_call_elapsed_us =
            call_elapsed_us > std::numeric_limits<uint32_t>::max()
                ? std::numeric_limits<uint32_t>::max()
                : static_cast<uint32_t>(call_elapsed_us);
        AtomicMax(counters_.max_write_call_us, bounded_call_elapsed_us);
        if (call_elapsed_us >
            static_cast<uint64_t>(test_case_.api_call_timeout_ms) * 1000U)
        {
          PublishFailure(UartConcurrentConfigStressFailure::WRITE_CALL_TIMEOUT,
                         LibXR::ErrorCode::TIMEOUT);
          break;
        }
        if (answer == LibXR::ErrorCode::OK)
        {
          counters_.accepted_writes.fetch_add(1U, std::memory_order_relaxed);
          SaturatingAdd(counters_.accepted_write_bytes, static_cast<uint32_t>(length));
          counters_.required_boundary_length_mask.fetch_or(
              RequiredBoundaryLengthBit(length), std::memory_order_relaxed);
          ++accepted_records;
          accepted = true;
          continue;
        }
        if (answer == LibXR::ErrorCode::BUSY)
        {
          counters_.write_busy_retries.fetch_add(1U, std::memory_order_relaxed);
          WaitRetry(test_case_.retry_interval_ms);
          continue;
        }
        if (answer == LibXR::ErrorCode::FULL)
        {
          counters_.write_full_retries.fetch_add(1U, std::memory_order_relaxed);
          WaitRetry(test_case_.retry_interval_ms);
          continue;
        }

        PublishFailure(UartConcurrentConfigStressFailure::WRITE_RESULT, answer);
      }
    }
    if (accepted_records == batch_depth)
    {
      counters_.completed_batch_depth_mask.fetch_or(1U << (batch_depth - 1U),
                                                    std::memory_order_relaxed);
    }
    LibXR::Thread::Yield();
  }

  PublishState(UartConcurrentConfigStressState::WRITER_DONE);
}

void UartConcurrentConfigStressTest::RunConfigWorker()
{
  PublishState(UartConcurrentConfigStressState::CONFIGURATOR_READY);

  if (!ArgumentsValid())
  {
    PublishFailure(UartConcurrentConfigStressFailure::INVALID_ARGUMENT,
                   LibXR::ErrorCode::ARG_ERR);
    PublishState(UartConcurrentConfigStressState::CONFIGURATOR_DONE);
    return;
  }

  WaitForStart();
  uint32_t random_state = test_case_.config_seed;
  auto configuration = test_case_.configurations[UniformBelow(
      random_state, static_cast<uint32_t>(test_case_.configurations.size()))];
  uint32_t accepted_in_burst = 0U;

  while (!StopRequested())
  {
    if (!TryTakeAttempt(counters_.config_attempts, test_case_.max_config_attempts))
    {
      PublishFailure(UartConcurrentConfigStressFailure::CONFIG_ATTEMPT_LIMIT,
                     LibXR::ErrorCode::TIMEOUT);
      break;
    }

    const uint64_t call_start_us =
        static_cast<uint64_t>(LibXR::Timebase::GetMicroseconds());
    const auto answer = uart_.SetConfig(configuration);
    const uint64_t call_elapsed_us = ElapsedMicroseconds(call_start_us);
    const uint32_t bounded_call_elapsed_us =
        call_elapsed_us > std::numeric_limits<uint32_t>::max()
            ? std::numeric_limits<uint32_t>::max()
            : static_cast<uint32_t>(call_elapsed_us);
    AtomicMax(counters_.max_config_call_us, bounded_call_elapsed_us);
    if (call_elapsed_us > static_cast<uint64_t>(test_case_.api_call_timeout_ms) * 1000U)
    {
      PublishFailure(UartConcurrentConfigStressFailure::CONFIG_CALL_TIMEOUT,
                     LibXR::ErrorCode::TIMEOUT);
      break;
    }
    if (answer == LibXR::ErrorCode::OK)
    {
      counters_.accepted_configurations.fetch_add(1U, std::memory_order_relaxed);
      ++accepted_in_burst;
      if (accepted_in_burst >= test_case_.config_burst_size)
      {
        accepted_in_burst = 0U;
        WaitRetry(test_case_.config_fairness_quiet_ms);
      }
      configuration = test_case_.configurations[UniformBelow(
          random_state, static_cast<uint32_t>(test_case_.configurations.size()))];
      continue;
    }
    if (answer == LibXR::ErrorCode::BUSY)
    {
      counters_.config_busy_retries.fetch_add(1U, std::memory_order_relaxed);
      WaitRetry(test_case_.retry_interval_ms);
      continue;
    }

    PublishFailure(UartConcurrentConfigStressFailure::CONFIG_RESULT, answer);
  }

  PublishState(UartConcurrentConfigStressState::CONFIGURATOR_DONE);
}

UartConcurrentConfigStressResult UartConcurrentConfigStressTest::RunCoordinator()
{
  const uint64_t start_us = static_cast<uint64_t>(LibXR::Timebase::GetMicroseconds());
  if (coordinator_started_.exchange(1U, std::memory_order_acq_rel) != 0U)
  {
    PublishFailure(UartConcurrentConfigStressFailure::EXTERNAL_ABORT,
                   LibXR::ErrorCode::STATE_ERR);
    return Snapshot(ElapsedMicroseconds(start_us));
  }

  if (!ArgumentsValid())
  {
    PublishFailure(UartConcurrentConfigStressFailure::INVALID_ARGUMENT,
                   LibXR::ErrorCode::ARG_ERR);
  }
  else if (!StopRequested() &&
           !WaitForState(kWorkersReady, test_case_.worker_ready_timeout_ms))
  {
    PublishFailure(UartConcurrentConfigStressFailure::WORKER_READY_TIMEOUT,
                   LibXR::ErrorCode::TIMEOUT);
  }

  if (!StopRequested())
  {
    PublishState(UartConcurrentConfigStressState::START);
    const uint32_t phase_start_ms = NowMilliseconds();
    uint32_t last_drain_ms = phase_start_ms;
    uint32_t last_write_progress_ms = phase_start_ms;
    uint32_t last_config_progress_ms = phase_start_ms;
    uint32_t last_rx_progress_ms = phase_start_ms;
    uint32_t last_accepted_writes = 0U;
    uint32_t last_accepted_configs = 0U;
    uint32_t last_observed_rx = 0U;

    while (!TimedOut(phase_start_ms, test_case_.duration_ms) && !StopRequested())
    {
      const uint32_t now_ms = NowMilliseconds();
      if (static_cast<uint32_t>(now_ms - last_drain_ms) >=
          test_case_.rx_drain_interval_ms)
      {
        const size_t queued_before_clear = uart_.read_port_->Size();
        const auto clear_answer = uart_.read_port_->ClearQueuedData();
        if (clear_answer != LibXR::ErrorCode::OK)
        {
          PublishFailure(UartConcurrentConfigStressFailure::RX_CLEAR, clear_answer);
          break;
        }
        SaturatingAdd(counters_.observed_rx_bytes,
                      queued_before_clear > std::numeric_limits<uint32_t>::max()
                          ? std::numeric_limits<uint32_t>::max()
                          : static_cast<uint32_t>(queued_before_clear));
        last_drain_ms = now_ms;
      }

      const uint32_t accepted_writes =
          counters_.accepted_writes.load(std::memory_order_relaxed);
      const uint32_t accepted_configs =
          counters_.accepted_configurations.load(std::memory_order_relaxed);
      const uint32_t observed_rx =
          counters_.observed_rx_bytes.load(std::memory_order_relaxed);
      if (accepted_writes != last_accepted_writes)
      {
        last_accepted_writes = accepted_writes;
        last_write_progress_ms = now_ms;
      }
      if (accepted_configs != last_accepted_configs)
      {
        last_accepted_configs = accepted_configs;
        last_config_progress_ms = now_ms;
      }
      if (observed_rx != last_observed_rx)
      {
        last_observed_rx = observed_rx;
        last_rx_progress_ms = now_ms;
      }

      if (static_cast<uint32_t>(now_ms - last_write_progress_ms) >=
          test_case_.progress_timeout_ms)
      {
        PublishFailure(UartConcurrentConfigStressFailure::WRITE_PROGRESS_TIMEOUT,
                       LibXR::ErrorCode::TIMEOUT);
        break;
      }
      if (static_cast<uint32_t>(now_ms - last_config_progress_ms) >=
          test_case_.progress_timeout_ms)
      {
        PublishFailure(UartConcurrentConfigStressFailure::CONFIG_PROGRESS_TIMEOUT,
                       LibXR::ErrorCode::TIMEOUT);
        break;
      }
      if (test_case_.min_observed_rx_bytes != 0U &&
          static_cast<uint32_t>(now_ms - last_rx_progress_ms) >=
              test_case_.progress_timeout_ms)
      {
        PublishFailure(UartConcurrentConfigStressFailure::RX_PROGRESS_TIMEOUT,
                       LibXR::ErrorCode::TIMEOUT);
        break;
      }
      LibXR::Thread::Sleep(1U);
    }
  }

  RequestStop();
  if (!WaitForState(kWorkersDone, test_case_.worker_stop_timeout_ms))
  {
    PublishFailure(UartConcurrentConfigStressFailure::WORKER_STOP_TIMEOUT,
                   LibXR::ErrorCode::TIMEOUT);
  }

  if (failure_info_.load(std::memory_order_acquire) == 0U)
  {
    if (counters_.accepted_writes.load(std::memory_order_relaxed) <
        test_case_.min_accepted_writes)
    {
      PublishFailure(UartConcurrentConfigStressFailure::INSUFFICIENT_WRITE_PROGRESS,
                     LibXR::ErrorCode::CHECK_ERR);
    }
    else if (counters_.accepted_configurations.load(std::memory_order_relaxed) <
             test_case_.min_accepted_configurations)
    {
      PublishFailure(UartConcurrentConfigStressFailure::INSUFFICIENT_CONFIG_PROGRESS,
                     LibXR::ErrorCode::CHECK_ERR);
    }
    else if (counters_.required_boundary_length_mask.load(std::memory_order_relaxed) !=
             kRequiredBoundaryLengthMask)
    {
      PublishFailure(UartConcurrentConfigStressFailure::BOUNDARY_COVERAGE,
                     LibXR::ErrorCode::CHECK_ERR);
    }
    else if (counters_.completed_batch_depth_mask.load(std::memory_order_relaxed) !=
             kRequiredBatchDepthMask)
    {
      PublishFailure(UartConcurrentConfigStressFailure::BATCH_DEPTH_COVERAGE,
                     LibXR::ErrorCode::CHECK_ERR);
    }
    else if (counters_.observed_rx_bytes.load(std::memory_order_relaxed) <
             test_case_.min_observed_rx_bytes)
    {
      PublishFailure(UartConcurrentConfigStressFailure::INSUFFICIENT_RX_PROGRESS,
                     LibXR::ErrorCode::CHECK_ERR);
    }
  }

  return Snapshot(ElapsedMicroseconds(start_us));
}

void UartConcurrentConfigStressTest::Abort(LibXR::ErrorCode error)
{
  PublishFailure(UartConcurrentConfigStressFailure::EXTERNAL_ABORT, error);
}

uint32_t UartConcurrentConfigStressTest::State() const
{
  return state_.load(std::memory_order_acquire);
}

UartConcurrentConfigStressResult UartConcurrentConfigStressTest::Snapshot(
    uint64_t elapsed_us) const
{
  UartConcurrentConfigStressResult result;
  const uint32_t failure_info = failure_info_.load(std::memory_order_acquire);
  result.failure = DecodeFailure(failure_info);
  result.error = DecodeError(failure_info);
  result.counters.write_attempts =
      counters_.write_attempts.load(std::memory_order_relaxed);
  result.counters.accepted_writes =
      counters_.accepted_writes.load(std::memory_order_relaxed);
  result.counters.accepted_write_bytes =
      counters_.accepted_write_bytes.load(std::memory_order_relaxed);
  result.counters.write_busy_retries =
      counters_.write_busy_retries.load(std::memory_order_relaxed);
  result.counters.write_full_retries =
      counters_.write_full_retries.load(std::memory_order_relaxed);
  result.counters.max_write_call_us =
      counters_.max_write_call_us.load(std::memory_order_relaxed);
  result.counters.required_boundary_length_mask =
      counters_.required_boundary_length_mask.load(std::memory_order_relaxed);
  result.counters.completed_batch_depth_mask =
      counters_.completed_batch_depth_mask.load(std::memory_order_relaxed);
  result.counters.config_attempts =
      counters_.config_attempts.load(std::memory_order_relaxed);
  result.counters.accepted_configurations =
      counters_.accepted_configurations.load(std::memory_order_relaxed);
  result.counters.config_busy_retries =
      counters_.config_busy_retries.load(std::memory_order_relaxed);
  result.counters.max_config_call_us =
      counters_.max_config_call_us.load(std::memory_order_relaxed);
  result.counters.observed_rx_bytes =
      counters_.observed_rx_bytes.load(std::memory_order_relaxed);
  result.final_state = State();
  result.elapsed_us = elapsed_us;
  return result;
}

const char* UartConcurrentConfigStressFailureName(
    UartConcurrentConfigStressFailure failure)
{
  switch (failure)
  {
    case UartConcurrentConfigStressFailure::NONE:
      return "NONE";
    case UartConcurrentConfigStressFailure::INVALID_ARGUMENT:
      return "INVALID_ARGUMENT";
    case UartConcurrentConfigStressFailure::EXTERNAL_ABORT:
      return "EXTERNAL_ABORT";
    case UartConcurrentConfigStressFailure::WORKER_READY_TIMEOUT:
      return "WORKER_READY_TIMEOUT";
    case UartConcurrentConfigStressFailure::WRITE_RESULT:
      return "WRITE_RESULT";
    case UartConcurrentConfigStressFailure::CONFIG_RESULT:
      return "CONFIG_RESULT";
    case UartConcurrentConfigStressFailure::WRITE_CALL_TIMEOUT:
      return "WRITE_CALL_TIMEOUT";
    case UartConcurrentConfigStressFailure::CONFIG_CALL_TIMEOUT:
      return "CONFIG_CALL_TIMEOUT";
    case UartConcurrentConfigStressFailure::WRITE_ATTEMPT_LIMIT:
      return "WRITE_ATTEMPT_LIMIT";
    case UartConcurrentConfigStressFailure::CONFIG_ATTEMPT_LIMIT:
      return "CONFIG_ATTEMPT_LIMIT";
    case UartConcurrentConfigStressFailure::BOUNDARY_COVERAGE:
      return "BOUNDARY_COVERAGE";
    case UartConcurrentConfigStressFailure::BATCH_DEPTH_COVERAGE:
      return "BATCH_DEPTH_COVERAGE";
    case UartConcurrentConfigStressFailure::RX_CLEAR:
      return "RX_CLEAR";
    case UartConcurrentConfigStressFailure::WRITE_PROGRESS_TIMEOUT:
      return "WRITE_PROGRESS_TIMEOUT";
    case UartConcurrentConfigStressFailure::CONFIG_PROGRESS_TIMEOUT:
      return "CONFIG_PROGRESS_TIMEOUT";
    case UartConcurrentConfigStressFailure::RX_PROGRESS_TIMEOUT:
      return "RX_PROGRESS_TIMEOUT";
    case UartConcurrentConfigStressFailure::WORKER_STOP_TIMEOUT:
      return "WORKER_STOP_TIMEOUT";
    case UartConcurrentConfigStressFailure::INSUFFICIENT_WRITE_PROGRESS:
      return "INSUFFICIENT_WRITE_PROGRESS";
    case UartConcurrentConfigStressFailure::INSUFFICIENT_CONFIG_PROGRESS:
      return "INSUFFICIENT_CONFIG_PROGRESS";
    case UartConcurrentConfigStressFailure::INSUFFICIENT_RX_PROGRESS:
      return "INSUFFICIENT_RX_PROGRESS";
  }
  return "UNKNOWN";
}

UartStressTrafficBarrierResult RetireUartStressTraffic(
    LibXR::UART& uart, const UartStressTrafficBarrierCase& test_case,
    LibXR::RawData rx_scratch)
{
  UartStressTrafficBarrierResult result;
  const uint64_t start_us = static_cast<uint64_t>(LibXR::Timebase::GetMicroseconds());

  if (uart.read_port_ == nullptr || uart.write_port_ == nullptr ||
      !uart.read_port_->Readable() || !uart.write_port_->Writable() ||
      rx_scratch.addr_ == nullptr || rx_scratch.size_ == 0U ||
      test_case.config_timeout_ms == 0U || test_case.marker_timeout_ms == 0U ||
      test_case.rx_quiet_time_ms == 0U)
  {
    SetBarrierFailure(result, UartStressTrafficBarrierFailure::INVALID_ARGUMENT,
                      LibXR::ErrorCode::ARG_ERR);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }

  const size_t initially_queued = uart.read_port_->Size();
  const auto clear_answer = uart.read_port_->ClearQueuedData();
  if (clear_answer != LibXR::ErrorCode::OK)
  {
    SetBarrierFailure(result, UartStressTrafficBarrierFailure::CLEAR_RX, clear_answer);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }
  result.discarded_prefix_bytes = initially_queued;

  const uint32_t config_start_ms = NowMilliseconds();
  while (true)
  {
    const auto config_answer = uart.SetConfig(test_case.final_config);
    if (config_answer == LibXR::ErrorCode::OK)
    {
      break;
    }
    if (config_answer != LibXR::ErrorCode::BUSY)
    {
      SetBarrierFailure(result, UartStressTrafficBarrierFailure::FINAL_CONFIG,
                        config_answer);
      result.elapsed_us = ElapsedMicroseconds(start_us);
      return result;
    }

    ++result.config_busy_retries;
    if (TimedOut(config_start_ms, test_case.config_timeout_ms))
    {
      SetBarrierFailure(result, UartStressTrafficBarrierFailure::FINAL_CONFIG,
                        LibXR::ErrorCode::TIMEOUT);
      result.elapsed_us = ElapsedMicroseconds(start_us);
      return result;
    }
    if (test_case.retry_interval_ms == 0U)
    {
      LibXR::Thread::Yield();
    }
    else
    {
      LibXR::Thread::Sleep(test_case.retry_interval_ms);
    }
  }

  const uint32_t marker_start_ms = NowMilliseconds();
  while (true)
  {
    LibXR::WriteOperation operation{};
    const auto write_answer =
        uart.Write({kMarker.data(), kMarker.size()}, operation, false);
    if (write_answer == LibXR::ErrorCode::OK)
    {
      break;
    }
    if (write_answer == LibXR::ErrorCode::BUSY)
    {
      ++result.marker_busy_retries;
    }
    else if (write_answer == LibXR::ErrorCode::FULL)
    {
      ++result.marker_full_retries;
    }
    else
    {
      SetBarrierFailure(result, UartStressTrafficBarrierFailure::MARKER_WRITE,
                        write_answer);
      result.elapsed_us = ElapsedMicroseconds(start_us);
      return result;
    }

    if (TimedOut(marker_start_ms, test_case.marker_timeout_ms))
    {
      SetBarrierFailure(result, UartStressTrafficBarrierFailure::MARKER_WRITE,
                        LibXR::ErrorCode::TIMEOUT);
      result.elapsed_us = ElapsedMicroseconds(start_us);
      return result;
    }
    if (test_case.retry_interval_ms == 0U)
    {
      LibXR::Thread::Yield();
    }
    else
    {
      LibXR::Thread::Sleep(test_case.retry_interval_ms);
    }
  }

  auto* rx = static_cast<uint8_t*>(rx_scratch.addr_);
  MarkerScanner scanner;
  size_t scanned_bytes = 0U;
  bool marker_found = false;
  while (!marker_found)
  {
    const uint32_t elapsed_ms =
        static_cast<uint32_t>(NowMilliseconds() - marker_start_ms);
    if (elapsed_ms >= test_case.marker_timeout_ms)
    {
      SetBarrierFailure(result, UartStressTrafficBarrierFailure::MARKER_READ_TIMEOUT,
                        LibXR::ErrorCode::TIMEOUT);
      result.elapsed_us = ElapsedMicroseconds(start_us);
      return result;
    }

    const size_t queued = uart.read_port_->Size();
    const size_t read_size = queued == 0U ? 1U : std::min(queued, rx_scratch.size_);
    LibXR::Semaphore read_semaphore(0U);
    LibXR::ReadOperation operation(
        read_semaphore, std::max<uint32_t>(1U, test_case.marker_timeout_ms - elapsed_ms));
    const auto read_answer = uart.Read({rx, read_size}, operation, false);
    if (read_answer != LibXR::ErrorCode::OK)
    {
      const auto failure = read_answer == LibXR::ErrorCode::TIMEOUT
                               ? UartStressTrafficBarrierFailure::MARKER_READ_TIMEOUT
                               : UartStressTrafficBarrierFailure::MARKER_READ;
      SetBarrierFailure(result, failure, read_answer);
      result.elapsed_us = ElapsedMicroseconds(start_us);
      return result;
    }

    for (size_t index = 0U; index < read_size; ++index)
    {
      ++scanned_bytes;
      if (!scanner.Consume(rx[index]))
      {
        continue;
      }

      marker_found = true;
      result.discarded_prefix_bytes += scanned_bytes - kMarker.size();
      if (index + 1U < read_size)
      {
        result.unexpected_trailing_bytes = read_size - index - 1U;
        SetBarrierFailure(result, UartStressTrafficBarrierFailure::MARKER_TRAILING_DATA,
                          LibXR::ErrorCode::CHECK_ERR);
      }
      break;
    }
  }

  if (!result.Passed())
  {
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }

  const uint32_t quiet_start_ms = NowMilliseconds();
  while (!TimedOut(quiet_start_ms, test_case.rx_quiet_time_ms))
  {
    const size_t queued = uart.read_port_->Size();
    if (queued != 0U)
    {
      result.unexpected_trailing_bytes = queued;
      SetBarrierFailure(result, UartStressTrafficBarrierFailure::MARKER_TRAILING_DATA,
                        LibXR::ErrorCode::CHECK_ERR);
      break;
    }
    LibXR::Thread::Sleep(1U);
  }

  if (result.Passed())
  {
    const size_t queued = uart.read_port_->Size();
    if (queued != 0U)
    {
      result.unexpected_trailing_bytes = queued;
      SetBarrierFailure(result, UartStressTrafficBarrierFailure::MARKER_TRAILING_DATA,
                        LibXR::ErrorCode::CHECK_ERR);
    }
  }

  result.elapsed_us = ElapsedMicroseconds(start_us);
  return result;
}

const char* UartStressTrafficBarrierFailureName(UartStressTrafficBarrierFailure failure)
{
  switch (failure)
  {
    case UartStressTrafficBarrierFailure::NONE:
      return "NONE";
    case UartStressTrafficBarrierFailure::INVALID_ARGUMENT:
      return "INVALID_ARGUMENT";
    case UartStressTrafficBarrierFailure::CLEAR_RX:
      return "CLEAR_RX";
    case UartStressTrafficBarrierFailure::FINAL_CONFIG:
      return "FINAL_CONFIG";
    case UartStressTrafficBarrierFailure::MARKER_WRITE:
      return "MARKER_WRITE";
    case UartStressTrafficBarrierFailure::MARKER_READ_TIMEOUT:
      return "MARKER_READ_TIMEOUT";
    case UartStressTrafficBarrierFailure::MARKER_READ:
      return "MARKER_READ";
    case UartStressTrafficBarrierFailure::MARKER_TRAILING_DATA:
      return "MARKER_TRAILING_DATA";
  }
  return "UNKNOWN";
}

}  // namespace LibXRTest
