#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "can.hpp"
#include "libxr_def.hpp"

namespace LibXRTest
{

enum class CanCallbackContextExpectation : uint8_t
{
  ANY,
  ISR,
  THREAD,
};

enum class CanFdLoopbackFailure : uint8_t
{
  NONE,
  INVALID_ARGUMENT,
  SESSION_ALREADY_USED,
  SET_CONFIG,
  ERROR_FRAME_NOT_REJECTED,
  CLASSIC_SUBMIT,
  FD_SUBMIT,
  CALLBACK_PHASE,
  CALLBACK_CONTEXT,
  FRAME_TYPE_MISMATCH,
  FRAME_ID_MISMATCH,
  FRAME_LENGTH_MISMATCH,
  FRAME_DATA_MISMATCH,
  EXTRA_CALLBACK,
  RX_TIMEOUT,
  ERROR_FRAME_RECEIVED,
};

struct CanFdLoopbackTestCase
{
  LibXR::FDCAN::Configuration configuration{};
  uint32_t classic_frame_count = 8U;
  uint32_t fd_frame_count = 12U;
  uint32_t phase_timeout_ms = 0U;
  uint32_t quiet_time_ms = 0U;
  uint32_t pattern_seed = 0U;
  CanCallbackContextExpectation callback_context = CanCallbackContextExpectation::ANY;
};

struct CanFdLoopbackTestResult
{
  CanFdLoopbackFailure failure = CanFdLoopbackFailure::NONE;
  LibXR::ErrorCode error = LibXR::ErrorCode::OK;
  uint32_t classic_submitted = 0U;
  uint32_t classic_callbacks = 0U;
  uint32_t classic_callbacks_completed = 0U;
  uint32_t fd_submitted = 0U;
  uint32_t fd_callbacks = 0U;
  uint32_t fd_callbacks_completed = 0U;
  uint32_t isr_callbacks = 0U;
  uint32_t thread_callbacks = 0U;
  uint32_t error_frame_callbacks = 0U;
  uint32_t error_frame_id = UINT32_MAX;
  uint32_t failed_frame_index = UINT32_MAX;
  LibXR::CAN::Type expected_type = LibXR::CAN::Type::TYPE_NUM;
  LibXR::CAN::Type observed_type = LibXR::CAN::Type::TYPE_NUM;
  uint32_t expected_id = UINT32_MAX;
  uint32_t observed_id = UINT32_MAX;
  uint8_t expected_length = UINT8_MAX;
  uint8_t observed_length = UINT8_MAX;
  size_t mismatch_offset = SIZE_MAX;
  bool classic_error_frame_rejected = false;
  bool fd_error_frame_rejected = false;
  uint64_t elapsed_us = 0U;

  [[nodiscard]] bool Passed() const { return failure == CanFdLoopbackFailure::NONE; }
};

/**
 * @brief Observe one deterministic CAN FD loopback test on a generic FDCAN object.
 *
 * The session registers permanent classic-standard, classic-extended, FD-standard,
 * FD-extended, and error callbacks. Because CAN subscriptions cannot be removed, the
 * session object must remain alive and at the same address until the FDCAN object can no
 * longer dispatch callbacks. `Run()` is one-shot; a second or concurrent call is
 * rejected.
 *
 * `Run()` sends the classic and FD bursts as separate phases. Each phase alternates
 * standard and extended data frames and requires at least four submissions, so a
 * three-entry hardware TX FIFO cannot complete the test without software queue progress.
 * The receive callbacks validate type, ID, legal payload length, byte-exact payload,
 * order, callback count, and the configured ISR/thread-context expectation. Incoming
 * virtual error frames fail the test and retain their ID in the result. Both classic and
 * FD attempts to transmit `Type::ERROR` must be rejected with `ARG_ERR`.
 *
 * The FDCAN object and bus must be dedicated to the test. `Run()` is bounded by one
 * timeout per receive phase plus the configured quiet windows. Call from task or main
 * context after `PlatformInit()` and timebase setup.
 */
class CanFdLoopbackTestSession
{
 public:
  explicit CanFdLoopbackTestSession(LibXR::FDCAN& fdcan);

  CanFdLoopbackTestSession(const CanFdLoopbackTestSession&) = delete;
  CanFdLoopbackTestSession& operator=(const CanFdLoopbackTestSession&) = delete;
  CanFdLoopbackTestSession(CanFdLoopbackTestSession&&) = delete;
  CanFdLoopbackTestSession& operator=(CanFdLoopbackTestSession&&) = delete;

  CanFdLoopbackTestResult Run(const CanFdLoopbackTestCase& test_case);

 private:
  enum class Phase : uint8_t
  {
    IDLE,
    PREPARING,
    CLASSIC,
    FD,
  };

