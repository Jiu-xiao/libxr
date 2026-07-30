#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include "driver/dac_adc_tracking_test.hpp"

namespace
{

class MemoryDac : public LibXR::DAC
{
 public:
  LibXR::ErrorCode Write(float voltage) override
  {
    if (fail_next_)
    {
      fail_next_ = false;
      return LibXR::ErrorCode::FAILED;
    }
    voltage_ = voltage;
    return LibXR::ErrorCode::OK;
  }

  float voltage_ = 0.0F;
  bool fail_next_ = false;
};

class FeedbackAdc : public LibXR::ADC
{
 public:
  explicit FeedbackAdc(MemoryDac& dac) : dac_(dac) {}

  float Read() override
  {
    if (non_finite_)
    {
      return std::numeric_limits<float>::quiet_NaN();
    }
    return dac_.voltage_ + offset_;
  }

  MemoryDac& dac_;
  float offset_ = 0.0F;
  bool non_finite_ = false;
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
  MemoryDac dac0;
  MemoryDac dac1;
  FeedbackAdc adc0(dac0);
  FeedbackAdc adc1(dac1);
  const std::array<LibXRTest::DacAdcPair, 2U> pairs = {{
      {.output = &dac0, .feedback = &adc0},
      {.output = &dac1, .feedback = &adc1},
  }};
  constexpr std::array<float, 6U> kSetpoints = {0.2F, 2.8F, 1.0F, 2.0F, 2.6F, 0.4F};
  LibXRTest::DacAdcTrackingTestCase test_case = {
      .pairs = pairs,
      .point_major_setpoints = kSetpoints,
      .point_count = 3U,
      .settle_time_ms = 0U,
      .tolerance = 0.05F,
  };

  auto result = LibXRTest::RunDacAdcTrackingTest(test_case);
  SELF_CHECK(result.Passed());
  SELF_CHECK(result.completed_comparisons == 6U);
  SELF_CHECK(result.maximum_error == 0.0F);

  adc1.offset_ = 0.10F;
  result = LibXRTest::RunDacAdcTrackingTest(test_case);
  SELF_CHECK(result.failure == LibXRTest::DacAdcTrackingFailure::VOLTAGE_MISMATCH);
  SELF_CHECK(result.failed_point == 0U);
  SELF_CHECK(result.failed_pair == 1U);
  adc1.offset_ = 0.0F;

  dac0.fail_next_ = true;
  result = LibXRTest::RunDacAdcTrackingTest(test_case);
  SELF_CHECK(result.failure == LibXRTest::DacAdcTrackingFailure::DAC_WRITE);
  SELF_CHECK(result.error == LibXR::ErrorCode::FAILED);

  adc0.non_finite_ = true;
  result = LibXRTest::RunDacAdcTrackingTest(test_case);
  SELF_CHECK(result.failure == LibXRTest::DacAdcTrackingFailure::NON_FINITE);
  adc0.non_finite_ = false;

  auto invalid_case = test_case;
  invalid_case.point_count = 0U;
  result = LibXRTest::RunDacAdcTrackingTest(invalid_case);
  SELF_CHECK(result.failure == LibXRTest::DacAdcTrackingFailure::INVALID_ARGUMENT);

  invalid_case = test_case;
  invalid_case.point_major_setpoints = std::span(kSetpoints).first(5U);
  result = LibXRTest::RunDacAdcTrackingTest(invalid_case);
  SELF_CHECK(result.failure == LibXRTest::DacAdcTrackingFailure::INVALID_ARGUMENT);

  SELF_CHECK(std::strcmp(LibXRTest::DacAdcTrackingFailureName(
                             LibXRTest::DacAdcTrackingFailure::VOLTAGE_MISMATCH),
                         "VOLTAGE_MISMATCH") == 0);

  std::puts("dac-adc hardware-test support selftest: PASS");
  return 0;
}
