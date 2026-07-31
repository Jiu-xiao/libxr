#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

#include "driver/can_loopback_test.hpp"
#include "libxr.hpp"

namespace LibXRTest::Internal
{

void ArmFdCallbackBarrier(uint32_t frame_index);
bool FdCallbackBarrierReached();
void ReleaseFdCallbackBarrier();

}  // namespace LibXRTest::Internal

namespace
{

class FakeFdCan : public LibXR::FDCAN
{
 public:
  enum class DispatchFault : uint8_t
  {
    NONE,
    CLASSIC_REORDER,
    FD_DUPLICATE,
    FD_CORRUPT,
    INCOMING_ERROR,
  };

  FakeFdCan(uint32_t classic_release_count, uint32_t fd_release_count)
      : classic_release_count_(classic_release_count), fd_release_count_(fd_release_count)
  {
  }

  ~FakeFdCan() override { JoinLastCallback(); }

  LibXR::ErrorCode SetConfig(const LibXR::CAN::Configuration& config) override
  {
    LibXR::FDCAN::Configuration fd_config{};
    static_cast<LibXR::CAN::Configuration&>(fd_config) = config;
    return SetConfig(fd_config);
  }

  LibXR::ErrorCode SetConfig(const LibXR::FDCAN::Configuration& config) override
  {
    config_calls_++;
    config_ = config;
    return fail_config_ ? LibXR::ErrorCode::FAILED : LibXR::ErrorCode::OK;
  }

  uint32_t GetClockFreq() const override { return 80000000U; }

  LibXR::ErrorCode GetErrorState(LibXR::CAN::ErrorState& state) const override
  {
    state = {};
    return LibXR::ErrorCode::OK;
  }

  LibXR::ErrorCode AddMessage(const LibXR::CAN::ClassicPack& pack) override
  {
    if (pack.type == LibXR::CAN::Type::ERROR)
    {
      return reject_classic_error_frames_ ? LibXR::ErrorCode::ARG_ERR
                                          : LibXR::ErrorCode::OK;
    }
    if (classic_count_ == fail_classic_index_)
    {
      return LibXR::ErrorCode::FULL;
    }
    if (classic_count_ >= classic_frames_.size())
    {
      return LibXR::ErrorCode::FULL;
    }
    classic_frames_[classic_count_++] = pack;
    peak_classic_ = classic_count_;
    if (!suppress_classic_dispatch_ && classic_count_ == classic_release_count_)
    {
      DispatchClassic();
    }
    return LibXR::ErrorCode::OK;
  }

  LibXR::ErrorCode AddMessage(const LibXR::FDCAN::FDPack& pack) override
  {
    if (pack.type == LibXR::CAN::Type::ERROR)
    {
      return reject_fd_error_frames_ ? LibXR::ErrorCode::ARG_ERR : LibXR::ErrorCode::OK;
    }
    if (fd_count_ == fail_fd_index_)
    {
      return LibXR::ErrorCode::FULL;
    }
    if (fd_count_ >= fd_frames_.size())
    {
      return LibXR::ErrorCode::FULL;
    }
    fd_frames_[fd_count_++] = pack;
    peak_fd_ = fd_count_;
    if (!suppress_fd_dispatch_ && fd_count_ == fd_release_count_)
    {
      DispatchFd();
    }
    return LibXR::ErrorCode::OK;
  }

  void DispatchClassic()
  {
    if (fault_ == DispatchFault::CLASSIC_REORDER && classic_count_ >= 2U)
    {
      OnMessage(classic_frames_[1U], callback_in_isr_);
      OnMessage(classic_frames_[0U], callback_in_isr_);
      for (uint32_t index = 2U; index < classic_count_; ++index)
      {
        OnMessage(classic_frames_[index], callback_in_isr_);
      }
    }
    else
    {
      for (uint32_t index = 0U; index < classic_count_; ++index)
      {
        OnMessage(classic_frames_[index], callback_in_isr_);
        if (fault_ == DispatchFault::INCOMING_ERROR && index == 0U)
        {
          LibXR::CAN::ClassicPack error{};
          error.id = LibXR::CAN::FromErrorID(LibXR::CAN::ErrorID::CAN_ERROR_ID_ACK);
          error.type = LibXR::CAN::Type::ERROR;
          OnMessage(error, callback_in_isr_);
        }
      }
    }
  }

