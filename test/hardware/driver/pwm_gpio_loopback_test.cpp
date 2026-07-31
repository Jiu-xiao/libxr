#include "driver/pwm_gpio_loopback_test.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "libxr.hpp"

namespace LibXRTest
{
namespace
{

PwmGpioLoopbackTestResult InvalidArgument()
{
  return {
      .failure = PwmGpioLoopbackFailure::INVALID_ARGUMENT,
      .error = LibXR::ErrorCode::ARG_ERR,
  };
}

void DisableEnabled(const std::span<const PwmGpioPair> pairs, size_t enabled_count,
                    PwmGpioLoopbackTestResult& result)
{
  while (enabled_count > 0U)
  {
    --enabled_count;
    const LibXR::ErrorCode error = pairs[enabled_count].output->Disable();
    if (error != LibXR::ErrorCode::OK && result.Passed())
    {
      result.failure = PwmGpioLoopbackFailure::DISABLE;
      result.error = error;
      result.failed_pair = enabled_count;
    }
  }
}

}  // namespace

PwmGpioLoopbackTestResult RunPwmGpioLoopbackTest(const PwmGpioLoopbackTestCase& test_case)
{
  if (test_case.pairs.empty() || test_case.point_count == 0U ||
      test_case.frequency_hz == 0U || test_case.samples_per_point == 0U ||
      test_case.minimum_sample_interval_us == 0U ||
      test_case.sample_interval_jitter_us == UINT32_MAX ||
      test_case.minimum_sample_interval_us >
          UINT32_MAX - test_case.sample_interval_jitter_us ||
      !std::isfinite(test_case.duty_tolerance) || test_case.duty_tolerance < 0.0F ||
      !std::isfinite(test_case.frequency_tolerance) ||
      test_case.frequency_tolerance < 0.0F ||
      test_case.point_count >
          std::numeric_limits<size_t>::max() / test_case.pairs.size() ||
      test_case.point_major_duties.size() !=
          test_case.point_count * test_case.pairs.size())
  {
    return InvalidArgument();
  }

  for (const auto& pair : test_case.pairs)
  {
    if (pair.output == nullptr || pair.feedback == nullptr)
    {
      return InvalidArgument();
    }
  }
  for (const float duty : test_case.point_major_duties)
  {
    if (!std::isfinite(duty) || duty < 0.0F || duty > 1.0F)
    {
      return InvalidArgument();
    }
  }

  PwmGpioLoopbackTestResult result{};
  for (size_t pair = 0U; pair < test_case.pairs.size(); ++pair)
  {
    const LibXR::ErrorCode error =
        test_case.pairs[pair].output->SetConfig({.frequency = test_case.frequency_hz});
    if (error != LibXR::ErrorCode::OK)
    {
      result.failure = PwmGpioLoopbackFailure::CONFIGURE;
      result.error = error;
      result.failed_pair = pair;
      return result;
    }
  }

  for (size_t pair = 0U; pair < test_case.pairs.size(); ++pair)
  {
    const float duty = test_case.point_major_duties[pair];
    const LibXR::ErrorCode error = test_case.pairs[pair].output->SetDutyCycle(duty);
    if (error != LibXR::ErrorCode::OK)
    {
      result.failure = PwmGpioLoopbackFailure::SET_DUTY;
      result.error = error;
      result.failed_pair = pair;
      result.expected_duty = duty;
      return result;
    }
  }

  size_t enabled_count = 0U;
  for (; enabled_count < test_case.pairs.size(); ++enabled_count)
  {
    const LibXR::ErrorCode error = test_case.pairs[enabled_count].output->Enable();
    if (error != LibXR::ErrorCode::OK)
    {
      result.failure = PwmGpioLoopbackFailure::ENABLE;
      result.error = error;
      result.failed_pair = enabled_count;
      DisableEnabled(test_case.pairs, enabled_count, result);
      return result;
    }
  }

  uint32_t random = test_case.random_seed;

  for (size_t point = 0U; point < test_case.point_count && result.Passed(); ++point)
  {
    for (size_t pair = 0U; pair < test_case.pairs.size(); ++pair)
    {
      const float duty =
          test_case.point_major_duties[point * test_case.pairs.size() + pair];
      const LibXR::ErrorCode error = test_case.pairs[pair].output->SetDutyCycle(duty);
      if (error != LibXR::ErrorCode::OK)
      {
        result.failure = PwmGpioLoopbackFailure::SET_DUTY;
        result.error = error;
        result.failed_point = point;
        result.failed_pair = pair;
        result.expected_duty = duty;
        break;
      }
    }
    if (!result.Passed())
    {
      break;
    }

    if (test_case.settle_time_us > 0U)
    {
      LibXR::Timebase::DelayMicroseconds(test_case.settle_time_us);
    }
    for (size_t pair = 0U; pair < test_case.pairs.size(); ++pair)
    {
      uint32_t high_count = 0U;
      uint32_t transition_count = 0U;
      uint32_t rising_count = 0U;
      bool previous = test_case.pairs[pair].feedback->Read();
      const uint64_t start_us = static_cast<uint64_t>(LibXR::Timebase::GetMicroseconds());
      for (uint32_t sample = 0U; sample < test_case.samples_per_point; ++sample)
      {
        random = random * 1664525U + 1013904223U;
        uint32_t delay_us = test_case.minimum_sample_interval_us;
        if (test_case.sample_interval_jitter_us > 0U)
        {
          delay_us += random % (test_case.sample_interval_jitter_us + 1U);
        }
        LibXR::Timebase::DelayMicroseconds(delay_us);
        const bool level = test_case.pairs[pair].feedback->Read();
        high_count += level ? 1U : 0U;
        if (level != previous)
        {
          transition_count++;
          rising_count += level ? 1U : 0U;
          previous = level;
        }
      }
      const uint64_t elapsed_us =
          static_cast<uint64_t>(LibXR::Timebase::GetMicroseconds()) - start_us;
      const float expected =
          test_case.point_major_duties[point * test_case.pairs.size() + pair];
      const float observed = static_cast<float>(high_count) /
                             static_cast<float>(test_case.samples_per_point);
      const float duty_error = std::fabs(observed - expected);
      const float observed_frequency =
          elapsed_us == 0U ? 0.0F
                           : static_cast<float>(rising_count) * 1000000.0F /
                                 static_cast<float>(elapsed_us);
      const float frequency_error =
          std::fabs(observed_frequency - static_cast<float>(test_case.frequency_hz)) /
          static_cast<float>(test_case.frequency_hz);
      const bool frequency_observable = expected > 0.0F && expected < 1.0F;

      result.completed_comparisons++;
      result.total_samples += test_case.samples_per_point;
      result.maximum_duty_error = std::max(result.maximum_duty_error, duty_error);
      if (frequency_observable)
      {
        result.maximum_frequency_error =
            std::max(result.maximum_frequency_error, frequency_error);
      }
      result.minimum_transitions = std::min(result.minimum_transitions, transition_count);
      if (duty_error > test_case.duty_tolerance)
      {
        result.failure = PwmGpioLoopbackFailure::DUTY_MISMATCH;
      }
      else if (frequency_observable && frequency_error > test_case.frequency_tolerance)
      {
        result.failure = PwmGpioLoopbackFailure::FREQUENCY_MISMATCH;
      }
      if (!result.Passed())
      {
        result.error = LibXR::ErrorCode::CHECK_ERR;
        result.failed_point = point;
        result.failed_pair = pair;
        result.expected_duty = expected;
        result.observed_duty = observed;
        result.observed_frequency_hz = observed_frequency;
        break;
      }
    }
  }

  DisableEnabled(test_case.pairs, enabled_count, result);
  return result;
}

const char* PwmGpioLoopbackFailureName(PwmGpioLoopbackFailure failure)
{
  switch (failure)
  {
    case PwmGpioLoopbackFailure::NONE:
      return "NONE";
    case PwmGpioLoopbackFailure::INVALID_ARGUMENT:
      return "INVALID_ARGUMENT";
    case PwmGpioLoopbackFailure::CONFIGURE:
      return "CONFIGURE";
    case PwmGpioLoopbackFailure::SET_DUTY:
      return "SET_DUTY";
    case PwmGpioLoopbackFailure::ENABLE:
      return "ENABLE";
    case PwmGpioLoopbackFailure::DUTY_MISMATCH:
      return "DUTY_MISMATCH";
    case PwmGpioLoopbackFailure::FREQUENCY_MISMATCH:
      return "FREQUENCY_MISMATCH";
    case PwmGpioLoopbackFailure::DISABLE:
      return "DISABLE";
    default:
      return "UNKNOWN";
  }
}

}  // namespace LibXRTest
