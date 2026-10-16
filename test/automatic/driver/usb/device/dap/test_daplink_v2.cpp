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
#include <string>
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
  using DapV2::GetWinUsbMsOs20DescriptorSet;
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
/// Run every command that has arrived, the way the application's DAP thread would.
void Run(DapFixture& f)
{
  while (f.dap.Step(0) == ErrorCode::OK)
  {
  }
}

/// The host sends bytes; the commands they complete are then executed by Step().
void Host(DapFixture& f, const std::vector<uint8_t>& bytes)
{
  f.out.HostSends(bytes);
  Run(f);
}

void Send(DapFixture& f, std::initializer_list<uint8_t> bytes)
{
  Host(f, std::vector<uint8_t>(bytes));
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

  // A delay command is only received by the USB side; nothing is executed or answered
  // until the DAP thread steps, and the next receive is already armed.
  f.out.HostSends({DELAY, 0x10, 0x27});  // 10000 us
  TEST_ASSERT(f.in.starts.empty());
  TEST_ASSERT(f.out.active);

  // Step() sleeps the delay out in the calling thread and then answers.
  TEST_ASSERT(f.dap.Step(0) == ErrorCode::OK);
  TEST_ASSERT(f.in.active && f.in.Last().length == 2);
  TEST_ASSERT(f.in.Last().data == std::vector<uint8_t>({DELAY, 0x00}));
  f.in.Complete();

  // A zero delay answers at once.
  Send(f, {DELAY, 0x00, 0x00});
  TEST_ASSERT(f.in.active && f.in.Last().data == std::vector<uint8_t>({DELAY, 0x00}));
  f.in.Complete();
}

/// 批命令里包含延时命令：延时在 Step 线程里睡完，批响应一次完整发出。
/// A batch that contains a delay command: the delay is slept out in the Step thread and
/// the batch response goes out whole.
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
  Host(f, queue);
  TEST_ASSERT(
      f.in.Last().data ==
      std::vector<uint8_t>({static_cast<uint8_t>(CommandId::QUEUE_COMMANDS), 0x00}));
  f.in.Complete();

  // EXECUTE_COMMANDS：批内的延时命令让 Step 睡到到期，整条批响应一次完整到达：
  // [EXECUTE, INFO 响应, DELAY 响应]，没有第二条多余响应。
  // EXECUTE_COMMANDS: the delay inside the batch makes Step sleep until it is due, and
  // the whole batch response arrives at once: [EXECUTE, INFO response, DELAY response],
  // with no second stray response.
  Send(f, {static_cast<uint8_t>(CommandId::EXECUTE_COMMANDS)});
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

/// 下一条命令在上一条执行（这里是延时）期间就已收下，按序在它之后执行。
/// The next command is received while the previous one executes (here: a delay) and runs
/// after it, in order.
void TestNextCommandReceivedDuringDelay()
{
  FreshTimerList();
  DapFixture f;
  f.dap.BindEndpoints(f.pool, 0, false);

  f.out.HostSends({DELAY, 0x10, 0x27});  // 10000 us
  std::thread dap_thread([&f]() { (void)f.dap.Step(1000); });
  std::this_thread::sleep_for(std::chrono::milliseconds(2));

  // The host sends the next command while the delay is pending: it is received (the
  // USB side does not wait for the DAP thread) but not executed, and nothing answers.
  f.out.HostSends({INFO, PACKET_SIZE_ID});
  dap_thread.join();

  // The delay response goes first; the received command answers after it.
  TEST_ASSERT(f.in.active && f.in.Last().data == std::vector<uint8_t>({DELAY, 0x00}));
  f.in.Complete();
  Run(f);
  TEST_ASSERT(f.in.active && f.in.Last().data[0] == INFO);
  f.in.Complete();
  TEST_ASSERT(!f.in.active && f.out.active);
}

