#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "gpio.hpp"
#include "libxr_type.hpp"
#include "pwm.hpp"

namespace LibXRTest
{

enum class PwmGpioLoopbackFailure : uint8_t
{
  NONE,
  INVALID_ARGUMENT,
  CONFIGURE,
  SET_DUTY,
  ENABLE,
  DUTY_MISMATCH,
  FREQUENCY_MISMATCH,
  DISABLE,
};

struct PwmGpioPair
{
  LibXR::PWM* output = nullptr;
  LibXR::GPIO* feedback = nullptr;
};

struct PwmGpioLoopbackTestCase
{
  std::span<const PwmGpioPair> pairs{};
  std::span<const float> point_major_duties{};
  size_t point_count = 0U;
  uint32_t frequency_hz = 0U;
  uint32_t settle_time_us = 0U;
  uint32_t samples_per_point = 0U;
  uint32_t minimum_sample_interval_us = 0U;
  uint32_t sample_interval_jitter_us = 0U;
  float duty_tolerance = 0.0F;
  float frequency_tolerance = 0.0F;
  uint32_t random_seed = 1U;
};

struct PwmGpioLoopbackTestResult
{
  PwmGpioLoopbackFailure failure = PwmGpioLoopbackFailure::NONE;
  LibXR::ErrorCode error = LibXR::ErrorCode::OK;
  size_t failed_point = SIZE_MAX;
  size_t failed_pair = SIZE_MAX;
  float expected_duty = 0.0F;
  float observed_duty = 0.0F;
  float observed_frequency_hz = 0.0F;
  float maximum_duty_error = 0.0F;
  float maximum_frequency_error = 0.0F;
  uint32_t completed_comparisons = 0U;
  uint32_t total_samples = 0U;
  uint32_t minimum_transitions = UINT32_MAX;

  [[nodiscard]] bool Passed() const { return failure == PwmGpioLoopbackFailure::NONE; }
};

/**
 * @brief Verify PWM outputs through wired GPIO feedback.
 *
 * Duties are point-major. Sampling intervals are deterministically jittered so
 * the test cannot phase-lock to the PWM period. The caller owns all objects and
 * must dedicate the pins and PWM channels to the test. To observe every edge,
 * the maximum sample interval must be shorter than the smallest expected high
 * or low pulse. Static 0% and 100% points validate duty only.
 */
PwmGpioLoopbackTestResult RunPwmGpioLoopbackTest(
    const PwmGpioLoopbackTestCase& test_case);

const char* PwmGpioLoopbackFailureName(PwmGpioLoopbackFailure failure);

}  // namespace LibXRTest
