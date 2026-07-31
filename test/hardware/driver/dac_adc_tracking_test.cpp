#include "driver/dac_adc_tracking_test.hpp"

#include <cmath>
#include <limits>

#include "libxr.hpp"

namespace LibXRTest
{
namespace
{

DacAdcTrackingTestResult InvalidArgument()
{
  return {
      .failure = DacAdcTrackingFailure::INVALID_ARGUMENT,
      .error = LibXR::ErrorCode::ARG_ERR,
  };
}

}  // namespace

DacAdcTrackingTestResult RunDacAdcTrackingTest(const DacAdcTrackingTestCase& test_case)
{
  if (test_case.pairs.empty() || test_case.point_count == 0U ||
      !std::isfinite(test_case.tolerance) || test_case.tolerance < 0.0F ||
      test_case.point_count >
          std::numeric_limits<size_t>::max() / test_case.pairs.size() ||
      test_case.point_major_setpoints.size() !=
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
  for (const float setpoint : test_case.point_major_setpoints)
  {
    if (!std::isfinite(setpoint))
    {
      return InvalidArgument();
    }
  }

  DacAdcTrackingTestResult result{};
  for (size_t point = 0U; point < test_case.point_count; ++point)
  {
    for (size_t pair = 0U; pair < test_case.pairs.size(); ++pair)
    {
      const float setpoint =
          test_case.point_major_setpoints[point * test_case.pairs.size() + pair];
      const LibXR::ErrorCode error = test_case.pairs[pair].output->Write(setpoint);
      if (error != LibXR::ErrorCode::OK)
      {
        result.failure = DacAdcTrackingFailure::DAC_WRITE;
        result.error = error;
        result.failed_point = point;
        result.failed_pair = pair;
        result.expected_voltage = setpoint;
        return result;
      }
    }

    if (test_case.settle_time_ms > 0U)
    {
      LibXR::Thread::Sleep(test_case.settle_time_ms);
    }

    for (size_t pair = 0U; pair < test_case.pairs.size(); ++pair)
    {
      const float expected =
          test_case.point_major_setpoints[point * test_case.pairs.size() + pair];
      const float observed = test_case.pairs[pair].feedback->Read();
      result.completed_comparisons++;
      if (!std::isfinite(observed))
      {
        result.failure = DacAdcTrackingFailure::NON_FINITE;
        result.error = LibXR::ErrorCode::CHECK_ERR;
        result.failed_point = point;
        result.failed_pair = pair;
        result.expected_voltage = expected;
        result.observed_voltage = observed;
        return result;
      }

      const float error = std::fabs(observed - expected);
      if (error > result.maximum_error)
      {
        result.maximum_error = error;
      }
      if (error > test_case.tolerance)
      {
        result.failure = DacAdcTrackingFailure::VOLTAGE_MISMATCH;
        result.error = LibXR::ErrorCode::CHECK_ERR;
        result.failed_point = point;
        result.failed_pair = pair;
        result.expected_voltage = expected;
        result.observed_voltage = observed;
        return result;
      }
    }
  }

  return result;
}

const char* DacAdcTrackingFailureName(DacAdcTrackingFailure failure)
{
  switch (failure)
  {
    case DacAdcTrackingFailure::NONE:
      return "NONE";
    case DacAdcTrackingFailure::INVALID_ARGUMENT:
      return "INVALID_ARGUMENT";
    case DacAdcTrackingFailure::DAC_WRITE:
      return "DAC_WRITE";
    case DacAdcTrackingFailure::NON_FINITE:
      return "NON_FINITE";
    case DacAdcTrackingFailure::VOLTAGE_MISMATCH:
      return "VOLTAGE_MISMATCH";
    default:
      return "UNKNOWN";
  }
}

}  // namespace LibXRTest