  void DispatchFd()
  {
    const uint32_t synchronous_count =
        async_last_fd_ && fd_count_ > 0U ? fd_count_ - 1U : fd_count_;
    for (uint32_t index = 0U; index < synchronous_count; ++index)
    {
      auto pack = fd_frames_[index];
      if (fault_ == DispatchFault::FD_CORRUPT && index == 4U)
      {
        pack.data[3] ^= 0x80U;
      }
      OnMessage(pack, callback_in_isr_);
    }
    if (async_last_fd_ && fd_count_ > 0U)
    {
      auto last = fd_frames_[fd_count_ - 1U];
      if (corrupt_async_last_fd_ && last.len > 0U)
      {
        last.data[0] ^= 0x80U;
      }
      callback_thread_ = std::thread([this, last] { OnMessage(last, callback_in_isr_); });
      return;
    }
    if (fault_ == DispatchFault::FD_DUPLICATE && fd_count_ > 0U)
    {
      OnMessage(fd_frames_[fd_count_ - 1U], callback_in_isr_);
    }
  }

  void JoinLastCallback()
  {
    if (callback_thread_.joinable())
    {
      callback_thread_.join();
    }
  }

  void EmitLateClassic()
  {
    if (classic_count_ > 0U)
    {
      OnMessage(classic_frames_[0U], callback_in_isr_);
    }
  }

  std::array<LibXR::CAN::ClassicPack, 64U> classic_frames_{};
  std::array<LibXR::FDCAN::FDPack, 64U> fd_frames_{};
  LibXR::FDCAN::Configuration config_{};
  uint32_t classic_release_count_ = 0U;
  uint32_t fd_release_count_ = 0U;
  uint32_t classic_count_ = 0U;
  uint32_t fd_count_ = 0U;
  uint32_t peak_classic_ = 0U;
  uint32_t peak_fd_ = 0U;
  uint32_t config_calls_ = 0U;
  uint32_t fail_classic_index_ = UINT32_MAX;
  uint32_t fail_fd_index_ = UINT32_MAX;
  bool callback_in_isr_ = false;
  bool reject_classic_error_frames_ = true;
  bool reject_fd_error_frames_ = true;
  bool fail_config_ = false;
  bool suppress_classic_dispatch_ = false;
  bool suppress_fd_dispatch_ = false;
  bool async_last_fd_ = false;
  bool corrupt_async_last_fd_ = false;
  DispatchFault fault_ = DispatchFault::NONE;

  std::thread callback_thread_;
};

LibXRTest::CanFdLoopbackTestCase MakeTestCase()
{
  LibXRTest::CanFdLoopbackTestCase test_case;
  test_case.configuration.bitrate = 500000U;
  test_case.configuration.data_bitrate = 2000000U;
  test_case.configuration.fd_mode.fd_enabled = true;
  test_case.configuration.fd_mode.brs = true;
  test_case.classic_frame_count = 8U;
  test_case.fd_frame_count = 12U;
  test_case.phase_timeout_ms = 10U;
  test_case.quiet_time_ms = 1U;
  test_case.pattern_seed = 0x12345678U;
  test_case.callback_context = LibXRTest::CanCallbackContextExpectation::THREAD;
  return test_case;
}

bool Check(bool condition, const char* expression, int line)
{
  if (!condition)
  {
    std::fprintf(stderr, "selftest failure at line %d: %s\n", line, expression);
  }
  return condition;
}

bool WaitForFdCallbackBarrier(uint32_t timeout_ms)
{
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline)
  {
    if (LibXRTest::Internal::FdCallbackBarrierReached())
    {
      return true;
    }
    std::this_thread::yield();
  }
  return LibXRTest::Internal::FdCallbackBarrierReached();
}

