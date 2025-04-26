#include "float_encoder.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_float_encoder()
{
  using namespace LibXR;

  constexpr int BITS = 21;

  // 1. Gyroscope test: ±2000 deg/s
  {
    FloatEncoder<BITS> encoder(-2000.0f, 2000.0f);

    struct __attribute__((packed))
    {
      float input;
      uint32_t encoded;
      float decoded;
    } gyro;

    gyro.input = 123.456f;
    gyro.encoded = encoder.Encode(gyro.input);
    gyro.decoded = encoder.Decode(gyro.encoded);

    float error = std::abs(gyro.decoded - gyro.input);

    UNUSED(error);

    ASSERT(error < 0.001f);  // Allowable error: ±0.001 deg/s
  }

  // 2. Accelerometer test: ±24g
  {
    FloatEncoder<BITS> encoder(-24.0f, 24.0f);

    struct __attribute__((packed))
    {
      float input;
      uint32_t encoded;
      float decoded;
    } accl;

    accl.input = -9.81f;
    accl.encoded = encoder.Encode(accl.input);
    accl.decoded = encoder.Decode(accl.encoded);

    float error = std::abs(accl.decoded - accl.input);

    UNUSED(error);

    ASSERT(error < 0.001f);  // Higher precision required (±0.001g)
  }

  // 3. Euler angle test: [-π, π]
  {
    FloatEncoder<BITS> encoder(-M_PI, M_PI);

    struct __attribute__((packed))
    {
      float input;
      uint32_t encoded;
      float decoded;
    } eulr;

    eulr.input = M_PI / 2.0f;
    eulr.encoded = encoder.Encode(eulr.input);
    eulr.decoded = encoder.Decode(eulr.encoded);

    float error = std::abs(eulr.decoded - eulr.input);

    UNUSED(error);

    ASSERT(error < 0.001f);  // Require high angular accuracy (±0.001 rad)
  }

  // 4. Out-of-range test: values exceeding min/max should be clamped
  {
    FloatEncoder<BITS> encoder(-100.0f, 100.0f);

    float min_val = encoder.Decode(encoder.Encode(-150.0f));  // below range
    float max_val = encoder.Decode(encoder.Encode(150.0f));   // above range

    ASSERT(std::abs(min_val + 100.0f) < 1e-3f);
    ASSERT(std::abs(max_val - 100.0f) < 1e-3f);

    UNUSED(min_val && max_val);
  }

  // 5. Test boundary values: verify encoding and decoding of min/max
  {
    FloatEncoder<BITS> encoder(-100.0f, 100.0f);
    float min_decoded = encoder.Decode(encoder.Encode(-100.0f));
    float max_decoded = encoder.Decode(encoder.Encode(100.0f));

    ASSERT(std::abs(min_decoded + 100.0f) < 1e-5f);
    ASSERT(std::abs(max_decoded - 100.0f) < 1e-5f);

    UNUSED(min_decoded && max_decoded);
  }

  // 6. Test encoding of 0.0 (center point)
  {
    FloatEncoder<BITS> encoder(-50.0f, 50.0f);
    float decoded = encoder.Decode(encoder.Encode(0.0f));
    ASSERT(std::abs(decoded - 0.0f) < 1e-4f);

    UNUSED(decoded);
  }

  // 7. Minimum bit-width test (Bits = 1)
  {
    FloatEncoder<1> encoder(-1.0f, 1.0f);
    uint32_t code0 = encoder.Encode(-1.0f);
    uint32_t code1 = encoder.Encode(1.0f);
    ASSERT(code0 == 0);
    ASSERT(code1 == 1);

    float decoded0 = encoder.Decode(code0);
    float decoded1 = encoder.Decode(code1);
    ASSERT(decoded0 <= -0.5f);
    ASSERT(decoded1 >= 0.5f);

    UNUSED(decoded0 && decoded1);
  }

  // 8. Special values test (NaN / INF): ensure robustness
  {
    FloatEncoder<BITS> encoder(-100.0f, 100.0f);

    float decoded_nan = encoder.Decode(encoder.Encode(NAN));
    float decoded_inf = encoder.Decode(encoder.Encode(INFINITY));
    float decoded_ninf = encoder.Decode(encoder.Encode(-INFINITY));

    ASSERT(std::abs(decoded_inf - 100.0f) < 1e-3f);
    ASSERT(std::abs(decoded_ninf + 100.0f) < 1e-3f);
    // Behavior of NaN is undefined; main goal is to ensure no crash

    UNUSED(decoded_nan && decoded_inf && decoded_ninf);
  }
}
