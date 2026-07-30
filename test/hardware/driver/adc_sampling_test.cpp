#include "driver/adc_sampling_test.hpp"

#include <cmath>
#include <limits>

#include "libxr.hpp"

namespace LibXRTest
{
namespace
{

AdcSamplingTestResult InvalidArgument()
{
  return {
      .failure = AdcSamplingFailure::INVALID_ARGUMENT,
      .error = LibXR::ErrorCode::ARG_ERR,
  };
}

bool ValidExpectation(const AdcSamplingExpectation& expectation)
{
  return expectation.channel != nullptr && std::isfinite(expectation.minimum_voltage) &&
         std::isfinite(expectation.maximum_voltage) &&
         std::isfinite(expectation.maximum_span) &&
         expectation.minimum_voltage <= expectation.maximum_voltage &&
         expectation.maximum_span >= 0.0F;
}

}  // namespace

AdcSamplingTestResult RunAdcSamplingTest(const AdcSamplingTestCase& test_case,
                                         std::span<AdcSamplingChannelStats> channel_stats)
{
  if (test_case.channels.empty() || test_case.samples_per_channel == 0U ||
      channel_stats.size() != test_case.channels.size())
  {
    return InvalidArgument();
  }

  for (const auto& expectation : test_case.channels)
  {
    if (!ValidExpectation(expectation))
    {
      return InvalidArgument();
    }
  }

  for (auto& stats : channel_stats)
  {
    stats.minimum_voltage = std::numeric_limits<float>::infinity();
    stats.maximum_voltage = -std::numeric_limits<float>::infinity();
    stats.samples = 0U;
  }

  AdcSamplingTestResult result{};
  for (uint32_t sample = 0U; sample < test_case.samples_per_channel; ++sample)
  {
    for (size_t channel = 0U; channel < test_case.channels.size(); ++channel)
    {
      const auto& expectation = test_case.channels[channel];
      auto& stats = channel_stats[channel];
      const float voltage = expectation.channel->Read();

      result.completed_reads++;
      stats.samples++;
      if (!std::isfinite(voltage))
      {
        result.failure = AdcSamplingFailure::NON_FINITE;
        result.error = LibXR::ErrorCode::CHECK_ERR;
        result.failed_channel = channel;
        result.failed_sample = sample;
        result.observed_voltage = voltage;
        return result;
      }

      if (voltage < stats.minimum_voltage)
      {
        stats.minimum_voltage = voltage;
      }
      if (voltage > stats.maximum_voltage)
      {
        stats.maximum_voltage = voltage;
      }

      if (voltage < expectation.minimum_voltage || voltage > expectation.maximum_voltage)
      {
        result.failure = AdcSamplingFailure::OUT_OF_RANGE;
        result.error = LibXR::ErrorCode::CHECK_ERR;
        result.failed_channel = channel;
        result.failed_sample = sample;
        result.observed_voltage = voltage;
        return result;
      }
    }

    if (test_case.sample_interval_ms > 0U && sample + 1U < test_case.samples_per_channel)
    {
      LibXR::Thread::Sleep(test_case.sample_interval_ms);
    }
  }

  for (size_t channel = 0U; channel < test_case.channels.size(); ++channel)
  {
    const float span =
        channel_stats[channel].maximum_voltage - channel_stats[channel].minimum_voltage;
    if (span > test_case.channels[channel].maximum_span)
    {
      result.failure = AdcSamplingFailure::EXCESSIVE_SPAN;
      result.error = LibXR::ErrorCode::CHECK_ERR;
      result.failed_channel = channel;
      result.failed_sample = test_case.samples_per_channel - 1U;
      result.observed_voltage = span;
      return result;
    }
  }

  return result;
}

const char* AdcSamplingFailureName(AdcSamplingFailure failure)
{
  switch (failure)
  {
    case AdcSamplingFailure::NONE:
      return "NONE";
    case AdcSamplingFailure::INVALID_ARGUMENT:
      return "INVALID_ARGUMENT";
    case AdcSamplingFailure::NON_FINITE:
      return "NON_FINITE";
    case AdcSamplingFailure::OUT_OF_RANGE:
      return "OUT_OF_RANGE";
    case AdcSamplingFailure::EXCESSIVE_SPAN:
      return "EXCESSIVE_SPAN";
    default:
      return "UNKNOWN";
  }
}

}  // namespace LibXRTest
