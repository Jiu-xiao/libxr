#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "adc.hpp"
#include "dac.hpp"
#include "libxr_type.hpp"

namespace LibXRTest
{

enum class DacAdcTrackingFailure : uint8_t
{
  NONE,
  INVALID_ARGUMENT,
  DAC_WRITE,
  NON_FINITE,
  VOLTAGE_MISMATCH,
};

struct DacAdcPair
{
  LibXR::DAC* output = nullptr;
  LibXR::ADC* feedback = nullptr;
};

struct DacAdcTrackingTestCase
{
  std::span<const DacAdcPair> pairs{};
  std::span<const float> point_major_setpoints{};
  size_t point_count = 0U;
  uint32_t settle_time_ms = 0U;
  float tolerance = 0.0F;
};

struct DacAdcTrackingTestResult
{
  DacAdcTrackingFailure failure = DacAdcTrackingFailure::NONE;
  LibXR::ErrorCode error = LibXR::ErrorCode::OK;
  size_t failed_point = SIZE_MAX;
  size_t failed_pair = SIZE_MAX;
  float expected_voltage = 0.0F;
  float observed_voltage = 0.0F;
  float maximum_error = 0.0F;
  uint32_t completed_comparisons = 0U;

  [[nodiscard]] bool Passed() const { return failure == DacAdcTrackingFailure::NONE; }
};

/**
 * @brief Drive DAC outputs through a point table and compare ADC feedback.
 *
 * Setpoints are point-major: all pair values for point zero, followed by all pair values
 * for point one. Bounds are inclusive. Pair objects and the setpoint span remain owned by
 * the caller. The peripherals must be dedicated to this test.
 *
 * Call only from task or main context after `PlatformInit()` when a nonzero settling time
 * is requested.
 */
DacAdcTrackingTestResult RunDacAdcTrackingTest(const DacAdcTrackingTestCase& test_case);

const char* DacAdcTrackingFailureName(DacAdcTrackingFailure failure);

}  // namespace LibXRTest
