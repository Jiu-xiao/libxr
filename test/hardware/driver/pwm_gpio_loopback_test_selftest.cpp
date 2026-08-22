#include <array>
#include <cmath>
#include <cstdio>

#include "driver/pwm_gpio_loopback_test.hpp"
#include "libxr.hpp"

namespace
{

class FakePwm : public LibXR::PWM
{
 public:
  LibXR::ErrorCode SetDutyCycle(float value) override
  {
    duty = value;
    return fail_duty ? LibXR::ErrorCode::FAILED : LibXR::ErrorCode::OK;
  }
  LibXR::ErrorCode SetConfig(Configuration config) override
  {
    frequency = config.frequency;
    return fail_config ? LibXR::ErrorCode::FAILED : LibXR::ErrorCode::OK;
  }
  LibXR::ErrorCode Enable() override
  {
    enabled = true;
    return LibXR::ErrorCode::OK;
  }
  LibXR::ErrorCode Disable() override
  {
    enabled = false;
    return LibXR::ErrorCode::OK;
  }

  float duty = 0.0F;
  uint32_t frequency = 0U;
  bool enabled = false;
  bool fail_config = false;
  bool fail_duty = false;
};

class FakeGpio : public LibXR::GPIO
{
 public:
  explicit FakeGpio(FakePwm& pwm) : pwm_(pwm) {}
  bool Read() override
  {
    if (!pwm_.enabled || pwm_.frequency == 0U)
    {
      return false;
    }
    const uint64_t now = static_cast<uint64_t>(LibXR::Timebase::GetMicroseconds());
    const uint64_t phase = (now * pwm_.frequency) % 1000000U;
    return phase < static_cast<uint64_t>(pwm_.duty * 1000000.0F);
  }
  void Write(bool) override {}
  LibXR::ErrorCode EnableInterrupt() override { return LibXR::ErrorCode::OK; }
  LibXR::ErrorCode DisableInterrupt() override { return LibXR::ErrorCode::OK; }
  LibXR::ErrorCode SetConfig(Configuration) override { return LibXR::ErrorCode::OK; }

 private:
  FakePwm& pwm_;
};

#define SELF_CHECK(expression)                                     \
  do                                                               \
  {                                                                \
    if (!(expression))                                             \
    {                                                              \
      std::fprintf(stderr, "selftest failure: %s\n", #expression); \
      return 1;                                                    \
    }                                                              \
  } while (false)

}  // namespace

int main()
{
  LibXR::PlatformInit();
  FakePwm pwm0;
  FakePwm pwm1;
  FakeGpio gpio0(pwm0);
  FakeGpio gpio1(pwm1);
  const std::array<LibXRTest::PwmGpioPair, 2U> pairs = {{
      {.output = &pwm0, .feedback = &gpio0},
      {.output = &pwm1, .feedback = &gpio1},
  }};
  constexpr std::array<float, 6U> kDuties = {0.2F, 0.8F, 0.5F, 0.5F, 0.8F, 0.2F};
  LibXRTest::PwmGpioLoopbackTestCase test_case = {
      .pairs = pairs,
      .point_major_duties = kDuties,
      .point_count = 3U,
      .frequency_hz = 200U,
      .settle_time_us = 5000U,
      .samples_per_point = 400U,
      .minimum_sample_interval_us = 100U,
      .sample_interval_jitter_us = 200U,
      .duty_tolerance = 0.15F,
      .frequency_tolerance = 0.15F,
      .random_seed = 0x12345678U,
  };

  auto result = LibXRTest::RunPwmGpioLoopbackTest(test_case);
  SELF_CHECK(result.Passed());
  SELF_CHECK(result.completed_comparisons == 6U);
  SELF_CHECK(!pwm0.enabled && !pwm1.enabled);

  pwm0.fail_config = true;
  result = LibXRTest::RunPwmGpioLoopbackTest(test_case);
  SELF_CHECK(result.failure == LibXRTest::PwmGpioLoopbackFailure::CONFIGURE);
  pwm0.fail_config = false;

  auto invalid = test_case;
  invalid.frequency_hz = 0U;
  result = LibXRTest::RunPwmGpioLoopbackTest(invalid);
  SELF_CHECK(result.failure == LibXRTest::PwmGpioLoopbackFailure::INVALID_ARGUMENT);

  std::puts("pwm-gpio hardware-test support selftest: PASS");
  return 0;
}
