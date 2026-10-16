/**
 * @file test_daplink_v2.cpp
 * @brief DAP v2 设备类（槽模型）主机测试 / Host tests for the DAP v2 device class.
 *
 * 覆盖 5B 迁移改动的路径：OUT 用拷贝模式 Read 一次收一条完整命令、IN 用拷贝模式 Write
 * 发队头响应（超过一个 USB 包的响应跨多个包）、响应环保持主机侧在途深度、以及延时命令
 * 不在 owner 里忙等而是把响应槽留给 1 ms Timer 路径续做。
 * Covers the paths the 5B migration changed: OUT receives one whole command per copy-mode
 * Read, IN writes the head response through a copy-mode Write (a response above the wire
 * packet size spans several packets), the response ring keeps the host-visible
 * outstanding depth, and a delayed command does not busy-wait in the owner but reserves
 * its response slot for the 1 ms Timer path.
 */

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <thread>
#include <vector>

#include "../../mock_endpoint.hpp"
#include "daplink_v2.hpp"
#include "debug/swd.hpp"
#include "device_composition.hpp"
#include "lockfree_list.hpp"
#include "timer.hpp"

using namespace LibXR;
using namespace LibXR::USB;
using namespace LibXR::USB::Test;

namespace
{
static constexpr auto TEST_LANGUAGE = DescriptorStrings::MakeLanguagePack(
    DescriptorStrings::Language::EN_US, "LibXR", "USB tests", "0");

using CommandId = LibXR::USB::DapLinkV2Def::CommandId;
using InfoId = LibXR::USB::DapLinkV2Def::InfoId;
using Port = LibXR::USB::DapLinkV2Def::Port;

constexpr uint8_t INFO = static_cast<uint8_t>(CommandId::INFO);
constexpr uint8_t CONNECT = static_cast<uint8_t>(CommandId::CONNECT);
constexpr uint8_t DELAY = static_cast<uint8_t>(CommandId::DELAY);
constexpr uint8_t PACKET_SIZE_ID = static_cast<uint8_t>(InfoId::PACKET_SIZE);
constexpr uint8_t PACKET_COUNT_ID = static_cast<uint8_t>(InfoId::PACKET_COUNT);

/// Minimal SWD probe: every transfer succeeds and reads a fixed word.
class FakeSwd : public LibXR::Debug::Swd
{
 public:
  unsigned enters = 0, closes = 0;
  uint32_t clock_hz = 0;

  ErrorCode SetClockHz(uint32_t hz) override
  {
    clock_hz = hz;
    return ErrorCode::OK;
  }
  void Close() override { ++closes; }
  ErrorCode LineReset() override { return ErrorCode::OK; }
  ErrorCode EnterSwd() override
  {
    ++enters;
    return ErrorCode::OK;
  }
  ErrorCode Transfer(const LibXR::Debug::SwdProtocol::Request& req,
                     LibXR::Debug::SwdProtocol::Response& resp) override
  {
    if (req.rnw) resp.rdata = 0x12345678u;
    resp.ack = LibXR::Debug::SwdProtocol::Ack::OK;
    resp.parity_ok = true;
    return ErrorCode::OK;
  }
  void IdleClocks(uint32_t) override {}
  ErrorCode SeqWriteBits(uint32_t, const uint8_t*) override { return ErrorCode::OK; }
  ErrorCode SeqReadBits(uint32_t cycles, uint8_t* out) override
  {
    if (out != nullptr) std::memset(out, 0, (cycles + 7U) / 8U);
    return ErrorCode::OK;
  }
};

/// Give this test its own timer list so a class destroyed earlier cannot leave a
/// dangling task node behind for the next Timer::Refresh().
void FreshTimerList() { LibXR::Timer::list_ = new LibXR::LockFreeList(); }

using DapV2 = DapLinkV2Class<FakeSwd>;

class TestDap2 : public DapV2
{
 public:
  using DapV2::BindEndpoints;
  using DapV2::DapV2;
  using DapV2::OnService;
  using DapV2::UnbindEndpoints;
};

/// The class, its SWD probe and the composition that gives it a class-service pool.
struct DapFixture : Fixture
{
  FakeSwd swd;
  TestDap2 dap;
  DeviceComposition composition;
  Callback<uint32_t> handler;