/// 响应恰好等于 DAP 包长时主机的包长缓冲读满即结束，不补零长包；补了会留下一个主机不取的
/// 零长包，占住响应队列的一个位置。短于包长的端点包长整数倍响应仍要补零长包收尾。
/// A response exactly as long as the DAP packet needs no ZLP (the host's packet-sized
/// read ends when full); one would stay armed on the endpoint and keep a response-queue
/// slot occupied. A response shorter than the DAP packet that is a multiple of the
/// endpoint packet size still ends with a ZLP.
void TestZlpOnlyBelowDapPacketSize()
{
  FreshTimerList();
  DapFixture f;
  f.dap.BindEndpoints(f.pool, 0, false);

  // Complete every hardware request of one response; count bytes and zero-length tails.
  auto drain = [&f](size_t& bytes, unsigned& tails)
  {
    bytes = 0;
    tails = 0;
    while (f.in.active)
    {
      if (f.in.Last().length == 0)
        ++tails;
      else
        bytes += f.in.Last().length;
      f.in.Complete();
    }
  };
  size_t bytes;
  unsigned tails;

  // 64 bytes = one endpoint packet, shorter than the DAP packet: a ZLP follows.
  const std::string vendor(61, 'V');  // [INFO, 62, 61 chars + NUL] = 64 bytes
  DapV2::InfoStrings info{};
  info.vendor = vendor.c_str();
  f.dap.SetInfoStrings(info);
  Send(f, {INFO, static_cast<uint8_t>(InfoId::VENDOR)});
  drain(bytes, tails);
  TEST_ASSERT(bytes == 64 && tails == 1);

  // 1024 bytes = the DAP packet size (a 255-word block read): no ZLP.
  Send(f, {CONNECT, static_cast<uint8_t>(Port::SWD)});
  drain(bytes, tails);
  Send(f, {static_cast<uint8_t>(CommandId::TRANSFER_BLOCK), 0x00, 0xFF, 0x00, 0x0F});
  drain(bytes, tails);
  TEST_ASSERT(bytes == 1024 && tails == 0);
}

/// 命令长度恰为端点包长的整数倍、主机不补零长包：按命令自己的长度结束，不等到超时。
/// A command whose length is a multiple of the endpoint packet size and no ZLP from the
/// host: it ends by the command's own length instead of waiting for a timeout.
void TestCommandOnPacketBoundaryEndsWithoutZlp()
{
  FreshTimerList();
  DapFixture f;
  f.dap.BindEndpoints(f.pool, 0, false);

  // QUEUE_COMMANDS with 63 DAP_Info commands: 2 + 63 * 2 = 128 bytes, two packets.
  std::vector<uint8_t> queue{static_cast<uint8_t>(CommandId::QUEUE_COMMANDS), 63u};
  for (unsigned i = 0; i < 63; ++i)
  {
    queue.push_back(static_cast<uint8_t>(CommandId::INFO));
    queue.push_back(static_cast<uint8_t>(InfoId::VENDOR));
  }
  TEST_ASSERT(queue.size() == 128);

  TEST_ASSERT(f.out.active && f.out.Last().length == 64);
  Host(f, std::vector<uint8_t>(queue.begin(), queue.begin() + 64));
  // The first packet does not complete the command: exactly the missing 64 bytes are
  // asked for, and nothing has been answered.
  TEST_ASSERT(f.out.active && f.out.Last().length == 64);
  TEST_ASSERT(f.in.starts.empty());
  Host(f, std::vector<uint8_t>(queue.begin() + 64, queue.end()));
  // The command is complete after the second whole packet: it answers.
  TEST_ASSERT(f.in.active);
  TEST_ASSERT(f.in.Last().data[0] == static_cast<uint8_t>(CommandId::QUEUE_COMMANDS));
}