  class CallbackGuard
  {
   public:
    CallbackGuard(CanFdLoopbackTestSession& owner,
                  std::atomic<uint32_t>* completed = nullptr)
        : owner_(owner), completed_(completed)
    {
    }
    ~CallbackGuard();

    CallbackGuard(const CallbackGuard&) = delete;
    CallbackGuard& operator=(const CallbackGuard&) = delete;

   private:
    CanFdLoopbackTestSession& owner_;
    std::atomic<uint32_t>* completed_;
  };

  static void OnClassic(bool in_isr, CanFdLoopbackTestSession* self,
                        const LibXR::CAN::ClassicPack& pack);
  static void OnFd(bool in_isr, CanFdLoopbackTestSession* self,
                   const LibXR::FDCAN::FDPack& pack);
  static void OnError(bool in_isr, CanFdLoopbackTestSession* self,
                      const LibXR::CAN::ClassicPack& pack);

  void HandleClassic(bool in_isr, const LibXR::CAN::ClassicPack& pack);
  void HandleFd(bool in_isr, const LibXR::FDCAN::FDPack& pack);
  void HandleError(bool in_isr, const LibXR::CAN::ClassicPack& pack);
  void ObserveContext(bool in_isr, uint32_t frame_index);
  void RecordFailure(CanFdLoopbackFailure failure, LibXR::ErrorCode error,
                     uint32_t frame_index = UINT32_MAX,
                     LibXR::CAN::Type expected_type = LibXR::CAN::Type::TYPE_NUM,
                     LibXR::CAN::Type observed_type = LibXR::CAN::Type::TYPE_NUM,
                     uint32_t expected_id = UINT32_MAX, uint32_t observed_id = UINT32_MAX,
                     uint8_t expected_length = UINT8_MAX,
                     uint8_t observed_length = UINT8_MAX,
                     size_t mismatch_offset = SIZE_MAX);
  bool WaitForCallbacks(const std::atomic<uint32_t>& callback_count,
                        uint32_t expected_count, uint32_t timeout_ms);
  bool WaitForCallbackDrain(uint32_t timeout_ms);
  bool TryEnterCallback(Phase& phase);
  void LeaveCallback();
  bool TransitionPhase(Phase expected, Phase next);
  void CloseCallbacks();
  bool ObserveQuietPeriod(uint32_t expected_classic, uint32_t expected_fd,
                          uint32_t quiet_time_ms);
  void Reset(const CanFdLoopbackTestCase& test_case);
  CanFdLoopbackTestResult Finish(CanFdLoopbackTestResult result, uint64_t start_us);

  LibXR::FDCAN& fdcan_;
  std::atomic<uint32_t> started_{0U};
  std::atomic<uint32_t> admission_{static_cast<uint32_t>(Phase::IDLE)};
  std::atomic<uint32_t> failure_claimed_{0U};
  std::atomic<uint32_t> failure_{static_cast<uint32_t>(CanFdLoopbackFailure::NONE)};
  std::atomic<int32_t> error_{static_cast<int32_t>(LibXR::ErrorCode::OK)};
  std::atomic<uint32_t> classic_callbacks_{0U};
  std::atomic<uint32_t> classic_callbacks_completed_{0U};
  std::atomic<uint32_t> fd_callbacks_{0U};
  std::atomic<uint32_t> fd_callbacks_completed_{0U};
  std::atomic<uint32_t> isr_callbacks_{0U};
  std::atomic<uint32_t> thread_callbacks_{0U};
  std::atomic<uint32_t> error_frame_callbacks_{0U};
  std::atomic<uint32_t> error_frame_id_{UINT32_MAX};
  std::atomic<uint32_t> failed_frame_index_{UINT32_MAX};
  std::atomic<uint32_t> expected_type_{static_cast<uint32_t>(LibXR::CAN::Type::TYPE_NUM)};
  std::atomic<uint32_t> observed_type_{static_cast<uint32_t>(LibXR::CAN::Type::TYPE_NUM)};
  std::atomic<uint32_t> expected_id_{UINT32_MAX};
  std::atomic<uint32_t> observed_id_{UINT32_MAX};
  std::atomic<uint32_t> expected_length_{UINT8_MAX};
  std::atomic<uint32_t> observed_length_{UINT8_MAX};
  std::atomic<size_t> mismatch_offset_{SIZE_MAX};

  uint32_t classic_frame_count_ = 0U;
  uint32_t fd_frame_count_ = 0U;
  uint32_t phase_timeout_ms_ = 0U;
  uint32_t pattern_seed_ = 0U;
  CanCallbackContextExpectation callback_context_ = CanCallbackContextExpectation::ANY;
};

const char* CanFdLoopbackFailureName(CanFdLoopbackFailure failure);

}  // namespace LibXRTest
