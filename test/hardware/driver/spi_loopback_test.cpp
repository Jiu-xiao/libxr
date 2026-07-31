#include "driver/spi_loopback_test.hpp"

#include <array>
#include <cstring>
#include <limits>

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

void SetFailure(SpiLoopbackTestResult& result, SpiLoopbackFailure failure,
                LibXR::ErrorCode error, uint32_t round, size_t transfer_index,
                size_t transfer_size)
{
  result.failure = failure;
  result.error = error;
  result.failed_round = round;
  result.failed_transfer_index = transfer_index;
  result.failed_size = transfer_size;
}

bool BuffersArePairwiseDisjoint(std::span<const LibXR::RawData> buffers,
                                size_t active_size)
{
  std::array<uintptr_t, 4U> begins{};
  std::array<uintptr_t, 4U> ends{};
  if (buffers.size() > begins.size())
  {
    return false;
  }

  for (size_t i = 0U; i < buffers.size(); ++i)
  {
    begins[i] = reinterpret_cast<uintptr_t>(buffers[i].addr_);
    if (begins[i] > std::numeric_limits<uintptr_t>::max() - active_size)
    {
      return false;
    }
    ends[i] = begins[i] + active_size;
  }

  for (size_t i = 0U; i < buffers.size(); ++i)
  {
    for (size_t j = i + 1U; j < buffers.size(); ++j)
    {
      if (begins[i] < ends[j] && begins[j] < ends[i])
      {
        return false;
      }
    }
  }
  return true;
}

}  // namespace