#define SELF_CHECK(expression)                                 \
  do                                                           \
  {                                                            \
    if (!Check((expression), #expression, __LINE__)) return 1; \
  } while (false)

}  // namespace

int main()
{
  LibXR::PlatformInit();

  {
    auto test_case = MakeTestCase();
    FakeFdCan fdcan(test_case.classic_frame_count, test_case.fd_frame_count);
    LibXRTest::CanFdLoopbackTestSession session(fdcan);
    const auto result = session.Run(test_case);
    SELF_CHECK(result.Passed());
    SELF_CHECK(result.classic_submitted == test_case.classic_frame_count);
    SELF_CHECK(result.classic_callbacks == test_case.classic_frame_count);
    SELF_CHECK(result.classic_callbacks_completed == test_case.classic_frame_count);
    SELF_CHECK(result.fd_submitted == test_case.fd_frame_count);
    SELF_CHECK(result.fd_callbacks == test_case.fd_frame_count);
    SELF_CHECK(result.fd_callbacks_completed == test_case.fd_frame_count);
    SELF_CHECK(result.thread_callbacks ==
               test_case.classic_frame_count + test_case.fd_frame_count);
    SELF_CHECK(result.isr_callbacks == 0U);
    SELF_CHECK(result.classic_error_frame_rejected);
    SELF_CHECK(result.fd_error_frame_rejected);
    SELF_CHECK(fdcan.peak_classic_ > 3U);
    SELF_CHECK(fdcan.peak_fd_ > 3U);
    SELF_CHECK(fdcan.config_calls_ == 1U);
  }

  {
    auto test_case = MakeTestCase();
    test_case.callback_context = LibXRTest::CanCallbackContextExpectation::ISR;
    FakeFdCan fdcan(test_case.classic_frame_count, test_case.fd_frame_count);
    LibXRTest::CanFdLoopbackTestSession session(fdcan);
    const auto result = session.Run(test_case);
    SELF_CHECK(result.failure == LibXRTest::CanFdLoopbackFailure::CALLBACK_CONTEXT);
  }

  {
    auto test_case = MakeTestCase();
    FakeFdCan fdcan(test_case.classic_frame_count, test_case.fd_frame_count);
    fdcan.fault_ = FakeFdCan::DispatchFault::CLASSIC_REORDER;
    LibXRTest::CanFdLoopbackTestSession session(fdcan);
    const auto result = session.Run(test_case);
    SELF_CHECK(result.failure == LibXRTest::CanFdLoopbackFailure::FRAME_TYPE_MISMATCH);
    SELF_CHECK(result.failed_frame_index == 0U);
  }

  {
    auto test_case = MakeTestCase();
    FakeFdCan fdcan(test_case.classic_frame_count, test_case.fd_frame_count);
    fdcan.fault_ = FakeFdCan::DispatchFault::FD_DUPLICATE;
    LibXRTest::CanFdLoopbackTestSession session(fdcan);
    const auto result = session.Run(test_case);
    SELF_CHECK(result.failure == LibXRTest::CanFdLoopbackFailure::EXTRA_CALLBACK);
    SELF_CHECK(result.fd_callbacks == test_case.fd_frame_count + 1U);
  }

  {
    auto test_case = MakeTestCase();
    FakeFdCan fdcan(test_case.classic_frame_count, test_case.fd_frame_count);
    fdcan.fault_ = FakeFdCan::DispatchFault::FD_CORRUPT;
    LibXRTest::CanFdLoopbackTestSession session(fdcan);
    const auto result = session.Run(test_case);
    SELF_CHECK(result.failure == LibXRTest::CanFdLoopbackFailure::FRAME_DATA_MISMATCH);
    SELF_CHECK(result.failed_frame_index == 4U);
    SELF_CHECK(result.mismatch_offset == 3U);
  }

  {
    auto test_case = MakeTestCase();
    FakeFdCan fdcan(test_case.classic_frame_count, test_case.fd_frame_count);
    fdcan.fault_ = FakeFdCan::DispatchFault::INCOMING_ERROR;
    LibXRTest::CanFdLoopbackTestSession session(fdcan);
    const auto result = session.Run(test_case);
    SELF_CHECK(result.failure == LibXRTest::CanFdLoopbackFailure::ERROR_FRAME_RECEIVED);
    SELF_CHECK(result.error_frame_callbacks == 1U);
    SELF_CHECK(result.error_frame_id ==
               LibXR::CAN::FromErrorID(LibXR::CAN::ErrorID::CAN_ERROR_ID_ACK));
  }

  {
    auto test_case = MakeTestCase();
    test_case.phase_timeout_ms = 2U;
    FakeFdCan fdcan(test_case.classic_frame_count, test_case.fd_frame_count);
    fdcan.suppress_classic_dispatch_ = true;
    LibXRTest::CanFdLoopbackTestSession session(fdcan);
    const auto result = session.Run(test_case);
    SELF_CHECK(result.failure == LibXRTest::CanFdLoopbackFailure::RX_TIMEOUT);
    SELF_CHECK(result.error == LibXR::ErrorCode::TIMEOUT);
  }

  {
    auto test_case = MakeTestCase();
    FakeFdCan fdcan(test_case.classic_frame_count, test_case.fd_frame_count);
    fdcan.reject_classic_error_frames_ = false;
    LibXRTest::CanFdLoopbackTestSession session(fdcan);
    const auto result = session.Run(test_case);
    SELF_CHECK(result.failure ==
               LibXRTest::CanFdLoopbackFailure::ERROR_FRAME_NOT_REJECTED);
    SELF_CHECK(!result.classic_error_frame_rejected);
    SELF_CHECK(result.error == LibXR::ErrorCode::CHECK_ERR);
  }

  {
    auto test_case = MakeTestCase();
    FakeFdCan fdcan(test_case.classic_frame_count, test_case.fd_frame_count);
    fdcan.reject_fd_error_frames_ = false;
    LibXRTest::CanFdLoopbackTestSession session(fdcan);
    const auto result = session.Run(test_case);
    SELF_CHECK(result.failure ==
               LibXRTest::CanFdLoopbackFailure::ERROR_FRAME_NOT_REJECTED);
    SELF_CHECK(result.classic_error_frame_rejected);
    SELF_CHECK(!result.fd_error_frame_rejected);
    SELF_CHECK(result.error == LibXR::ErrorCode::CHECK_ERR);
  }

  {
    auto test_case = MakeTestCase();
    FakeFdCan fdcan(test_case.classic_frame_count, test_case.fd_frame_count);
    fdcan.fail_classic_index_ = 4U;
    LibXRTest::CanFdLoopbackTestSession session(fdcan);
    const auto result = session.Run(test_case);
    SELF_CHECK(result.failure == LibXRTest::CanFdLoopbackFailure::CLASSIC_SUBMIT);
    SELF_CHECK(result.error == LibXR::ErrorCode::FULL);
    SELF_CHECK(result.classic_submitted == 4U);
  }

  {
    auto test_case = MakeTestCase();
    test_case.classic_frame_count = 3U;
    FakeFdCan fdcan(test_case.classic_frame_count, test_case.fd_frame_count);
    LibXRTest::CanFdLoopbackTestSession session(fdcan);
    const auto result = session.Run(test_case);
    SELF_CHECK(result.failure == LibXRTest::CanFdLoopbackFailure::INVALID_ARGUMENT);
  }

  {
    auto test_case = MakeTestCase();
    FakeFdCan fdcan(test_case.classic_frame_count, test_case.fd_frame_count);
    fdcan.fail_config_ = true;
    LibXRTest::CanFdLoopbackTestSession session(fdcan);
    const auto result = session.Run(test_case);
    SELF_CHECK(result.failure == LibXRTest::CanFdLoopbackFailure::SET_CONFIG);
  }

  {
    auto test_case = MakeTestCase();
    test_case.phase_timeout_ms = 1000U;
    FakeFdCan fdcan(test_case.classic_frame_count, test_case.fd_frame_count);
    fdcan.async_last_fd_ = true;
    fdcan.corrupt_async_last_fd_ = true;
    LibXRTest::CanFdLoopbackTestSession session(fdcan);
    LibXRTest::CanFdLoopbackTestResult async_result;
    std::atomic<bool> run_returned{false};
    LibXRTest::Internal::ArmFdCallbackBarrier(test_case.fd_frame_count - 1U);
    std::thread run_thread(
        [&]
        {
          async_result = session.Run(test_case);
          run_returned.store(true, std::memory_order_release);
        });

    const bool callback_waiting = WaitForFdCallbackBarrier(500U);
    const bool returned_before_release = run_returned.load(std::memory_order_acquire);
    const auto concurrent_result = session.Run(test_case);
    LibXRTest::Internal::ReleaseFdCallbackBarrier();
    run_thread.join();
    fdcan.JoinLastCallback();
    fdcan.EmitLateClassic();
    const auto repeated_result = session.Run(test_case);

    SELF_CHECK(callback_waiting);
    SELF_CHECK(!returned_before_release);
    SELF_CHECK(concurrent_result.failure ==
               LibXRTest::CanFdLoopbackFailure::SESSION_ALREADY_USED);
    SELF_CHECK(async_result.failure ==
               LibXRTest::CanFdLoopbackFailure::FRAME_DATA_MISMATCH);
    SELF_CHECK(async_result.failed_frame_index == test_case.fd_frame_count - 1U);
    SELF_CHECK(async_result.mismatch_offset == 0U);
    SELF_CHECK(async_result.fd_callbacks == test_case.fd_frame_count);
    SELF_CHECK(async_result.fd_callbacks_completed == test_case.fd_frame_count);
    SELF_CHECK(repeated_result.failure ==
               LibXRTest::CanFdLoopbackFailure::SESSION_ALREADY_USED);
  }

  std::puts("CAN/FDCAN hardware-test support selftest: PASS");
  return 0;
}
