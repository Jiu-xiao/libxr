#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <span>

#include "driver/adc_sampling_test.hpp"

namespace
{

class SequenceAdc : public LibXR::ADC
{
 public:
  explicit SequenceAdc(std::span<const float> values) : values_(values) {}

  float Read() override
  {
    const float value = values_[next_ % values_.size()];
    next_++;
    return value;
  }

 private:
  std::span<const float> values_;
  size_t next_ = 0U;
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
  constexpr std::array<float, 3U> kValues0 = {1.10F, 1.20F, 1.15F};
  constexpr std::array<float, 3U> kValues1 = {2.00F, 2.10F, 2.05F};
  SequenceAdc adc0(kValues0);
  SequenceAdc adc1(kValues1);

  std::array<LibXRTest::AdcSamplingExpectation, 2U> expectations = {{
      {.channel = &adc0,
       .minimum_voltage = 1.0F,
       .maximum_voltage = 1.3F,
       .maximum_span = 0.2F},
      {.channel = &adc1,
       .minimum_voltage = 1.9F,
       .maximum_voltage = 2.2F,
       .maximum_span = 0.2F},
  }};
  std::array<LibXRTest::AdcSamplingChannelStats, 2U> stats{};
  LibXRTest::AdcSamplingTestCase test_case = {
      .channels = expectations,
      .samples_per_channel = 3U,
      .sample_interval_ms = 0U,
  };

  auto result = LibXRTest::RunAdcSamplingTest(test_case, stats);
  SELF_CHECK(result.Passed());
  SELF_CHECK(result.completed_reads == 6U);
  SELF_CHECK(stats[0].samples == 3U);
  SELF_CHECK(stats[0].minimum_voltage == 1.10F);
  SELF_CHECK(stats[0].maximum_voltage == 1.20F);
  SELF_CHECK(stats[1].minimum_voltage == 2.00F);
  SELF_CHECK(stats[1].maximum_voltage == 2.10F);

  auto invalid_case = test_case;
  invalid_case.samples_per_channel = 0U;
  result = LibXRTest::RunAdcSamplingTest(invalid_case, stats);
  SELF_CHECK(result.failure == LibXRTest::AdcSamplingFailure::INVALID_ARGUMENT);
  SELF_CHECK(result.error == LibXR::ErrorCode::ARG_ERR);

  result = LibXRTest::RunAdcSamplingTest(test_case, std::span(stats).first(1U));
  SELF_CHECK(result.failure == LibXRTest::AdcSamplingFailure::INVALID_ARGUMENT);

  auto invalid_expectations = expectations;
  invalid_expectations[0].channel = nullptr;
  invalid_case = test_case;
  invalid_case.channels = invalid_expectations;
  result = LibXRTest::RunAdcSamplingTest(invalid_case, stats);
  SELF_CHECK(result.failure == LibXRTest::AdcSamplingFailure::INVALID_ARGUMENT);

  invalid_expectations = expectations;
  invalid_expectations[0].minimum_voltage = 2.0F;
  invalid_expectations[0].maximum_voltage = 1.0F;
  invalid_case.channels = invalid_expectations;
  result = LibXRTest::RunAdcSamplingTest(invalid_case, stats);
  SELF_CHECK(result.failure == LibXRTest::AdcSamplingFailure::INVALID_ARGUMENT);

  constexpr std::array<float, 1U> kOutOfRange = {3.0F};
  SequenceAdc out_of_range(kOutOfRange);
  std::array<LibXRTest::AdcSamplingExpectation, 1U> one_expectation = {{
      {.channel = &out_of_range,
       .minimum_voltage = 0.0F,
       .maximum_voltage = 2.0F,
       .maximum_span = 1.0F},
  }};
  std::array<LibXRTest::AdcSamplingChannelStats, 1U> one_stats{};
  invalid_case = {
      .channels = one_expectation,
      .samples_per_channel = 1U,
      .sample_interval_ms = 0U,
  };
  result = LibXRTest::RunAdcSamplingTest(invalid_case, one_stats);
  SELF_CHECK(result.failure == LibXRTest::AdcSamplingFailure::OUT_OF_RANGE);
  SELF_CHECK(result.failed_channel == 0U);
  SELF_CHECK(result.failed_sample == 0U);

  constexpr std::array<float, 1U> kNonFinite = {std::numeric_limits<float>::quiet_NaN()};
  SequenceAdc non_finite(kNonFinite);
  one_expectation[0].channel = &non_finite;
  one_expectation[0].maximum_voltage = 3.3F;
  result = LibXRTest::RunAdcSamplingTest(invalid_case, one_stats);
  SELF_CHECK(result.failure == LibXRTest::AdcSamplingFailure::NON_FINITE);

  constexpr std::array<float, 2U> kNoisy = {1.0F, 1.5F};
  SequenceAdc noisy(kNoisy);
  one_expectation[0] = {
      .channel = &noisy,
      .minimum_voltage = 0.0F,
      .maximum_voltage = 3.3F,
      .maximum_span = 0.1F,
  };
  invalid_case.samples_per_channel = 2U;
  result = LibXRTest::RunAdcSamplingTest(invalid_case, one_stats);
  SELF_CHECK(result.failure == LibXRTest::AdcSamplingFailure::EXCESSIVE_SPAN);
  SELF_CHECK(std::fabs(result.observed_voltage - 0.5F) < 0.0001F);

  SELF_CHECK(std::strcmp(LibXRTest::AdcSamplingFailureName(
                             LibXRTest::AdcSamplingFailure::EXCESSIVE_SPAN),
                         "EXCESSIVE_SPAN") == 0);

  std::puts("adc hardware-test support selftest: PASS");
  return 0;
}
