#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

#include "driver/uart_concurrency_stress_test.hpp"
#include "driver/uart_loopback_test.hpp"
#include "libxr.hpp"
#include "serialized_service.hpp"

namespace
{

class MemoryLoopbackUart;

class MemoryWritePort : public LibXR::WritePort
{
 public:
  using LibXR::WritePort::operator=;

  MemoryWritePort(MemoryLoopbackUart& owner, size_t queue_depth, size_t buffer_size)
      : LibXR::WritePort(queue_depth, buffer_size), owner_(owner)
  {
  }

  MemoryLoopbackUart& owner_;
};

class MemoryLoopbackUart : public LibXR::UART
{
 public:
  explicit MemoryLoopbackUart(size_t read_buffer_size = 32768U,
                              size_t write_buffer_size = 8192U)
      : LibXR::UART(&read_port_, &write_port_),
        read_port_(read_buffer_size),
        write_port_(*this, 8U, write_buffer_size)
  {
    write_port_ = WriteFun;
  }

  LibXR::ErrorCode SetConfig(Configuration config, bool = false) override
  {
    if (config_busy_responses_ > 0U)
    {
      config_busy_responses_--;
      return LibXR::ErrorCode::BUSY;
    }

    config_ = config;
    config_busy_responses_ = 2U;
    return LibXR::ErrorCode::OK;
  }

  LibXR::ErrorCode InjectRxByte(uint8_t byte)
  {
    auto queue = read_port_.GetReadQueue();
    const LibXR::ErrorCode result = queue.Push(byte);
    if (result == LibXR::ErrorCode::OK)
    {
      queue.Publish();
    }
    return result;
  }

  static void WriteFun(LibXR::WritePort& port, bool in_isr)
  {
    auto& memory_port = static_cast<MemoryWritePort&>(port);
    auto& owner = memory_port.owner_;
    (void)owner.tx_service_.Invoke(
        1U, [&owner, in_isr](uint32_t) noexcept { owner.ProgressTx(in_isr); });
  }

  void ProgressTx(bool in_isr)
  {
    while (true)
    {
      size_t accepted = 0U;
      size_t expected = 0U;
      size_t completed = 0U;
      bool append_extra = false;
      {
        auto queue = write_port_.GetWriteQueue(in_isr);
        if (queue.front_size == 0U)
        {
          return;
        }

        if (queue.front_size > transfer_.size())
        {
          REQUIRE(queue.FailFront(LibXR::ErrorCode::SIZE_ERR));
          continue;
        }

        if (fail_next_write_)
        {
          fail_next_write_ = false;
          REQUIRE(queue.FailFront(LibXR::ErrorCode::FAILED));
          continue;
        }

        append_extra = append_extra_next_write_;
        expected = queue.front_size;
        const size_t offered = queue.front_size + queue.next_size;
        if (!corrupt_next_write_ && !append_extra && queue.next_size != 0U &&
            offered <= transfer_.size() && read_port_.EmptySize() >= offered)
        {
          expected = offered;
        }

        const size_t rx_size = expected + (append_extra ? 1U : 0U);
        if (read_port_.EmptySize() < rx_size)
        {
          REQUIRE(queue.FailFront(LibXR::ErrorCode::FULL));
          continue;
        }

        accepted = queue.PopWithWriter(
            expected,
            [this, expected](const uint8_t* first, size_t first_size,
                             const uint8_t* second, size_t second_size)
            {
              std::memcpy(transfer_.data(), first, first_size);
              if (second_size != 0U)
              {
                std::memcpy(transfer_.data() + first_size, second, second_size);
              }
              REQUIRE(first_size + second_size == expected);
              return expected;
            });
        REQUIRE(accepted == expected);
        completed = expected == offered && queue.next_size != 0U ? 2U : 1U;
      }

      if (corrupt_next_write_ && expected != 0U)
      {
        corrupt_next_write_ = false;
        transfer_[0] ^= 0x80U;
      }

      {
        auto queue = read_port_.GetReadQueue(in_isr);
        REQUIRE(queue.PushBatch(transfer_.data(), accepted) == LibXR::ErrorCode::OK);
        if (append_extra)
        {
          append_extra_next_write_ = false;
          constexpr uint8_t kUnexpectedByte = 0xA5U;
          REQUIRE(queue.Push(kUnexpectedByte) == LibXR::ErrorCode::OK);
        }
        queue.Publish();
      }
      completed_writes_.fetch_add(static_cast<uint32_t>(completed),
                                  std::memory_order_release);
    }
  }

