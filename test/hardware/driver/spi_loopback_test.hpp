#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "libxr_def.hpp"
#include "libxr_type.hpp"
#include "spi.hpp"

namespace LibXRTest
{

enum class SpiLoopbackFailure : uint8_t
{
  NONE,
  INVALID_ARGUMENT,
  SET_CONFIG,
  TRANSFER,
  DATA_MISMATCH,
};

struct SpiLoopbackTestCase
{
  LibXR::SPI::Configuration spi_config{};
  std::span<const size_t> transfer_sizes{};
  uint32_t rounds = 0U;
  uint32_t operation_timeout_ms = 0U;
  uint32_t pattern_seed = 0U;
};

struct SpiLoopbackTestResult
{
  SpiLoopbackFailure failure = SpiLoopbackFailure::NONE;
  LibXR::ErrorCode error = LibXR::ErrorCode::OK;
  uint32_t completed_transfers = 0U;
  uint32_t failed_round = UINT32_MAX;
  size_t failed_transfer_index = SIZE_MAX;
  size_t failed_size = 0U;
  uint64_t verified_bytes = 0U;
  size_t mismatch_offset = SIZE_MAX;
  uint64_t elapsed_us = 0U;
  bool transfer_retirement_unconfirmed = false;

  [[nodiscard]] bool Passed() const { return failure == SpiLoopbackFailure::NONE; }
};

/**
 * @brief Run deterministic full-duplex SPI loopback transfers.
 *
 * Each transfer uses a BLOCK operation. A DMA backend therefore returns only after its
 * terminal callback has copied the received bytes, while a polling backend completes in
 * the call itself. The function stops at the first failure; after a timeout, the caller
 * must not reuse the SPI object until the backend has independently retired the transfer.
 *
 * The SPI must be dedicated to this test. Both caller-owned scratch buffers and both
 * driver-owned transfer buffers must hold the largest requested transfer, and all four
 * active ranges must be pairwise disjoint. Software double buffering is deliberately
 * rejected because this helper submits one record at a time and compares the active
 * receive buffer immediately after completion.
 *
 * If a BLOCK operation times out, its backend may still retire DMA into `rx_scratch`.
 * The caller must therefore keep both scratch buffers alive and unmodified, and must not
 * reuse the SPI object, until that transfer has independently reached a terminal state.
 *
 * Call only from task or main context after `PlatformInit()` and timebase setup.
 */
SpiLoopbackTestResult RunSpiLoopbackTest(LibXR::SPI& spi,
                                         const SpiLoopbackTestCase& test_case,
                                         LibXR::RawData tx_scratch,
                                         LibXR::RawData rx_scratch);

const char* SpiLoopbackFailureName(SpiLoopbackFailure failure);

}  // namespace LibXRTest
