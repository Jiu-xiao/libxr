#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "adc.hpp"
#include "libxr_type.hpp"

namespace LibXRTest
{

enum class AdcSamplingFailure : uint8_t
{
  NONE,
  INVALID_ARGUMENT,
  NON_FINITE,
  OUT_OF_RANGE,
  EXCESSIVE_SPAN,
};

struct AdcSamplingExpectation
{
  LibXR::ADC* channel = nullptr;
  float minimum_voltage = 0.0F;
  float maximum_voltage = 0.0F;
  float maximum_span = 0.0F;
};

struct AdcSamplingChannelStats
{
  float minimum_voltage = 0.0F;
  float maximum_voltage = 0.0F;
  uint32_t samples = 0U;
};

struct AdcSamplingTestCase
{
  std::span<const AdcSamplingExpectation> channels{};
  uint32_t samples_per_channel = 0U;
  uint32_t sample_interval_ms = 0U;
};

struct AdcSamplingTestResult
{
  AdcSamplingFailure failure = AdcSamplingFailure::NONE;
  LibXR::ErrorCode error = LibXR::ErrorCode::OK;
  size_t failed_channel = SIZE_MAX;
  uint32_t failed_sample = UINT32_MAX;
  float observed_voltage = 0.0F;
  uint32_t completed_reads = 0U;

  [[nodiscard]] bool Passed() const { return failure == AdcSamplingFailure::NONE; }
};

/**
 * @brief Sample ADC channels repeatedly and validate caller-supplied voltage bounds.
 *
 * Each expectation owns no channel; every channel object and the statistics span must
 * remain valid until the function returns. Bounds and maximum span are inclusive. A
 * zero interval performs consecutive reads without sleeping.
 *
 * Call only from task or main context after `PlatformInit()` when a nonzero interval is
 * requested. The ADC backend must already be configured and dedicated to the test.
 */
AdcSamplingTestResult RunAdcSamplingTest(
    const AdcSamplingTestCase& test_case,
    std::span<AdcSamplingChannelStats> channel_stats);

const char* AdcSamplingFailureName(AdcSamplingFailure failure);

}  // namespace LibXRTest