  LibXR::ReadPort read_port_;
  MemoryWritePort write_port_;
  std::array<uint8_t, 8192U> transfer_{};
  Configuration config_{};
  uint32_t config_busy_responses_ = 0U;
  bool fail_next_write_ = false;
  bool corrupt_next_write_ = false;
  bool append_extra_next_write_ = false;
  std::atomic<uint32_t> completed_writes_{0U};
  LibXR::SerializedService tx_service_;
};

bool Check(bool condition, const char* expression, int line)
{
  if (!condition)
  {
    std::fprintf(stderr, "selftest failure at line %d: %s\n", line, expression);
  }
  return condition;
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
  MemoryLoopbackUart uart;
  std::array<uint8_t, 512U> tx{};
  std::array<uint8_t, 512U> rx{};

  constexpr std::array<LibXR::UART::Configuration, 2> kConfigs = {{
      {115200U, LibXR::UART::Parity::NO_PARITY, 8U, 1U},
      {921600U, LibXR::UART::Parity::NO_PARITY, 8U, 1U},
  }};
  const auto idle_result = LibXRTest::RunUartIdleReconfigureTest(
      uart, {
                .configurations = std::span<const LibXR::UART::Configuration>(kConfigs),
                .transitions = 5U,
                .transition_timeout_ms = 100U,
                .retry_interval_ms = 0U,
            });
  SELF_CHECK(idle_result.Passed());
  SELF_CHECK(idle_result.completed_transitions == 5U);
  SELF_CHECK(idle_result.busy_retries == 8U);

  const LibXRTest::UartLoopbackTestCase single_case = {
      .uart_config = kConfigs[0],
      .frame_size = 64U,
      .rounds = 3U,
      .operation_timeout_ms = 100U,
      .rx_quiet_time_ms = 2U,
      .pattern_seed = 0x12345678U,
      .batch_depth = 1U,
  };
  auto loop_result = LibXRTest::RunUartLoopbackTest(
      uart, single_case, {tx.data(), tx.size()}, {rx.data(), rx.size()});
  SELF_CHECK(loop_result.Passed());
  SELF_CHECK(loop_result.completed_rounds == 3U);
  SELF_CHECK(loop_result.verified_bytes == 192U);

  const LibXRTest::UartLoopbackTestCase batch_case = {
      .uart_config = kConfigs[1],
      .frame_size = 64U,
      .rounds = 3U,
      .operation_timeout_ms = 100U,
      .rx_quiet_time_ms = 2U,
      .pattern_seed = 0x87654321U,
      .batch_depth = 2U,
  };
  loop_result = LibXRTest::RunUartLoopbackTest(uart, batch_case, {tx.data(), tx.size()},
                                               {rx.data(), rx.size()});
  SELF_CHECK(loop_result.Passed());
  SELF_CHECK(loop_result.completed_rounds == 3U);
  SELF_CHECK(loop_result.verified_bytes == 384U);

  uart.corrupt_next_write_ = true;
  loop_result = LibXRTest::RunUartLoopbackTest(uart, single_case, {tx.data(), tx.size()},
                                               {rx.data(), rx.size()});
  SELF_CHECK(loop_result.failure == LibXRTest::UartLoopbackFailure::DATA_MISMATCH);
  SELF_CHECK(loop_result.mismatch_offset == 0U);

  uart.fail_next_write_ = true;
  loop_result = LibXRTest::RunUartLoopbackTest(uart, single_case, {tx.data(), tx.size()},
                                               {rx.data(), rx.size()});
  SELF_CHECK(loop_result.failure == LibXRTest::UartLoopbackFailure::WRITE_COMPLETE);
  SELF_CHECK(loop_result.error == LibXR::ErrorCode::FAILED);

  auto trailing_case = single_case;
  trailing_case.rounds = 1U;
  uart.append_extra_next_write_ = true;
  loop_result = LibXRTest::RunUartLoopbackTest(
      uart, trailing_case, {tx.data(), tx.size()}, {rx.data(), rx.size()});
  SELF_CHECK(loop_result.failure == LibXRTest::UartLoopbackFailure::UNEXPECTED_RX_DATA);
  SELF_CHECK(loop_result.unexpected_rx_bytes == 1U);

  auto continuous_case = single_case;
  continuous_case.rounds = 2U;
  uart.append_extra_next_write_ = true;
  loop_result = LibXRTest::RunUartLoopbackTest(
      uart, continuous_case, {tx.data(), tx.size()}, {rx.data(), rx.size()});
  SELF_CHECK(loop_result.failure == LibXRTest::UartLoopbackFailure::DATA_MISMATCH);
  SELF_CHECK(loop_result.failed_round == 1U);
  SELF_CHECK(loop_result.mismatch_offset == 0U);

  loop_result = LibXRTest::RunUartLoopbackTest(uart, batch_case, {tx.data(), 64U},
                                               {rx.data(), rx.size()});
  SELF_CHECK(loop_result.failure == LibXRTest::UartLoopbackFailure::INVALID_ARGUMENT);

  MemoryLoopbackUart small_rx_uart(64U);
  loop_result = LibXRTest::RunUartLoopbackTest(
      small_rx_uart, batch_case, {tx.data(), tx.size()}, {rx.data(), rx.size()});
  SELF_CHECK(loop_result.failure == LibXRTest::UartLoopbackFailure::INVALID_ARGUMENT);
  SELF_CHECK(loop_result.error == LibXR::ErrorCode::SIZE_ERR);

  MemoryLoopbackUart small_tx_uart(32768U, 64U);
  loop_result = LibXRTest::RunUartLoopbackTest(
      small_tx_uart, batch_case, {tx.data(), tx.size()}, {rx.data(), rx.size()});
  SELF_CHECK(loop_result.failure == LibXRTest::UartLoopbackFailure::INVALID_ARGUMENT);
  SELF_CHECK(loop_result.error == LibXR::ErrorCode::SIZE_ERR);

  constexpr size_t kStressMaxWriteSize = 4095U;
  constexpr uint32_t kAllStressLifecycleBits =
      static_cast<uint32_t>(LibXRTest::UartConcurrentConfigStressState::WRITER_READY) |
      static_cast<uint32_t>(
          LibXRTest::UartConcurrentConfigStressState::CONFIGURATOR_READY) |
      static_cast<uint32_t>(LibXRTest::UartConcurrentConfigStressState::START) |
      static_cast<uint32_t>(LibXRTest::UartConcurrentConfigStressState::STOP) |
      static_cast<uint32_t>(LibXRTest::UartConcurrentConfigStressState::WRITER_DONE) |
      static_cast<uint32_t>(
          LibXRTest::UartConcurrentConfigStressState::CONFIGURATOR_DONE);
  std::array<uint8_t, kStressMaxWriteSize * 2U> stress_tx{};
  const LibXRTest::UartConcurrentConfigStressCase stress_case = {
      .configurations = std::span<const LibXR::UART::Configuration>(kConfigs),
      .max_write_size = kStressMaxWriteSize,
      .duration_ms = 250U,
      .worker_ready_timeout_ms = 500U,
      .worker_stop_timeout_ms = 500U,
      .api_call_timeout_ms = 100U,
      .progress_timeout_ms = 200U,
      .retry_interval_ms = 0U,
      .rx_drain_interval_ms = 1U,
      .config_burst_size = 4U,
      .config_fairness_quiet_ms = 5U,
      .max_write_attempts = 250000U,
      .max_config_attempts = 250000U,
      .min_accepted_writes = 32U,
      .min_accepted_configurations = 4U,
      .min_observed_rx_bytes = 1U,
      .write_seed = 0x13579BDFU,
      .config_seed = 0x2468ACE0U,
  };
  LibXRTest::UartConcurrentConfigStressTest stress_test(
      uart, stress_case, {stress_tx.data(), stress_tx.size()});
  std::thread write_worker([&stress_test]() { stress_test.RunWriteWorker(); });
  std::thread config_worker([&stress_test]() { stress_test.RunConfigWorker(); });
  const auto stress_result = stress_test.RunCoordinator();
  write_worker.join();
  config_worker.join();

  SELF_CHECK(stress_result.Passed());
  SELF_CHECK(stress_result.final_state == kAllStressLifecycleBits);
  SELF_CHECK(stress_result.counters.write_attempts ==
             stress_result.counters.accepted_writes +
                 stress_result.counters.write_busy_retries +
                 stress_result.counters.write_full_retries);
  SELF_CHECK(stress_result.counters.config_attempts ==
             stress_result.counters.accepted_configurations +
                 stress_result.counters.config_busy_retries);
  SELF_CHECK(stress_result.counters.accepted_writes >= stress_case.min_accepted_writes);
  SELF_CHECK(stress_result.counters.accepted_configurations >=
             stress_case.min_accepted_configurations);
  SELF_CHECK(stress_result.counters.accepted_write_bytes >=
             stress_result.counters.accepted_writes);
  SELF_CHECK(stress_result.counters.accepted_write_bytes <=
             stress_result.counters.accepted_writes * stress_case.max_write_size);
  SELF_CHECK(stress_result.counters.required_boundary_length_mask == 0x1FFU);
  SELF_CHECK(stress_result.counters.completed_batch_depth_mask == 0x3U);
  SELF_CHECK(stress_result.counters.observed_rx_bytes > 0U);
  SELF_CHECK(stress_result.counters.max_write_call_us <=
             stress_case.api_call_timeout_ms * 1000U);
  SELF_CHECK(stress_result.counters.max_config_call_us <=
             stress_case.api_call_timeout_ms * 1000U);

  MemoryLoopbackUart missing_config_uart;
  auto ready_timeout_case = stress_case;
  ready_timeout_case.duration_ms = 20U;
  ready_timeout_case.worker_ready_timeout_ms = 20U;
  ready_timeout_case.worker_stop_timeout_ms = 20U;
  LibXRTest::UartConcurrentConfigStressTest missing_config_test(
      missing_config_uart, ready_timeout_case, {stress_tx.data(), stress_tx.size()});
  std::thread lone_write_worker([&missing_config_test]()
                                { missing_config_test.RunWriteWorker(); });
  const auto ready_timeout_result = missing_config_test.RunCoordinator();
  lone_write_worker.join();

  constexpr uint32_t kWriterStoppedLifecycleBits =
      static_cast<uint32_t>(LibXRTest::UartConcurrentConfigStressState::WRITER_READY) |
      static_cast<uint32_t>(LibXRTest::UartConcurrentConfigStressState::START) |
      static_cast<uint32_t>(LibXRTest::UartConcurrentConfigStressState::STOP) |
      static_cast<uint32_t>(LibXRTest::UartConcurrentConfigStressState::WRITER_DONE);
  constexpr uint32_t kMissingConfigLifecycleBits =
      static_cast<uint32_t>(
          LibXRTest::UartConcurrentConfigStressState::CONFIGURATOR_READY) |
      static_cast<uint32_t>(
          LibXRTest::UartConcurrentConfigStressState::CONFIGURATOR_DONE);
  SELF_CHECK(ready_timeout_result.failure ==
             LibXRTest::UartConcurrentConfigStressFailure::WORKER_READY_TIMEOUT);
  SELF_CHECK(ready_timeout_result.error == LibXR::ErrorCode::TIMEOUT);
  SELF_CHECK((ready_timeout_result.final_state & kWriterStoppedLifecycleBits) ==
             kWriterStoppedLifecycleBits);
  SELF_CHECK((ready_timeout_result.final_state & kMissingConfigLifecycleBits) == 0U);

  const LibXRTest::UartStressTrafficBarrierCase barrier_case = {
      .final_config = kConfigs[0],
      .config_timeout_ms = 100U,
      .marker_timeout_ms = 100U,
      .retry_interval_ms = 0U,
      .rx_quiet_time_ms = 2U,
  };
  std::array<uint8_t, 7U> barrier_rx{};
  const auto barrier_result = LibXRTest::RetireUartStressTraffic(
      uart, barrier_case, {barrier_rx.data(), barrier_rx.size()});
  SELF_CHECK(barrier_result.Passed());
  SELF_CHECK(barrier_result.unexpected_trailing_bytes == 0U);

  loop_result = LibXRTest::RunUartLoopbackTest(uart, single_case, {tx.data(), tx.size()},
                                               {rx.data(), rx.size()});
  SELF_CHECK(loop_result.Passed());
  SELF_CHECK(loop_result.completed_rounds == single_case.rounds);
  SELF_CHECK(loop_result.verified_bytes == single_case.frame_size * single_case.rounds);

  MemoryLoopbackUart trailing_barrier_uart;
  trailing_barrier_uart.append_extra_next_write_ = true;
  const auto trailing_barrier_result = LibXRTest::RetireUartStressTraffic(
      trailing_barrier_uart, barrier_case, {barrier_rx.data(), barrier_rx.size()});
  SELF_CHECK(trailing_barrier_result.failure ==
             LibXRTest::UartStressTrafficBarrierFailure::MARKER_TRAILING_DATA);
  SELF_CHECK(trailing_barrier_result.error == LibXR::ErrorCode::CHECK_ERR);
  SELF_CHECK(trailing_barrier_result.unexpected_trailing_bytes == 1U);

  MemoryLoopbackUart delayed_trailing_barrier_uart;
  auto delayed_barrier_case = barrier_case;
  delayed_barrier_case.rx_quiet_time_ms = 50U;
  LibXR::ErrorCode delayed_push_answer = LibXR::ErrorCode::FAILED;
  std::thread delayed_tail(
      [&delayed_trailing_barrier_uart, &delayed_push_answer]()
      {
        const auto write_deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(250U);
        while (delayed_trailing_barrier_uart.completed_writes_.load(
                   std::memory_order_acquire) == 0U &&
               std::chrono::steady_clock::now() < write_deadline)
        {
          std::this_thread::yield();
        }
        if (delayed_trailing_barrier_uart.completed_writes_.load(
                std::memory_order_acquire) == 0U)
        {
          return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10U));
        delayed_push_answer = delayed_trailing_barrier_uart.InjectRxByte(0xA5U);
      });
  const auto delayed_trailing_barrier_result = LibXRTest::RetireUartStressTraffic(
      delayed_trailing_barrier_uart, delayed_barrier_case,
      {barrier_rx.data(), barrier_rx.size()});
  delayed_tail.join();
  SELF_CHECK(delayed_push_answer == LibXR::ErrorCode::OK);
  SELF_CHECK(delayed_trailing_barrier_result.failure ==
             LibXRTest::UartStressTrafficBarrierFailure::MARKER_TRAILING_DATA);
  SELF_CHECK(delayed_trailing_barrier_result.error == LibXR::ErrorCode::CHECK_ERR);
  SELF_CHECK(delayed_trailing_barrier_result.unexpected_trailing_bytes == 1U);

  std::puts("uart hardware-test support selftest: PASS");
  return 0;
}