SpiLoopbackTestResult RunSpiLoopbackTest(LibXR::SPI& spi,
                                         const SpiLoopbackTestCase& test_case,
                                         LibXR::RawData tx_scratch,
                                         LibXR::RawData rx_scratch)
{
  SpiLoopbackTestResult result;
  const uint64_t start_us = static_cast<uint64_t>(LibXR::Timebase::GetMicroseconds());

  size_t max_transfer_size = 0U;
  uint64_t bytes_per_round = 0U;
  bool valid_sizes = !test_case.transfer_sizes.empty();
  for (const size_t size : test_case.transfer_sizes)
  {
    valid_sizes = valid_sizes && size > 0U;
    if (size > std::numeric_limits<uint64_t>::max() - bytes_per_round)
    {
      valid_sizes = false;
    }
    else
    {
      bytes_per_round += size;
    }
    if (size > max_transfer_size)
    {
      max_transfer_size = size;
    }
  }

  const size_t transfer_count = test_case.transfer_sizes.size();
  const bool count_fits =
      transfer_count <= std::numeric_limits<uint32_t>::max() &&
      (transfer_count == 0U ||
       test_case.rounds <= std::numeric_limits<uint32_t>::max() / transfer_count);
  const bool byte_count_fits =
      bytes_per_round == 0U ||
      test_case.rounds <= std::numeric_limits<uint64_t>::max() / bytes_per_round;
  if (!valid_sizes || !count_fits || test_case.rounds == 0U || !byte_count_fits ||
      test_case.operation_timeout_ms == 0U || test_case.spi_config.double_buffer ||
      tx_scratch.addr_ == nullptr || rx_scratch.addr_ == nullptr)
  {
    SetFailure(result, SpiLoopbackFailure::INVALID_ARGUMENT, LibXR::ErrorCode::ARG_ERR,
               0U, 0U, max_transfer_size);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }

  if (tx_scratch.size_ < max_transfer_size || rx_scratch.size_ < max_transfer_size)
  {
    SetFailure(result, SpiLoopbackFailure::INVALID_ARGUMENT, LibXR::ErrorCode::SIZE_ERR,
               0U, 0U, max_transfer_size);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }

  const std::array<LibXR::RawData, 2U> scratch_buffers = {tx_scratch, rx_scratch};
  if (!BuffersArePairwiseDisjoint(scratch_buffers, max_transfer_size))
  {
    SetFailure(result, SpiLoopbackFailure::INVALID_ARGUMENT, LibXR::ErrorCode::ARG_ERR,
               0U, 0U, max_transfer_size);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }

  const auto config_ans = spi.SetConfig(test_case.spi_config);
  if (config_ans != LibXR::ErrorCode::OK)
  {
    SetFailure(result, SpiLoopbackFailure::SET_CONFIG, config_ans, 0U, 0U,
               max_transfer_size);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }

  const LibXR::RawData driver_rx = spi.GetRxBuffer();
  const LibXR::RawData driver_tx = spi.GetTxBuffer();
  if (driver_rx.addr_ == nullptr || driver_tx.addr_ == nullptr ||
      driver_rx.size_ < max_transfer_size || driver_tx.size_ < max_transfer_size)
  {
    SetFailure(result, SpiLoopbackFailure::INVALID_ARGUMENT, LibXR::ErrorCode::SIZE_ERR,
               0U, 0U, max_transfer_size);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }

  const std::array<LibXR::RawData, 4U> all_buffers = {tx_scratch, rx_scratch, driver_tx,
                                                      driver_rx};
  if (!BuffersArePairwiseDisjoint(all_buffers, max_transfer_size))
  {
    SetFailure(result, SpiLoopbackFailure::INVALID_ARGUMENT, LibXR::ErrorCode::ARG_ERR,
               0U, 0U, max_transfer_size);
    result.elapsed_us = ElapsedMicroseconds(start_us);
    return result;
  }

  auto* tx = static_cast<uint8_t*>(tx_scratch.addr_);
  auto* rx = static_cast<uint8_t*>(rx_scratch.addr_);

  for (uint32_t round = 0U; round < test_case.rounds; ++round)
  {
    for (size_t transfer_index = 0U; transfer_index < transfer_count; ++transfer_index)
    {
      const size_t transfer_size = test_case.transfer_sizes[transfer_index];
      const uint32_t seed = test_case.pattern_seed ^ (round * 0x9E3779B9U) ^
                            (static_cast<uint32_t>(transfer_index) * 0x85EBCA6BU) ^
                            static_cast<uint32_t>(transfer_size);
      FillPattern(tx, transfer_size, seed);
      for (size_t i = 0U; i < transfer_size; ++i)
      {
        rx[i] = static_cast<uint8_t>(~tx[i]);
      }

      LibXR::Semaphore transfer_sem(0U);
      LibXR::WriteOperation transfer_op(transfer_sem, test_case.operation_timeout_ms);
      const auto transfer_ans =
          spi.ReadAndWrite({rx, transfer_size}, {tx, transfer_size}, transfer_op, false);
      if (transfer_ans != LibXR::ErrorCode::OK)
      {
        SetFailure(result, SpiLoopbackFailure::TRANSFER, transfer_ans, round,
                   transfer_index, transfer_size);
        result.transfer_retirement_unconfirmed =
            transfer_ans == LibXR::ErrorCode::TIMEOUT;
        result.elapsed_us = ElapsedMicroseconds(start_us);
        return result;
      }

      if (std::memcmp(tx, rx, transfer_size) != 0)
      {
        for (size_t i = 0U; i < transfer_size; ++i)
        {
          if (tx[i] != rx[i])
          {
            result.mismatch_offset = i;
            break;
          }
        }
        SetFailure(result, SpiLoopbackFailure::DATA_MISMATCH, LibXR::ErrorCode::CHECK_ERR,
                   round, transfer_index, transfer_size);
        result.elapsed_us = ElapsedMicroseconds(start_us);
        return result;
      }

      result.completed_transfers++;
      result.verified_bytes += transfer_size;
    }
  }

  result.elapsed_us = ElapsedMicroseconds(start_us);
  return result;
}

const char* SpiLoopbackFailureName(SpiLoopbackFailure failure)
{
  switch (failure)
  {
    case SpiLoopbackFailure::NONE:
      return "NONE";
    case SpiLoopbackFailure::INVALID_ARGUMENT:
      return "INVALID_ARGUMENT";
    case SpiLoopbackFailure::SET_CONFIG:
      return "SET_CONFIG";
    case SpiLoopbackFailure::TRANSFER:
      return "TRANSFER";
    case SpiLoopbackFailure::DATA_MISMATCH:
      return "DATA_MISMATCH";
  }
  return "UNKNOWN";
}

}  // namespace LibXRTest
