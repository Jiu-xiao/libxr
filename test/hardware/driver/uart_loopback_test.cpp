#include "driver/uart_loopback_test.hpp"

#include <cstring>

#include "libxr.hpp"

namespace LibXRTest
{
namespace
{

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

uint64_t ElapsedMicroseconds(uint64_t start_us)
{
  return static_cast<uint64_t>(LibXR::Timebase::GetMicroseconds()) - start_us;
}

bool TimedOut(uint32_t start_ms, uint32_t timeout_ms)
{
  const uint32_t now_ms = static_cast<uint32_t>(LibXR::Timebase::GetMilliseconds());
  return static_cast<uint32_t>(now_ms - start_ms) >= timeout_ms;
}

LibXR::ErrorCode SetConfigWithRetry(LibXR::UART& uart, LibXR::UART::Configuration config,
                                    uint32_t timeout_ms, uint32_t retry_interval_ms,
                                    uint32_t* busy_retries = nullptr)
{
  const uint32_t start_ms = static_cast<uint32_t>(LibXR::Timebase::GetMilliseconds());

  while (true)
  {
    const auto ans = uart.SetConfig(config);
    if (ans != LibXR::ErrorCode::BUSY)
    {
      return ans;
    }

    if (busy_retries != nullptr)
    {
      (*busy_retries)++;
    }
    if (TimedOut(start_ms, timeout_ms))
    {
      return LibXR::ErrorCode::TIMEOUT;
    }

    if (retry_interval_ms == 0U)
    {
      LibXR::Thread::Yield();
    }
    else
    {
      LibXR::Thread::Sleep(retry_interval_ms);
    }
  }
}

void SetFailure(UartLoopbackTestResult& result, UartLoopbackFailure failure,
                LibXR::ErrorCode error, uint32_t round, uint8_t batch)
{
  result.failure = failure;
  result.error = error;
  result.failed_round = round;
  result.failed_batch = batch;
}

}  // namespace

UartLoopbackTestResult RunUartLoopbackTest(LibXR::UART& uart,
                                           const UartLoopbackTestCase& test_case,
                                           LibXR::RawData tx_scratch,
                                           LibXR::RawData rx_scratch)
{
  UartLoopbackTestResult result;
  const uint64_t start_us = static_cast<uint64_t>(LibXR::Timebase::GetMicroseconds());

  const bool valid_batch = test_case.batch_depth == 1U || test_case.batch_depth == 2U;
  const size_t required_size =
      test_case.frame_size * static_cast<size_t>(test_case.batch_depth);
  if (uart.read_port_ == nullptr || uart.write_port_ == nullptr ||
      !uart.read_port_->Readable() || !uart.write_port_->Writable() ||
      tx_scratch.addr_ == nullptr || rx_scratch.addr_ == nullptr ||
      test_case.frame_size == 0U || test_case.rounds == 0U ||
      test_case.operation_timeout_ms == 0U || test_case.rx_quiet_time_ms == 0U ||
      !valid_batch || required_size / test_case.batch_depth != test_case.frame_size ||
      tx_scratch.size_ < required_size || rx_scratch.size_ < required_size)
  {
    SetFailure(result, UartLoopbackFailure::INVALID_ARGUMENT, LibXR::ErrorCode::ARG_ERR,
               0U, 0U);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }

  const auto config_ans =
      SetConfigWithRetry(uart, test_case.uart_config, test_case.operation_timeout_ms, 1U);
  if (config_ans != LibXR::ErrorCode::OK)
  {
    SetFailure(result, UartLoopbackFailure::SET_CONFIG, config_ans, 0U, 0U);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }

  auto* tx = static_cast<uint8_t*>(tx_scratch.addr_);
  auto* rx = static_cast<uint8_t*>(rx_scratch.addr_);
  LibXR::Semaphore write_sem(0U);
  LibXR::Semaphore read_sem(0U);
  LibXR::WriteOperation blocking_write(write_sem, test_case.operation_timeout_ms);
  LibXR::ReadOperation blocking_read(read_sem, test_case.operation_timeout_ms);

  const auto clear_ans = uart.read_port_->ClearQueuedData();
  if (clear_ans != LibXR::ErrorCode::OK)
  {
    SetFailure(result, UartLoopbackFailure::CLEAR_RX, clear_ans, 0U, 0U);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }
  if (uart.read_port_->EmptySize() < required_size ||
      uart.write_port_->EmptySize() < required_size)
  {
    SetFailure(result, UartLoopbackFailure::INVALID_ARGUMENT, LibXR::ErrorCode::SIZE_ERR,
               0U, 0U);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }

  for (uint32_t round = 0U; round < test_case.rounds; ++round)
  {
    for (uint8_t batch = 0U; batch < test_case.batch_depth; ++batch)
    {
      const size_t offset = static_cast<size_t>(batch) * test_case.frame_size;
      const uint32_t seed = test_case.pattern_seed ^ (round * 0x9E3779B9U) ^
                            (static_cast<uint32_t>(batch) * 0x85EBCA6BU);
      FillPattern(tx + offset, test_case.frame_size, seed);
    }
    std::memset(rx, 0, required_size);

    if (test_case.batch_depth == 1U)
    {
      const auto write_ans =
          uart.Write({tx, test_case.frame_size}, blocking_write, false);
      if (write_ans != LibXR::ErrorCode::OK)
      {
        SetFailure(result, UartLoopbackFailure::WRITE_COMPLETE, write_ans, round, 0U);
        break;
      }
    }
    else
    {
      LibXR::WriteOperation operations[2]{};

      for (uint8_t batch = 0U; batch < 2U; ++batch)
      {
        const size_t offset = static_cast<size_t>(batch) * test_case.frame_size;
        const auto write_ans =
            uart.Write({tx + offset, test_case.frame_size}, operations[batch], false);
        if (write_ans != LibXR::ErrorCode::OK)
        {
          SetFailure(result, UartLoopbackFailure::WRITE_SUBMIT, write_ans, round, batch);
          break;
        }
      }
      if (!result.Passed())
      {
        break;
      }
    }

    const auto read_ans = uart.Read({rx, required_size}, blocking_read, false);
    if (read_ans != LibXR::ErrorCode::OK)
    {
      SetFailure(result, UartLoopbackFailure::READ, read_ans, round, 0U);
      break;
    }

    if (std::memcmp(tx, rx, required_size) != 0)
    {
      for (size_t i = 0U; i < required_size; ++i)
      {
        if (tx[i] != rx[i])
        {
          result.mismatch_offset = i;
          break;
        }
      }
      SetFailure(result, UartLoopbackFailure::DATA_MISMATCH, LibXR::ErrorCode::CHECK_ERR,
                 round,
                 static_cast<uint8_t>(result.mismatch_offset / test_case.frame_size));
      break;
    }

    result.completed_rounds++;
    result.verified_bytes += required_size;
  }

  if (result.Passed())
  {
    const uint32_t quiet_start_ms =
        static_cast<uint32_t>(LibXR::Timebase::GetMilliseconds());
    while (true)
    {
      const size_t queued = uart.read_port_->Size();
      if (queued != 0U)
      {
        result.unexpected_rx_bytes = queued;
        SetFailure(result, UartLoopbackFailure::UNEXPECTED_RX_DATA,
                   LibXR::ErrorCode::CHECK_ERR, test_case.rounds, UINT8_MAX);
        break;
      }
      if (TimedOut(quiet_start_ms, test_case.rx_quiet_time_ms))
      {
        break;
      }
      LibXR::Thread::Sleep(1U);
    }
  }

  result.elapsed_us = ElapsedMicroseconds(start_us);
  return result;
}

UartIdleReconfigureTestResult RunUartIdleReconfigureTest(
    LibXR::UART& uart, const UartIdleReconfigureTestCase& test_case)
{
  UartIdleReconfigureTestResult result;
  const uint64_t start_us = static_cast<uint64_t>(LibXR::Timebase::GetMicroseconds());

  if (test_case.configurations.empty() || test_case.transitions == 0U ||
      test_case.transition_timeout_ms == 0U)
  {
    result.error = LibXR::ErrorCode::ARG_ERR;
    result.failed_transition = 0U;
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }

  for (uint32_t transition = 0U; transition < test_case.transitions; ++transition)
  {
    const auto config =
        test_case.configurations[transition % test_case.configurations.size()];
    const auto ans =
        SetConfigWithRetry(uart, config, test_case.transition_timeout_ms,
                           test_case.retry_interval_ms, &result.busy_retries);
    if (ans == LibXR::ErrorCode::OK)
    {
      result.completed_transitions++;
      continue;
    }

    result.error = ans;
    result.failed_transition = transition;
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }

  result.elapsed_us = ElapsedMicroseconds(start_us);
  return result;
}

const char* UartLoopbackFailureName(UartLoopbackFailure failure)
{
  switch (failure)
  {
    case UartLoopbackFailure::NONE:
      return "NONE";
    case UartLoopbackFailure::INVALID_ARGUMENT:
      return "INVALID_ARGUMENT";
    case UartLoopbackFailure::CLEAR_RX:
      return "CLEAR_RX";
    case UartLoopbackFailure::SET_CONFIG:
      return "SET_CONFIG";
    case UartLoopbackFailure::WRITE_SUBMIT:
      return "WRITE_SUBMIT";
    case UartLoopbackFailure::WRITE_COMPLETE:
      return "WRITE_COMPLETE";
    case UartLoopbackFailure::READ:
      return "READ";
    case UartLoopbackFailure::DATA_MISMATCH:
      return "DATA_MISMATCH";
    case UartLoopbackFailure::UNEXPECTED_RX_DATA:
      return "UNEXPECTED_RX_DATA";
  }
  return "UNKNOWN";
}

}  // namespace LibXRTest
