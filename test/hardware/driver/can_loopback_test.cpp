#include "driver/can_loopback_test.hpp"

#include <array>
#if defined(LIBXR_SYSTEM_linux)
#include <thread>
#endif

#include "libxr.hpp"

namespace LibXRTest
{
namespace
{

constexpr uint32_t kMinimumBurstSize = 4U;
constexpr uint32_t kMaximumBurstSize = 256U;
constexpr uint32_t kAdmissionPhaseMask = 0x3U;
constexpr uint32_t kAdmissionCallback = 0x4U;
constexpr uint32_t kClassicStandardIdBase = 0x120U;
constexpr uint32_t kClassicExtendedIdBase = 0x01ABCDE0U;
constexpr uint32_t kFdStandardIdBase = 0x320U;
constexpr uint32_t kFdExtendedIdBase = 0x01BCDEF0U;
constexpr std::array<uint8_t, 9U> kClassicLengths = {0U, 1U, 8U, 3U, 7U, 2U, 6U, 4U, 5U};
constexpr std::array<uint8_t, 10U> kFdLengths = {0U,  1U,  8U,  12U, 16U,
                                                 20U, 24U, 32U, 48U, 64U};

uint32_t NextPatternWord(uint32_t state)
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

void FillPattern(uint8_t* output, size_t size, uint32_t seed)
{
  uint32_t state = seed;
  for (size_t i = 0U; i < size; ++i)
  {
    state = NextPatternWord(state + static_cast<uint32_t>(i));
    output[i] = static_cast<uint8_t>(state >> 24U);
  }
}

LibXR::CAN::ClassicPack MakeClassicPack(uint32_t index, uint32_t seed)
{
  LibXR::CAN::ClassicPack pack{};
  const bool extended = (index & 1U) != 0U;
  pack.type = extended ? LibXR::CAN::Type::EXTENDED : LibXR::CAN::Type::STANDARD;
  pack.id = (extended ? kClassicExtendedIdBase : kClassicStandardIdBase) + index / 2U;
  pack.dlc = kClassicLengths[index % kClassicLengths.size()];
  FillPattern(pack.data, pack.dlc, seed ^ 0xC1A551C0U ^ (index * 0x9E3779B9U));
  return pack;
}

LibXR::FDCAN::FDPack MakeFdPack(uint32_t index, uint32_t seed)
{
  LibXR::FDCAN::FDPack pack{};
  const bool extended = (index & 1U) != 0U;
  pack.type = extended ? LibXR::CAN::Type::EXTENDED : LibXR::CAN::Type::STANDARD;
  pack.id = (extended ? kFdExtendedIdBase : kFdStandardIdBase) + index / 2U;
  pack.len = kFdLengths[index % kFdLengths.size()];
  FillPattern(pack.data, pack.len, seed ^ 0xFDCA4E00U ^ (index * 0x85EBCA6BU));
  return pack;
}

bool TimedOut(uint32_t start_ms, uint32_t timeout_ms)
{
  const uint32_t now_ms = static_cast<uint32_t>(LibXR::Timebase::GetMilliseconds());
  return static_cast<uint32_t>(now_ms - start_ms) >= timeout_ms;
}

uint64_t ElapsedMicroseconds(uint64_t start_us)
{
  return static_cast<uint64_t>(LibXR::Timebase::GetMicroseconds()) - start_us;
}

}  // namespace

#if defined(LIBXR_SYSTEM_linux)
namespace Internal
{

namespace
{

constexpr uint32_t kBarrierDisabled = UINT32_MAX;
std::atomic<uint32_t> fd_callback_barrier_index{kBarrierDisabled};
std::atomic<bool> fd_callback_barrier_reached{false};
std::atomic<bool> fd_callback_barrier_release{true};

}  // namespace

void ArmFdCallbackBarrier(uint32_t frame_index)
{
  fd_callback_barrier_reached.store(false, std::memory_order_relaxed);
  fd_callback_barrier_release.store(false, std::memory_order_relaxed);
  fd_callback_barrier_index.store(frame_index, std::memory_order_release);
}

bool FdCallbackBarrierReached()
{
  return fd_callback_barrier_reached.load(std::memory_order_acquire);
}

void ReleaseFdCallbackBarrier()
{
  fd_callback_barrier_release.store(true, std::memory_order_release);
  fd_callback_barrier_index.store(kBarrierDisabled, std::memory_order_relaxed);
}

void FdCallbackCheckpoint(uint32_t frame_index)
{
  if (fd_callback_barrier_index.load(std::memory_order_acquire) != frame_index)
  {
    return;
  }

  fd_callback_barrier_reached.store(true, std::memory_order_release);
  while (!fd_callback_barrier_release.load(std::memory_order_acquire))
  {
    std::this_thread::yield();
  }
}

}  // namespace Internal
#endif

CanFdLoopbackTestSession::CanFdLoopbackTestSession(LibXR::FDCAN& fdcan) : fdcan_(fdcan)
{
  const auto classic_callback = LibXR::CAN::Callback::Create(OnClassic, this);
  fdcan_.Register(classic_callback, LibXR::CAN::Type::STANDARD);
  fdcan_.Register(classic_callback, LibXR::CAN::Type::EXTENDED);
  fdcan_.Register(LibXR::CAN::Callback::Create(OnError, this), LibXR::CAN::Type::ERROR);

  const auto fd_callback = LibXR::FDCAN::CallbackFD::Create(OnFd, this);
  fdcan_.Register(fd_callback, LibXR::CAN::Type::STANDARD);
  fdcan_.Register(fd_callback, LibXR::CAN::Type::EXTENDED);
}

CanFdLoopbackTestSession::CallbackGuard::~CallbackGuard()
{
  if (completed_ != nullptr)
  {
    completed_->fetch_add(1U, std::memory_order_release);
  }
  owner_.LeaveCallback();
}

bool CanFdLoopbackTestSession::TryEnterCallback(Phase& phase)
{
  uint32_t state = admission_.load(std::memory_order_acquire);
  while (static_cast<Phase>(state & kAdmissionPhaseMask) != Phase::IDLE)
  {
    const uint32_t desired = state + kAdmissionCallback;
    if (admission_.compare_exchange_weak(state, desired, std::memory_order_acquire,
                                         std::memory_order_relaxed))
    {
      phase = static_cast<Phase>(state & kAdmissionPhaseMask);
      return true;
    }
  }
  return false;
}

void CanFdLoopbackTestSession::LeaveCallback()
{
  admission_.fetch_sub(kAdmissionCallback, std::memory_order_release);
}

bool CanFdLoopbackTestSession::TransitionPhase(Phase expected, Phase next)
{
  uint32_t state = admission_.load(std::memory_order_acquire);
  while (static_cast<Phase>(state & kAdmissionPhaseMask) == expected)
  {
    const uint32_t desired = (state & ~kAdmissionPhaseMask) | static_cast<uint32_t>(next);
    if (admission_.compare_exchange_weak(state, desired, std::memory_order_acq_rel,
                                         std::memory_order_relaxed))
    {
      return true;
    }
  }
  return false;
}

void CanFdLoopbackTestSession::CloseCallbacks()
{
  uint32_t state = admission_.load(std::memory_order_acquire);
  while (static_cast<Phase>(state & kAdmissionPhaseMask) != Phase::IDLE)
  {
    const uint32_t desired = state & ~kAdmissionPhaseMask;
    if (admission_.compare_exchange_weak(state, desired, std::memory_order_acq_rel,
                                         std::memory_order_relaxed))
    {
      return;
    }
  }
}

void CanFdLoopbackTestSession::OnClassic(bool in_isr, CanFdLoopbackTestSession* self,
                                         const LibXR::CAN::ClassicPack& pack)
{
  self->HandleClassic(in_isr, pack);
}

void CanFdLoopbackTestSession::OnFd(bool in_isr, CanFdLoopbackTestSession* self,
                                    const LibXR::FDCAN::FDPack& pack)
{
  self->HandleFd(in_isr, pack);
}

void CanFdLoopbackTestSession::OnError(bool in_isr, CanFdLoopbackTestSession* self,
                                       const LibXR::CAN::ClassicPack& pack)
{
  self->HandleError(in_isr, pack);
}

void CanFdLoopbackTestSession::ObserveContext(bool in_isr, uint32_t frame_index)
{
  if (in_isr)
  {
    isr_callbacks_.fetch_add(1U, std::memory_order_relaxed);
  }
  else
  {
    thread_callbacks_.fetch_add(1U, std::memory_order_relaxed);
  }

  const bool mismatch =
      (callback_context_ == CanCallbackContextExpectation::ISR && !in_isr) ||
      (callback_context_ == CanCallbackContextExpectation::THREAD && in_isr);
  if (mismatch)
  {
    RecordFailure(CanFdLoopbackFailure::CALLBACK_CONTEXT, LibXR::ErrorCode::CHECK_ERR,
                  frame_index);
  }
}

void CanFdLoopbackTestSession::HandleClassic(bool in_isr,
                                             const LibXR::CAN::ClassicPack& pack)
{
  Phase phase = Phase::IDLE;
  if (!TryEnterCallback(phase))
  {
    return;
  }
  if (phase != Phase::CLASSIC)
  {
    CallbackGuard guard(*this);
    RecordFailure(CanFdLoopbackFailure::CALLBACK_PHASE, LibXR::ErrorCode::CHECK_ERR,
                  UINT32_MAX, LibXR::CAN::Type::TYPE_NUM, pack.type, UINT32_MAX, pack.id,
                  UINT8_MAX, pack.dlc);
    return;
  }

  const uint32_t index = classic_callbacks_.fetch_add(1U, std::memory_order_acq_rel);
  CallbackGuard guard(*this, &classic_callbacks_completed_);
  ObserveContext(in_isr, index);
  if (index >= classic_frame_count_)
  {
    RecordFailure(CanFdLoopbackFailure::EXTRA_CALLBACK, LibXR::ErrorCode::CHECK_ERR,
                  index, LibXR::CAN::Type::TYPE_NUM, pack.type, UINT32_MAX, pack.id,
                  UINT8_MAX, pack.dlc);
    return;
  }

  const auto expected = MakeClassicPack(index, pattern_seed_);
  if (pack.type != expected.type)
  {
    RecordFailure(CanFdLoopbackFailure::FRAME_TYPE_MISMATCH, LibXR::ErrorCode::CHECK_ERR,
                  index, expected.type, pack.type, expected.id, pack.id, expected.dlc,
                  pack.dlc);
    return;
  }
  if (pack.id != expected.id)
  {
    RecordFailure(CanFdLoopbackFailure::FRAME_ID_MISMATCH, LibXR::ErrorCode::CHECK_ERR,
                  index, expected.type, pack.type, expected.id, pack.id, expected.dlc,
                  pack.dlc);
    return;
  }
  if (pack.dlc != expected.dlc)
  {
    RecordFailure(CanFdLoopbackFailure::FRAME_LENGTH_MISMATCH,
                  LibXR::ErrorCode::CHECK_ERR, index, expected.type, pack.type,
                  expected.id, pack.id, expected.dlc, pack.dlc);
    return;
  }
  for (size_t offset = 0U; offset < expected.dlc; ++offset)
  {
    if (pack.data[offset] != expected.data[offset])
    {
      RecordFailure(CanFdLoopbackFailure::FRAME_DATA_MISMATCH,
                    LibXR::ErrorCode::CHECK_ERR, index, expected.type, pack.type,
                    expected.id, pack.id, expected.dlc, pack.dlc, offset);
      return;
    }
  }
}

void CanFdLoopbackTestSession::HandleFd(bool in_isr, const LibXR::FDCAN::FDPack& pack)
{
  Phase phase = Phase::IDLE;
  if (!TryEnterCallback(phase))
  {
    return;
  }
  if (phase != Phase::FD)
  {
    CallbackGuard guard(*this);
    RecordFailure(CanFdLoopbackFailure::CALLBACK_PHASE, LibXR::ErrorCode::CHECK_ERR,
                  UINT32_MAX, LibXR::CAN::Type::TYPE_NUM, pack.type, UINT32_MAX, pack.id,
                  UINT8_MAX, pack.len);
    return;
  }

  const uint32_t index = fd_callbacks_.fetch_add(1U, std::memory_order_acq_rel);
  CallbackGuard guard(*this, &fd_callbacks_completed_);
#if defined(LIBXR_SYSTEM_linux)
  Internal::FdCallbackCheckpoint(index);
#endif
  ObserveContext(in_isr, index);
  if (index >= fd_frame_count_)
  {
    RecordFailure(CanFdLoopbackFailure::EXTRA_CALLBACK, LibXR::ErrorCode::CHECK_ERR,
                  index, LibXR::CAN::Type::TYPE_NUM, pack.type, UINT32_MAX, pack.id,
                  UINT8_MAX, pack.len);
    return;
  }

  const auto expected = MakeFdPack(index, pattern_seed_);
  if (pack.type != expected.type)
  {
    RecordFailure(CanFdLoopbackFailure::FRAME_TYPE_MISMATCH, LibXR::ErrorCode::CHECK_ERR,
                  index, expected.type, pack.type, expected.id, pack.id, expected.len,
                  pack.len);
    return;
  }
  if (pack.id != expected.id)
  {
    RecordFailure(CanFdLoopbackFailure::FRAME_ID_MISMATCH, LibXR::ErrorCode::CHECK_ERR,
                  index, expected.type, pack.type, expected.id, pack.id, expected.len,
                  pack.len);
    return;
  }
  if (pack.len != expected.len)
  {
    RecordFailure(CanFdLoopbackFailure::FRAME_LENGTH_MISMATCH,
                  LibXR::ErrorCode::CHECK_ERR, index, expected.type, pack.type,
                  expected.id, pack.id, expected.len, pack.len);
    return;
  }
  for (size_t offset = 0U; offset < expected.len; ++offset)
  {
    if (pack.data[offset] != expected.data[offset])
    {
      RecordFailure(CanFdLoopbackFailure::FRAME_DATA_MISMATCH,
                    LibXR::ErrorCode::CHECK_ERR, index, expected.type, pack.type,
                    expected.id, pack.id, expected.len, pack.len, offset);
      return;
    }
  }
}

void CanFdLoopbackTestSession::HandleError(bool in_isr,
                                           const LibXR::CAN::ClassicPack& pack)
{
  Phase phase = Phase::IDLE;
  if (!TryEnterCallback(phase))
  {
    return;
  }
  static_cast<void>(phase);
  CallbackGuard guard(*this);
  ObserveContext(in_isr, UINT32_MAX);
  error_frame_id_.store(pack.id, std::memory_order_relaxed);
  error_frame_callbacks_.fetch_add(1U, std::memory_order_relaxed);
  RecordFailure(CanFdLoopbackFailure::ERROR_FRAME_RECEIVED, LibXR::ErrorCode::FAILED,
                UINT32_MAX, LibXR::CAN::Type::ERROR, pack.type, pack.id, pack.id, 0U,
                pack.dlc);
}

void CanFdLoopbackTestSession::RecordFailure(
    CanFdLoopbackFailure failure, LibXR::ErrorCode error, uint32_t frame_index,
    LibXR::CAN::Type expected_type, LibXR::CAN::Type observed_type, uint32_t expected_id,
    uint32_t observed_id, uint8_t expected_length, uint8_t observed_length,
    size_t mismatch_offset)
{
  uint32_t expected = 0U;
  if (!failure_claimed_.compare_exchange_strong(expected, 1U, std::memory_order_acq_rel,
                                                std::memory_order_relaxed))
  {
    return;
  }

  error_.store(static_cast<int32_t>(error), std::memory_order_relaxed);
  failed_frame_index_.store(frame_index, std::memory_order_relaxed);
  expected_type_.store(static_cast<uint32_t>(expected_type), std::memory_order_relaxed);
  observed_type_.store(static_cast<uint32_t>(observed_type), std::memory_order_relaxed);
  expected_id_.store(expected_id, std::memory_order_relaxed);
  observed_id_.store(observed_id, std::memory_order_relaxed);
  expected_length_.store(expected_length, std::memory_order_relaxed);
  observed_length_.store(observed_length, std::memory_order_relaxed);
  mismatch_offset_.store(mismatch_offset, std::memory_order_relaxed);
  failure_.store(static_cast<uint32_t>(failure), std::memory_order_release);
}

bool CanFdLoopbackTestSession::WaitForCallbacks(
    const std::atomic<uint32_t>& callback_count, uint32_t expected_count,
    uint32_t timeout_ms)
{
  const uint32_t start_ms = static_cast<uint32_t>(LibXR::Timebase::GetMilliseconds());
  while (callback_count.load(std::memory_order_acquire) < expected_count)
  {
    if (failure_.load(std::memory_order_acquire) != 0U)
    {
      return false;
    }
    if (TimedOut(start_ms, timeout_ms))
    {
      RecordFailure(CanFdLoopbackFailure::RX_TIMEOUT, LibXR::ErrorCode::TIMEOUT,
                    callback_count.load(std::memory_order_relaxed));
      return false;
    }
    LibXR::Thread::Sleep(1U);
  }
  return failure_.load(std::memory_order_acquire) == 0U;
}

bool CanFdLoopbackTestSession::WaitForCallbackDrain(uint32_t timeout_ms)
{
  const uint32_t start_ms = static_cast<uint32_t>(LibXR::Timebase::GetMilliseconds());
  while ((admission_.load(std::memory_order_acquire) & ~kAdmissionPhaseMask) != 0U)
  {
    if (TimedOut(start_ms, timeout_ms))
    {
      RecordFailure(CanFdLoopbackFailure::RX_TIMEOUT, LibXR::ErrorCode::TIMEOUT);
      return false;
    }
    LibXR::Thread::Sleep(1U);
  }
  return true;
}

bool CanFdLoopbackTestSession::ObserveQuietPeriod(uint32_t expected_classic,
                                                  uint32_t expected_fd,
                                                  uint32_t quiet_time_ms)
{
  const uint32_t start_ms = static_cast<uint32_t>(LibXR::Timebase::GetMilliseconds());
  while (!TimedOut(start_ms, quiet_time_ms))
  {
    if (failure_.load(std::memory_order_acquire) != 0U)
    {
      return false;
    }
    if (classic_callbacks_.load(std::memory_order_acquire) != expected_classic ||
        classic_callbacks_completed_.load(std::memory_order_acquire) !=
            expected_classic ||
        fd_callbacks_.load(std::memory_order_acquire) != expected_fd ||
        fd_callbacks_completed_.load(std::memory_order_acquire) != expected_fd)
    {
      RecordFailure(CanFdLoopbackFailure::EXTRA_CALLBACK, LibXR::ErrorCode::CHECK_ERR);
      return false;
    }
    LibXR::Thread::Sleep(1U);
  }
  return failure_.load(std::memory_order_acquire) == 0U;
}

void CanFdLoopbackTestSession::Reset(const CanFdLoopbackTestCase& test_case)
{
  admission_.store(static_cast<uint32_t>(Phase::IDLE), std::memory_order_release);
  failure_.store(0U, std::memory_order_relaxed);
  error_.store(static_cast<int32_t>(LibXR::ErrorCode::OK), std::memory_order_relaxed);
  classic_callbacks_.store(0U, std::memory_order_relaxed);
  classic_callbacks_completed_.store(0U, std::memory_order_relaxed);
  fd_callbacks_.store(0U, std::memory_order_relaxed);
  fd_callbacks_completed_.store(0U, std::memory_order_relaxed);
  isr_callbacks_.store(0U, std::memory_order_relaxed);
  thread_callbacks_.store(0U, std::memory_order_relaxed);
  error_frame_callbacks_.store(0U, std::memory_order_relaxed);
  error_frame_id_.store(UINT32_MAX, std::memory_order_relaxed);
  failed_frame_index_.store(UINT32_MAX, std::memory_order_relaxed);
  expected_type_.store(static_cast<uint32_t>(LibXR::CAN::Type::TYPE_NUM),
                       std::memory_order_relaxed);
  observed_type_.store(static_cast<uint32_t>(LibXR::CAN::Type::TYPE_NUM),
                       std::memory_order_relaxed);
  expected_id_.store(UINT32_MAX, std::memory_order_relaxed);
  observed_id_.store(UINT32_MAX, std::memory_order_relaxed);
  expected_length_.store(UINT8_MAX, std::memory_order_relaxed);
  observed_length_.store(UINT8_MAX, std::memory_order_relaxed);
  mismatch_offset_.store(SIZE_MAX, std::memory_order_relaxed);
  classic_frame_count_ = test_case.classic_frame_count;
  fd_frame_count_ = test_case.fd_frame_count;
  phase_timeout_ms_ = test_case.phase_timeout_ms;
  pattern_seed_ = test_case.pattern_seed;
  callback_context_ = test_case.callback_context;
  failure_claimed_.store(0U, std::memory_order_release);
}

CanFdLoopbackTestResult CanFdLoopbackTestSession::Finish(CanFdLoopbackTestResult result,
                                                         uint64_t start_us)
{
  CloseCallbacks();
  (void)WaitForCallbackDrain(phase_timeout_ms_ == 0U ? 1U : phase_timeout_ms_);
  result.failure =
      static_cast<CanFdLoopbackFailure>(failure_.load(std::memory_order_acquire));
  result.error = static_cast<LibXR::ErrorCode>(error_.load(std::memory_order_relaxed));
  result.classic_callbacks = classic_callbacks_.load(std::memory_order_relaxed);
  result.classic_callbacks_completed =
      classic_callbacks_completed_.load(std::memory_order_relaxed);
  result.fd_callbacks = fd_callbacks_.load(std::memory_order_relaxed);
  result.fd_callbacks_completed = fd_callbacks_completed_.load(std::memory_order_relaxed);
  result.isr_callbacks = isr_callbacks_.load(std::memory_order_relaxed);
  result.thread_callbacks = thread_callbacks_.load(std::memory_order_relaxed);
  result.error_frame_callbacks = error_frame_callbacks_.load(std::memory_order_relaxed);
  result.error_frame_id = error_frame_id_.load(std::memory_order_relaxed);
  result.failed_frame_index = failed_frame_index_.load(std::memory_order_relaxed);
  result.expected_type =
      static_cast<LibXR::CAN::Type>(expected_type_.load(std::memory_order_relaxed));
  result.observed_type =
      static_cast<LibXR::CAN::Type>(observed_type_.load(std::memory_order_relaxed));
  result.expected_id = expected_id_.load(std::memory_order_relaxed);
  result.observed_id = observed_id_.load(std::memory_order_relaxed);
  result.expected_length =
      static_cast<uint8_t>(expected_length_.load(std::memory_order_relaxed));
  result.observed_length =
      static_cast<uint8_t>(observed_length_.load(std::memory_order_relaxed));
  result.mismatch_offset = mismatch_offset_.load(std::memory_order_relaxed);
  result.elapsed_us = ElapsedMicroseconds(start_us);
  return result;
}

CanFdLoopbackTestResult CanFdLoopbackTestSession::Run(
    const CanFdLoopbackTestCase& test_case)
{
  CanFdLoopbackTestResult result;
  const uint64_t start_us = static_cast<uint64_t>(LibXR::Timebase::GetMicroseconds());

  if (started_.exchange(1U, std::memory_order_acq_rel) != 0U)
  {
    result.failure = CanFdLoopbackFailure::SESSION_ALREADY_USED;
    result.error = LibXR::ErrorCode::BUSY;
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }

  Reset(test_case);
  const bool valid_counts = test_case.classic_frame_count >= kMinimumBurstSize &&
                            test_case.classic_frame_count <= kMaximumBurstSize &&
                            test_case.fd_frame_count >= kMinimumBurstSize &&
                            test_case.fd_frame_count <= kMaximumBurstSize;
  if (!valid_counts || test_case.phase_timeout_ms == 0U ||
      test_case.quiet_time_ms == 0U || !test_case.configuration.fd_mode.fd_enabled)
  {
    RecordFailure(CanFdLoopbackFailure::INVALID_ARGUMENT, LibXR::ErrorCode::ARG_ERR);
    return Finish(result, start_us);
  }

  if (!TransitionPhase(Phase::IDLE, Phase::PREPARING))
  {
    RecordFailure(CanFdLoopbackFailure::CALLBACK_PHASE, LibXR::ErrorCode::FAILED);
    return Finish(result, start_us);
  }
  const auto config_ans = fdcan_.SetConfig(test_case.configuration);
  if (config_ans != LibXR::ErrorCode::OK)
  {
    RecordFailure(CanFdLoopbackFailure::SET_CONFIG, config_ans);
    return Finish(result, start_us);
  }

  LibXR::CAN::ClassicPack classic_error{};
  classic_error.id = LibXR::CAN::FromErrorID(LibXR::CAN::ErrorID::CAN_ERROR_ID_GENERIC);
  classic_error.type = LibXR::CAN::Type::ERROR;
  const auto classic_error_ans = fdcan_.AddMessage(classic_error);
  result.classic_error_frame_rejected = classic_error_ans == LibXR::ErrorCode::ARG_ERR;
  if (!result.classic_error_frame_rejected)
  {
    RecordFailure(CanFdLoopbackFailure::ERROR_FRAME_NOT_REJECTED,
                  classic_error_ans == LibXR::ErrorCode::OK ? LibXR::ErrorCode::CHECK_ERR
                                                            : classic_error_ans);
    return Finish(result, start_us);
  }

  LibXR::FDCAN::FDPack fd_error{};
  fd_error.id = classic_error.id;
  fd_error.type = LibXR::CAN::Type::ERROR;
  const auto fd_error_ans = fdcan_.AddMessage(fd_error);
  result.fd_error_frame_rejected = fd_error_ans == LibXR::ErrorCode::ARG_ERR;
  if (!result.fd_error_frame_rejected)
  {
    RecordFailure(CanFdLoopbackFailure::ERROR_FRAME_NOT_REJECTED,
                  fd_error_ans == LibXR::ErrorCode::OK ? LibXR::ErrorCode::CHECK_ERR
                                                       : fd_error_ans);
    return Finish(result, start_us);
  }

  if (!TransitionPhase(Phase::PREPARING, Phase::CLASSIC) ||
      !WaitForCallbackDrain(test_case.phase_timeout_ms) ||
      failure_.load(std::memory_order_acquire) != 0U)
  {
    return Finish(result, start_us);
  }
  for (uint32_t index = 0U; index < test_case.classic_frame_count; ++index)
  {
    const auto pack = MakeClassicPack(index, test_case.pattern_seed);
    const auto ans = fdcan_.AddMessage(pack);
    if (ans != LibXR::ErrorCode::OK)
    {
      RecordFailure(CanFdLoopbackFailure::CLASSIC_SUBMIT, ans, index, pack.type,
                    pack.type, pack.id, pack.id, pack.dlc, pack.dlc);
      return Finish(result, start_us);
    }
    result.classic_submitted++;
    if (failure_.load(std::memory_order_acquire) != 0U)
    {
      return Finish(result, start_us);
    }
  }
  if (!WaitForCallbacks(classic_callbacks_completed_, test_case.classic_frame_count,
                        test_case.phase_timeout_ms) ||
      !ObserveQuietPeriod(test_case.classic_frame_count, 0U, test_case.quiet_time_ms))
  {
    return Finish(result, start_us);
  }

  if (!TransitionPhase(Phase::CLASSIC, Phase::FD) ||
      !WaitForCallbackDrain(test_case.phase_timeout_ms) ||
      failure_.load(std::memory_order_acquire) != 0U)
  {
    return Finish(result, start_us);
  }
  for (uint32_t index = 0U; index < test_case.fd_frame_count; ++index)
  {
    const auto pack = MakeFdPack(index, test_case.pattern_seed);
    const auto ans = fdcan_.AddMessage(pack);
    if (ans != LibXR::ErrorCode::OK)
    {
      RecordFailure(CanFdLoopbackFailure::FD_SUBMIT, ans, index, pack.type, pack.type,
                    pack.id, pack.id, pack.len, pack.len);
      return Finish(result, start_us);
    }
    result.fd_submitted++;
    if (failure_.load(std::memory_order_acquire) != 0U)
    {
      return Finish(result, start_us);
    }
  }
  if (!WaitForCallbacks(fd_callbacks_completed_, test_case.fd_frame_count,
                        test_case.phase_timeout_ms) ||
      !ObserveQuietPeriod(test_case.classic_frame_count, test_case.fd_frame_count,
                          test_case.quiet_time_ms))
  {
    return Finish(result, start_us);
  }

  if (!TransitionPhase(Phase::FD, Phase::IDLE) ||
      !WaitForCallbackDrain(test_case.phase_timeout_ms))
  {
    return Finish(result, start_us);
  }

  return Finish(result, start_us);
}

const char* CanFdLoopbackFailureName(CanFdLoopbackFailure failure)
{
  switch (failure)
  {
    case CanFdLoopbackFailure::NONE:
      return "NONE";
    case CanFdLoopbackFailure::INVALID_ARGUMENT:
      return "INVALID_ARGUMENT";
    case CanFdLoopbackFailure::SESSION_ALREADY_USED:
      return "SESSION_ALREADY_USED";
    case CanFdLoopbackFailure::SET_CONFIG:
      return "SET_CONFIG";
    case CanFdLoopbackFailure::ERROR_FRAME_NOT_REJECTED:
      return "ERROR_FRAME_NOT_REJECTED";
    case CanFdLoopbackFailure::CLASSIC_SUBMIT:
      return "CLASSIC_SUBMIT";
    case CanFdLoopbackFailure::FD_SUBMIT:
      return "FD_SUBMIT";
    case CanFdLoopbackFailure::CALLBACK_PHASE:
      return "CALLBACK_PHASE";
    case CanFdLoopbackFailure::CALLBACK_CONTEXT:
      return "CALLBACK_CONTEXT";
    case CanFdLoopbackFailure::FRAME_TYPE_MISMATCH:
      return "FRAME_TYPE_MISMATCH";
    case CanFdLoopbackFailure::FRAME_ID_MISMATCH:
      return "FRAME_ID_MISMATCH";
    case CanFdLoopbackFailure::FRAME_LENGTH_MISMATCH:
      return "FRAME_LENGTH_MISMATCH";
    case CanFdLoopbackFailure::FRAME_DATA_MISMATCH:
      return "FRAME_DATA_MISMATCH";
    case CanFdLoopbackFailure::EXTRA_CALLBACK:
      return "EXTRA_CALLBACK";
    case CanFdLoopbackFailure::RX_TIMEOUT:
      return "RX_TIMEOUT";
    case CanFdLoopbackFailure::ERROR_FRAME_RECEIVED:
      return "ERROR_FRAME_RECEIVED";
  }
  return "UNKNOWN";
}

}  // namespace LibXRTest