/// 主机把传输补零到 DAP 包长：补零部分不当作命令，命令只执行一次。
/// The host pads its transfer with zeros to the DAP packet size: the padding is not taken
/// for commands and the command runs once.
void TestZeroPaddedTransferIsDropped()
{
  FreshTimerList();
  DapFixture f;
  f.dap.BindEndpoints(f.pool, 0, false);

  std::vector<uint8_t> first(64, 0);
  first[0] = INFO;
  first[1] = PACKET_SIZE_ID;
  Host(f, first);  // the command is complete inside the first packet
  TEST_ASSERT(f.in.active && f.in.starts.size() == 1);
  f.in.Complete();
  TEST_ASSERT(f.out.active && f.out.Last().length == 64);

  // The next whole packet is all zero: padding. The rest of the 1024 byte transfer is
  // asked for in one receive and dropped.
  Host(f, std::vector<uint8_t>(64, 0));
  // The remaining 896 bytes arrive as 14 packets of the one receive; no command starts.
  for (unsigned i = 0; i < 14; ++i)
  {
    TEST_ASSERT(f.out.active);
    Host(f, std::vector<uint8_t>(64, 0));
  }
  TEST_ASSERT(f.out.active && f.out.Last().length == 64);
  TEST_ASSERT(f.in.starts.size() == 1);  // no response to padding

  // A real command still works afterwards.
  Send(f, {INFO, PACKET_SIZE_ID});
  TEST_ASSERT(f.in.active && f.in.starts.size() == 2);
  f.in.Complete();
}

/// DAP_TransferAbort 没有响应，也不占命令环；接收继续。
/// DAP_TransferAbort has no response and does not take a command-ring place; reception
/// goes on.
void TestTransferAbortHasNoResponse()
{
  FreshTimerList();
  DapFixture f;
  f.dap.BindEndpoints(f.pool, 0, false);

  Send(f, {static_cast<uint8_t>(CommandId::TRANSFER_ABORT)});
  TEST_ASSERT(f.in.starts.empty() && !f.in.active);
  TEST_ASSERT(f.out.active);

  // A transfer started afterwards is not aborted by the stale flag.
  Send(f, {CONNECT, static_cast<uint8_t>(Port::SWD)});
  f.in.Complete();
  Send(f, {static_cast<uint8_t>(CommandId::TRANSFER), 0x00, 0x01, 0x02});  // read DP 0
  TEST_ASSERT(f.in.active);
  TEST_ASSERT(f.in.Last().data[0] == static_cast<uint8_t>(CommandId::TRANSFER));
  TEST_ASSERT(f.in.Last().data[1] == 1);  // one transfer completed
  f.in.Complete();
}

/// EXECUTE_COMMANDS 自带的命令和已排队的命令一起执行。
/// The commands carried by EXECUTE_COMMANDS run together with the queued ones.
void TestExecuteCommandsWithInlineCommands()
{
  FreshTimerList();
  DapFixture f;
  f.dap.BindEndpoints(f.pool, 0, false);

  Send(f, {static_cast<uint8_t>(CommandId::QUEUE_COMMANDS), 1, INFO, PACKET_COUNT_ID});
  f.in.Complete();
  Send(f, {static_cast<uint8_t>(CommandId::EXECUTE_COMMANDS), 1, INFO, PACKET_SIZE_ID});
  TEST_ASSERT(f.in.active);
  const auto& batch = f.in.Last().data;
  TEST_ASSERT(batch[0] == static_cast<uint8_t>(CommandId::EXECUTE_COMMANDS));
  // The queued PACKET_COUNT answer first, then the inline PACKET_SIZE answer.
  TEST_ASSERT(batch[1] == INFO && batch[2] == 1 && batch[3] == 0x04);
  TEST_ASSERT(batch[4] == INFO && batch[5] == 2);
  f.in.Complete();
}

/// 补零到 DAP 包长的请求，窗口里放满 4 条命令：第 4 条的补零也要收完，否则主机的写
/// 无法结束，也就不会去读响应，设备和主机互相等。
/// Zero-padded requests with the window filled to four commands: the padding of the
/// fourth must still be taken, or the host's write cannot finish, it never reads the
/// responses, and device and host wait for each other.
void TestPaddedRequestsFillTheWindowWithoutStalling()
{
  FreshTimerList();
  DapFixture f;
  f.dap.BindEndpoints(f.pool, 0, false);

  for (unsigned request = 0; request < 4; ++request)
  {
    std::vector<uint8_t> first(64, 0);
    first[0] = INFO;
    first[1] = PACKET_SIZE_ID;
    TEST_ASSERT(f.out.active);
    Host(f, first);
    // The padding that follows is taken whether or not the window is full.
    for (unsigned packet = 1; packet < 16; ++packet)
    {
      TEST_ASSERT(f.out.active);
      Host(f, std::vector<uint8_t>(64, 0));
    }
  }
  // Four commands outstanding: the window is full, one response is on the wire.
  TEST_ASSERT(!f.out.active);
  TEST_ASSERT(f.in.starts.size() == 1);

  // Retiring the responses opens the window again and reception resumes.
  for (unsigned i = 0; i < 4; ++i)
  {
    TEST_ASSERT(f.in.active);
    f.in.Complete();
  }
  TEST_ASSERT(!f.in.active && f.out.active);
}