  DapFixture()
      : swd(),
        dap(Number::EP1, Number::EP2, swd),
        composition(pool, {&TEST_LANGUAGE}, {{&dap}})
  {
    // Stand-in for DeviceCore's class-service dispatch.
    handler = Callback<uint32_t>::Create(
        [](bool in_isr, DeviceClass* self, uint32_t events)
        {
          if ((events & EndpointPool::CLASS_EVENT) != 0U) self->OnService(in_isr);
        },
        static_cast<DeviceClass*>(&dap));
    pool.SetControlHandler(handler);
  }
};

/// Send one whole DAP command; a short packet ends the copy-mode Read.
void Send(DapFixture& f, std::initializer_list<uint8_t> bytes)
{
  f.out.HostSends(std::vector<uint8_t>(bytes));
}

void TestCommandRoundTrip()
{
  FreshTimerList();
  DapFixture f;
  f.dap.BindEndpoints(f.pool, 0, false);

  // The OUT endpoint is armed with one wire packet of a whole DAP packet.
  TEST_ASSERT(f.out.active && f.out.Last().length == 64 && f.out.starts.size() == 1);
  TEST_ASSERT(f.in.starts.empty());

  // DAP_Info(PACKET_SIZE) answers the host-visible packet size (1024 by default).
  Send(f, {INFO, PACKET_SIZE_ID});
  TEST_ASSERT(f.in.active && f.in.Last().length == 4);
  TEST_ASSERT(f.in.Last().data == std::vector<uint8_t>({INFO, 2, 0x00, 0x04}));
  // The response was accepted, so the OUT receive re-armed behind it.
  TEST_ASSERT(f.out.active && f.out.starts.size() == 2);

  f.in.Complete();
  // The next command's Read was already armed when the first one was consumed, so the
  // OUT endpoint does not start again here.
  TEST_ASSERT(f.out.active && f.out.starts.size() == 2);

  // DAP_Info(PACKET_COUNT) answers the effective outstanding depth.
  Send(f, {INFO, PACKET_COUNT_ID});
  TEST_ASSERT(f.in.Last().length == 3);
  TEST_ASSERT(f.in.Last().data == std::vector<uint8_t>({INFO, 1, 0x04}));
  f.in.Complete();

  // A connect command reaches the SWD probe and answers the port.
  Send(f, {CONNECT, static_cast<uint8_t>(Port::SWD)});
  TEST_ASSERT(f.in.Last().length == 2);
  TEST_ASSERT(f.in.Last().data[0] == CONNECT &&
              f.in.Last().data[1] == static_cast<uint8_t>(Port::SWD));
  TEST_ASSERT(f.swd.enters == 1 && f.swd.clock_hz == 1000000u);
  f.in.Complete();
}

void TestPipelineKeepsAdvertisedDepth()
{
  FreshTimerList();
  DapFixture f;
  f.dap.BindEndpoints(f.pool, 0, false);

  // The advertised packet count is four; four commands are accepted back to back.
  for (unsigned i = 0; i < 4; ++i)
  {
    Send(f, {INFO, PACKET_SIZE_ID});
  }
  TEST_ASSERT(f.in.starts.size() == 1);   // one response on the wire at a time
  TEST_ASSERT(f.out.starts.size() == 4);  // three re-arms plus the initial one
  // The fourth response filled the outstanding window: OUT is not re-armed.
  TEST_ASSERT(!f.out.active);

  // Retiring one response opens the window again.
  f.in.Complete();
  TEST_ASSERT(f.in.starts.size() == 2 && f.out.active && f.out.starts.size() == 5);
  f.in.Complete();
  f.in.Complete();
  f.in.Complete();
  TEST_ASSERT(!f.in.active);
}

void TestDelayedCommand()
{
  FreshTimerList();
  DapFixture f;
  f.dap.BindEndpoints(f.pool, 0, false);

  // A delay command does not busy-wait in the owner: no response is submitted and the
  // OUT receive stays unarmed while the delay is pending.
  Send(f, {DELAY, 0x10, 0x27});  // 10000 us
  TEST_ASSERT(f.in.starts.empty());
  TEST_ASSERT(!f.out.active);

  // The 1 ms Timer task rings the class service; once the deadline passed the response
  // is queued and sent.
  std::this_thread::sleep_for(std::chrono::milliseconds(15));
  Timer::Refresh();
  TEST_ASSERT(f.in.active && f.in.Last().length == 2);
  TEST_ASSERT(f.in.Last().data == std::vector<uint8_t>({DELAY, 0x00}));
  TEST_ASSERT(f.out.active);
  f.in.Complete();

  // A zero delay answers immediately.
  Send(f, {DELAY, 0x00, 0x00});
  TEST_ASSERT(f.in.active && f.in.Last().data == std::vector<uint8_t>({DELAY, 0x00}));
  f.in.Complete();
}

/// 批命令里包含延时命令：旧实现会挂起并带着"半个响应"返回，延时到期后 Timer
/// 路径往已发出的缓冲里补写（主机收到多余响应、批响应缺字节）；现在延时就地
/// 同步完成，批响应一次完整发出。
/// A batch that contains a delay command: the old implementation suspended and
/// returned a half-built response, and the Timer path later wrote into a buffer
/// that had already been sent (an extra response on the host, a short batch
/// response). The delay now finishes inline and the batch response goes out whole.
void TestBatchWithDelay()
{
  FreshTimerList();
  DapFixture f;
  f.dap.BindEndpoints(f.pool, 0, false);

  // QUEUE_COMMANDS：两条命令入队——DAP_Info 与一个 2 ms 的 DAP_Delay。线格式是
  // 命令直接拼接（长度由命令 ID 决定，无前缀）。
  // QUEUE_COMMANDS: enqueue two commands - DAP_Info and a 2 ms DAP_Delay. The wire
  // format is plain concatenation (each command's length follows from its ID, no
  // prefix).
  std::vector<uint8_t> queue{
      static_cast<uint8_t>(CommandId::QUEUE_COMMANDS),
      2u,
      static_cast<uint8_t>(CommandId::INFO),
      static_cast<uint8_t>(InfoId::VENDOR),
      DELAY,
      0xD0,
      0x07,  // 2000 us little-endian
  };
  f.out.HostSends(queue);
  TEST_ASSERT(
      f.in.Last().data ==
      std::vector<uint8_t>({static_cast<uint8_t>(CommandId::QUEUE_COMMANDS), 0x00}));
  f.in.Complete();

  // EXECUTE_COMMANDS：批内的延时命令让批处理挂起——owner 不忙等，响应槽保留，
  // 此刻还没有响应发出，OUT 也没有重挂。
  // EXECUTE_COMMANDS: the delay command suspends the batch — the owner does not
  // busy-wait, the response slot stays reserved, and nothing has been sent yet.
  Send(f, {static_cast<uint8_t>(CommandId::EXECUTE_COMMANDS)});
  // 只有前面 QUEUE_COMMANDS 那一次 IN 启动，没有新的响应发出。
  // Only the earlier QUEUE_COMMANDS start exists; no new response went out.
  TEST_ASSERT(f.in.starts.size() == 1u && !f.in.active);
  TEST_ASSERT(!f.out.active);

  // 延时到期（Timer 路径续做）后，整条批响应一次完整到达：
  // [EXECUTE, INFO 响应, DELAY 响应]，没有第二条多余响应。
  // Once the delay elapses (the Timer path resumes), the whole batch response
  // arrives at once: [EXECUTE, INFO response, DELAY response], with no second stray
  // response.
  std::this_thread::sleep_for(std::chrono::milliseconds(15));
  Timer::Refresh();
  TEST_ASSERT(f.in.active);
  const auto& batch = f.in.Last().data;
  // INFO 响应：[INFO, len=7, "XRobot" + NUL]，共 9 字节（批内 [1..9]）；DELAY
  // 响应 [DELAY, 0x00] 跟在 [10..11]，批总长 12。
  // INFO response: [INFO, len=7, "XRobot" + NUL], 9 bytes total (batch [1..9]);
  // the DELAY response [DELAY, 0x00] follows at [10..11], batch length 12.
  TEST_ASSERT(batch[0] == static_cast<uint8_t>(CommandId::EXECUTE_COMMANDS));
  TEST_ASSERT(batch[1] == static_cast<uint8_t>(CommandId::INFO));
  TEST_ASSERT(batch[2] == 7U);    // "XRobot" incl. NUL
  TEST_ASSERT(batch[9] == 0x00);  // NUL of "XRobot"
  TEST_ASSERT(batch[10] == DELAY);
  TEST_ASSERT(batch[11] == 0x00);  // DAP_OK
  TEST_ASSERT(f.in.Last().length == 12U);
  f.in.Complete();

  // 批后 OUT 重新挂接收。
  // The OUT endpoint is rearmed after the batch.
  TEST_ASSERT(f.out.active);
}

void TestUnbind()
{
  FreshTimerList();
  DapFixture f;
  f.dap.BindEndpoints(f.pool, 0, false);
  Send(f, {INFO, PACKET_SIZE_ID});
  TEST_ASSERT(f.in.active);
  f.dap.UnbindEndpoints(f.pool, false);
  TEST_ASSERT(f.out.closes == 1);
  // Rebinding serves the next command normally.
  f.dap.BindEndpoints(f.pool, 0, false);
  TEST_ASSERT(f.out.active && f.out.Last().length == 64);
  Send(f, {INFO, PACKET_SIZE_ID});
  TEST_ASSERT(f.in.active && f.in.Last().length == 4);
  f.in.Complete();
}
}  // namespace

int main()
{
  // Each test provides its own timer list so the constructor's Timer::Add() does not
  // start a scheduler thread; the test drives every refresh itself.
  TestCommandRoundTrip();
  TestPipelineKeepsAdvertisedDepth();
  TestDelayedCommand();
  TestBatchWithDelay();
  TestUnbind();
  std::puts(
      "DAP v2 class: copy-mode Read/Write, response depth and deferred delay passed");
}
