/**
 * @file test_swd_general_gpio.cpp
 * @brief SwdGeneralGPIO 主机测试 / Host tests for SwdGeneralGPIO.
 */

#include <cstdio>
#include <vector>

#include "debug/swd_general_gpio.hpp"
#include "gpio.hpp"
#include "test_assert.hpp"

using namespace LibXR;

namespace
{
/// A pin that records every level written to it and every clock edge seen on the other.
class RecordingPin : public GPIO
{
 public:
  bool Read() override { return level; }
  void Write(bool value) override
  {
    level = value;
    writes.push_back(value);
  }
  ErrorCode EnableInterrupt() override { return ErrorCode::OK; }
  ErrorCode DisableInterrupt() override { return ErrorCode::OK; }
  ErrorCode SetConfig(Configuration) override { return ErrorCode::OK; }

  bool level = true;
  std::vector<bool> writes;
};

/// The SWDIO level at every rising edge of SWCLK, as a target would sample it.
struct ClockedPin : public RecordingPin
{
  RecordingPin* data = nullptr;
  std::vector<bool> sampled;
  void Write(bool value) override
  {
    const bool rising = value && !level;
    RecordingPin::Write(value);
    if (rising && data != nullptr) sampled.push_back(data->level);
  }
};

/// Idle cycles keep SWDIO low: a high level would be read by the target as a start bit.
void TestIdleClocksDriveSwdioLow()
{
  ClockedPin swclk;
  RecordingPin swdio;
  swclk.data = &swdio;
  Debug::SwdGeneralGPIO<ClockedPin, RecordingPin> swd(swclk, swdio, 0u,
                                                      Debug::SwdGpioTiming{});
  swclk.sampled.clear();

  swd.IdleClocks(4);
  TEST_ASSERT(swclk.sampled.size() == 4);
  for (const bool level : swclk.sampled)
  {
    TEST_ASSERT(!level);
  }
  TEST_ASSERT(!swdio.level);  // and it stays low afterwards
}

/// The request of a transfer goes out as the ADIv5 packet request, LSB first: a DP read
/// of IDCODE is 0xA5 on the wire.
void TestRequestOnTheWire()
{
  ClockedPin swclk;
  RecordingPin swdio;
  swclk.data = &swdio;
  Debug::SwdGeneralGPIO<ClockedPin, RecordingPin> swd(swclk, swdio, 0u,
                                                      Debug::SwdGpioTiming{});
  swclk.sampled.clear();

  Debug::SwdProtocol::Response resp{};
  (void)swd.Transfer(
      Debug::SwdProtocol::make_dp_read_req(Debug::SwdProtocol::DpReadReg::IDCODE), resp);
  TEST_ASSERT(swclk.sampled.size() >= 8);
  uint8_t request = 0;
  for (unsigned i = 0; i < 8; ++i)
  {
    if (swclk.sampled[i]) request = static_cast<uint8_t>(request | (1u << i));
  }
  TEST_ASSERT(request == 0xA5u);
}

using Gpio = Debug::SwdGeneralGPIO<ClockedPin, RecordingPin>;

/// SetClockHz follows the rule the user set: N = 0 when f >= f_cpu / c, otherwise
/// N = max(1, ceil((f_cpu / f - a) / b)). Synthetic port: 100 MHz CPU, b = 10 cycles,
/// a = 15 cycles, c = 12 cycles.
void TestDelayLoopsFollowTheRule()
{
  constexpr uint32_t CPU = 100'000'000u;
  constexpr Debug::SwdGpioTiming T{100u, 150u, 120u};
  TEST_ASSERT(Gpio::DelayLoops(CPU, 1'000'000u, T) == 9u);  // 100 cycles: 8.5 -> 9
  TEST_ASSERT(Gpio::DelayLoops(CPU, 2'000'000u, T) == 4u);  // 50 cycles: 3.5 -> 4
  TEST_ASSERT(Gpio::DelayLoops(CPU, 3'000'000u, T) == 2u);  // 33.3 cycles: 1.8 -> 2
  TEST_ASSERT(Gpio::DelayLoops(CPU, 5'000'000u, T) == 1u);  // 20 cycles: 0.5 -> 1
  TEST_ASSERT(Gpio::DelayLoops(CPU, 8'000'000u, T) == 1u);  // 12.5 cycles: below a -> 1
  TEST_ASSERT(Gpio::DelayLoops(CPU, 9'000'000u, T) == 0u);  // 11.1 cycles <= c -> 0
  TEST_ASSERT(Gpio::DelayLoops(CPU, 50'000u, T) == 199u);   // 2000 cycles: 198.5 -> 199
  // f == f_cpu / c exactly takes the path without delay.
  TEST_ASSERT(Gpio::DelayLoops(96'000'000u, 8'000'000u, T) == 0u);
  // An unknown CPU clock keeps the path without delay.
  TEST_ASSERT(Gpio::DelayLoops(0u, 1'000'000u, T) == 0u);
  // The defaults are lower bounds: 2 cycles per step, no intercept.
  TEST_ASSERT(Gpio::DelayLoops(CPU, 1'000'000u, Debug::SwdGpioTiming{}) == 50u);
}

/// Under the model the chosen N never gives a clock above the request, and it is the
/// smallest such N.
void TestDelayLoopsNeverFasterThanRequested()
{
  constexpr uint32_t CPU = 64'000'000u;
  constexpr Debug::SwdGpioTiming T{88u, 130u, 110u};
  const uint64_t REQ = static_cast<uint64_t>(CPU) * 10u;
  for (uint32_t hz = 50'000u; hz <= 20'000'000u; hz += 25'000u)
  {
    const uint32_t N = Gpio::DelayLoops(CPU, hz, T);
    if (N == 0u)
    {
      TEST_ASSERT(static_cast<uint64_t>(T.fast_cycles_x10) * hz >= REQ);
      continue;
    }
    TEST_ASSERT(static_cast<uint64_t>(T.fast_cycles_x10) * hz < REQ);
    const uint64_t COST = T.intercept_cycles_x10 + uint64_t{T.step_cycles_x10} * N;
    TEST_ASSERT(COST * hz >= REQ);
    if (N > 1u)
    {
      const uint64_t LESS =
          T.intercept_cycles_x10 + uint64_t{T.step_cycles_x10} * (N - 1u);
      TEST_ASSERT(LESS * hz < REQ);
    }
  }
}
}  // namespace

int main()
{
  TestIdleClocksDriveSwdioLow();
  TestRequestOnTheWire();
  TestDelayLoopsFollowTheRule();
  TestDelayLoopsNeverFasterThanRequested();
  std::puts("SwdGeneralGPIO: idle clocks drive SWDIO low");
}