/// 窗口满时，探针里收到的下一条命令不能挤进来（执行时会没有响应槽）：它留着，等一个
/// 响应发送完、窗口空出来才放进命令池。
/// With the window full, the next command received through the probe must not squeeze
/// in (it would find no response slot when it runs): it waits until a response has been
/// sent and the window has a place.
void TestCommandBeyondTheWindowWaitsInTheProbe()
{
  FreshTimerList();
  DapFixture f;
  f.dap.BindEndpoints(f.pool, 0, false);

  // Four commands, each exactly one whole packet (INFO then zeros, no padding after).
  std::vector<uint8_t> command(64, 0);
  command[0] = INFO;
  command[1] = PACKET_SIZE_ID;
  std::vector<uint8_t> next_command(64, 0x11);  // a fifth, non-zero first packet
  next_command[0] = INFO;
  next_command[1] = PACKET_SIZE_ID;
  for (unsigned i = 0; i < 4; ++i)
  {
    TEST_ASSERT(f.out.active);
    Host(f, command);
  }
  // The fifth command's first packet arrives through the probe; the window is full, so it
  // is held there and nothing more is received or answered.
  TEST_ASSERT(f.out.active);
  Host(f, next_command);
  TEST_ASSERT(!f.out.active);
  TEST_ASSERT(f.in.starts.size() == 1);

  // A response goes out: the window opens and the held command runs.
  unsigned answered = 0;
  while (f.in.active)
  {
    f.in.Complete();
    Run(f);
    ++answered;
  }
  TEST_ASSERT(answered == 5);  // 4 + the held fifth
  TEST_ASSERT(f.out.active);
}

/// 单功能设备用设备级 MS OS 2.0 描述符集（Windows 对非复合设备不看功能子集头）。
/// A single-function device uses the device-level MS OS 2.0 descriptor set (Windows
/// ignores the function subset header on a non-composite device).
void TestStandaloneDeviceUsesDeviceLevelWinUsbSet()
{
  FreshTimerList();
  DapFixture f;  // DAP is the only function of its composition
  const auto set = f.dap.GetWinUsbMsOs20DescriptorSet();
  using Device = LibXR::USB::WinUsbMsOs20::DeviceScopedWinUsbMsOs20DescSet<
      LibXR::USB::WinUsbMsOs20::GUID_MULTI_SZ_UTF16_BYTES>;
  TEST_ASSERT(set.size_ == sizeof(Device));
  uint16_t total = 0;
  std::memcpy(&total, static_cast<const uint8_t*>(set.addr_) + 8, sizeof(total));
  TEST_ASSERT(total == sizeof(Device));  // wTotalLength of the set header
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
  TestNextCommandReceivedDuringDelay();
  TestZlpOnlyBelowDapPacketSize();
  TestCommandOnPacketBoundaryEndsWithoutZlp();
  TestZeroPaddedTransferIsDropped();
  TestTransferAbortHasNoResponse();
  TestExecuteCommandsWithInlineCommands();
  TestPaddedRequestsFillTheWindowWithoutStalling();
  TestCommandBeyondTheWindowWaitsInTheProbe();
  TestStandaloneDeviceUsesDeviceLevelWinUsbSet();
  TestUnbind();
  std::puts(
      "DAP v2 class: copy-mode Read/Write, response depth and deferred delay passed");
}
