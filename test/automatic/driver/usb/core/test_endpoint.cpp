#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "cdc_uart.hpp"
#include "daplink_v1.hpp"
#include "daplink_v2.hpp"
#include "debug/swd.hpp"
#include "dfu_bootloader.hpp"
#include "ep_pool.hpp"
#include "hid_mouse.hpp"
#include "test_assert.hpp"
#include "uac_mic.hpp"

using namespace LibXR;
using namespace LibXR::USB;

extern "C" void libxr_fatal_error(const char* file, uint32_t line, bool)
{
  std::fprintf(stderr, "%s:%u: USB product assertion failed\n", file, line);
  std::abort();
}

namespace
{
using Direction = Endpoint::Direction;
using Number = Endpoint::EPNumber;
using Type = Endpoint::Type;
using State = Endpoint::State;

class MockEndpoint : public Endpoint
{
 public:
  struct Start
  {
    void* address;
    size_t length;
    std::vector<uint8_t> data;
  };
  MockEndpoint(Number number, Direction dir, RawData storage, size_t segment = 64)
      : Endpoint(number, dir, storage), segment_limit(segment)
  {
  }
  std::vector<Start> starts;
  std::vector<std::vector<uint8_t>> sent;
  std::atomic<size_t> wire_bytes{0};
  size_t segment_limit;
  std::function<void()> at_start;
  bool active = false;

  void Complete(bool in_isr = true)
  {
    TEST_ASSERT(active);
    active = false;
    const auto& started = starts.back();
    const auto* data = static_cast<const uint8_t*>(started.address);
    if (started.length)
      TEST_ASSERT(std::equal(started.data.begin(), started.data.end(), data));
    sent.push_back(started.data);
    wire_bytes.fetch_add(started.data.size(), std::memory_order_release);
    OnTransferCompleteCallback(in_isr, started.length);
  }
  void Receive(const std::vector<uint8_t>& bytes)
  {
    TEST_ASSERT(active && bytes.size() <= SegmentSize());
    active = false;
    if (!bytes.empty()) std::memcpy(TransferBuffer().addr_, bytes.data(), bytes.size());
    OnTransferCompleteCallback(true, bytes.size());
  }

 protected:
  void ConfigureHardware(const Config& cfg) override
  {
    GetConfig().max_packet_size =
        cfg.max_packet_size == UINT16_MAX ? 64 : cfg.max_packet_size;
    SetState(State::IDLE);
  }
  void CloseHardware() override { active = false; }
  ErrorCode StallHardware() override
  {
    active = false;
    return ErrorCode::OK;
  }
  ErrorCode ClearStallHardware() override { return ErrorCode::OK; }
  size_t MaxHardwareTransferSize() const override { return segment_limit; }
  ErrorCode StartHardware(RawData buffer, size_t size) override
  {
    TEST_ASSERT(!active && GetState() == State::BUSY && size <= buffer.size_);
    Start record{buffer.addr_, size, {}};
    if (GetDirection() == Direction::IN && size)
      record.data.assign(static_cast<uint8_t*>(buffer.addr_),
                         static_cast<uint8_t*>(buffer.addr_) + size);
    starts.push_back(record);
    active = true;
    if (at_start) at_start();
    return ErrorCode::OK;
  }
};

struct Fixture
{
  std::array<uint8_t, 128> in_storage{};
  std::array<uint8_t, 64> out_storage{};
  std::array<uint8_t, 32> comm_storage{};
  EndpointPool pool{4};
  MockEndpoint in{Number::EP1, Direction::IN, {in_storage.data(), in_storage.size()}};
  MockEndpoint out{Number::EP2, Direction::OUT, {out_storage.data(), out_storage.size()}};
  MockEndpoint comm{
      Number::EP3, Direction::IN, {comm_storage.data(), comm_storage.size()}};
  Fixture()
  {
    TEST_ASSERT(pool.Put(&in) == ErrorCode::OK);
    TEST_ASSERT(pool.Put(&out) == ErrorCode::OK);
    TEST_ASSERT(pool.Put(&comm) == ErrorCode::OK);
  }
};

class TestCDC : public CDCUart
{
 public:
  explicit TestCDC(size_t receive = 128)
      : CDCUart(Number::EP1, Number::EP2, Number::EP3, receive, 1024, 32)
  {
  }
  using CDCUart::BindEndpoints;
  using CDCUart::UnbindEndpoints;
};

std::vector<uint8_t> Bytes(size_t size, uint8_t seed)
{
  std::vector<uint8_t> data(size);
  for (size_t i = 0; i != size; ++i) data[i] = static_cast<uint8_t>(seed + i);
  return data;
}

void CheckData(const MockEndpoint& ep, const std::vector<uint8_t>& expected)
{
  std::vector<uint8_t> data;
  for (const auto& packet : ep.sent)
    data.insert(data.end(), packet.begin(), packet.end());
  if (data != expected)
  {
    size_t offset = 0;
    while (offset < data.size() && offset < expected.size() &&
           data[offset] == expected[offset])
      ++offset;
    std::fprintf(stderr, "wire mismatch at %zu: actual=%zu expected=%zu\n", offset,
                 data.size(), expected.size());
  }
  TEST_ASSERT(data == expected);
}

void TestCDCPrewriteAndZero()
{
  Fixture f;
  TestCDC cdc;
  cdc.BindEndpoints(f.pool, 0, false);
  unsigned finished = 0;
  auto complete = Callback<ErrorCode>::Create(
      [](bool, unsigned* count, ErrorCode result)
      {
        TEST_ASSERT(result == ErrorCode::OK);
        ++*count;
      },
      &finished);
  WriteOperation op(complete);
  auto bytes = Bytes(150, 7);
  TEST_ASSERT(cdc.Write({bytes.data(), bytes.size()}, op) == ErrorCode::OK);
  TEST_ASSERT(f.in.starts.size() == 1 && f.in.GetActiveLength() == 64);
  TEST_ASSERT(finished == 0);
  f.in.Complete();
  TEST_ASSERT(f.in.starts.size() == 2 && f.in.GetActiveLength() == 22);
  TEST_ASSERT(finished == 1);
  f.in.Complete();
  f.in.Complete();
  TEST_ASSERT(f.in.GetState() == State::IDLE && f.in.starts.size() == 3);
  CheckData(f.in, bytes);

  auto full = Bytes(64, 31);
  TEST_ASSERT(cdc.Write({full.data(), full.size()}, op) == ErrorCode::OK);
  f.in.Complete();
  TEST_ASSERT(f.in.GetState() == State::BUSY && f.in.starts.back().length == 0);
  auto later = Bytes(17, 84);
  TEST_ASSERT(cdc.Write({later.data(), later.size()}, op) == ErrorCode::OK);
  TEST_ASSERT(finished == 3);  // Stable PREPARED data can finish during the ZLP.
  TEST_ASSERT(f.in.GetActiveLength() == 17 && f.in.starts.back().length == 0);
  f.in.Complete();
  TEST_ASSERT(f.in.starts.back().length == 17);
  f.in.Complete();
  TEST_ASSERT(f.in.GetState() == State::IDLE && finished == 3);
}

void TestLateWriteAndCallbackReentry()
{
  Fixture f;
  TestCDC cdc;
  cdc.BindEndpoints(f.pool, 0, false);
  auto a = Bytes(64, 1), b = Bytes(23, 99);
  WriteOperation plain;
  TEST_ASSERT(cdc.Write({a.data(), a.size()}, plain) == ErrorCode::OK);
  TEST_ASSERT(f.in.GetActiveLength() == 0);
  TEST_ASSERT(cdc.Write({b.data(), b.size()}, plain) == ErrorCode::OK);
  TEST_ASSERT(f.in.GetActiveLength() == b.size());
  f.in.Complete();
  f.in.Complete();
  TEST_ASSERT(f.in.GetState() == State::IDLE);  // Short B suppresses A's old tail.
  std::vector<uint8_t> expected = a;
  expected.insert(expected.end(), b.begin(), b.end());
  CheckData(f.in, expected);

  struct Context
  {
    TestCDC* cdc;
    std::vector<uint8_t>* next;
    WriteOperation* op;
    unsigned called;
  };
  Context ctx{&cdc, &b, &plain, 0};
  auto recursive_callback = Callback<ErrorCode>::Create(
      [](bool in_isr, Context* ctx, ErrorCode result)
      {
        TEST_ASSERT(result == ErrorCode::OK);
        ++ctx->called;
        TEST_ASSERT(ctx->cdc->Write({ctx->next->data(), ctx->next->size()}, *ctx->op,
                                    in_isr) == ErrorCode::OK);
      },
      &ctx);
  WriteOperation recursive(recursive_callback);
  TEST_ASSERT(cdc.Write({a.data(), a.size()}, recursive) == ErrorCode::OK);
  TEST_ASSERT(ctx.called == 1 && f.in.GetActiveLength() == b.size());
  f.in.Complete();
  f.in.Complete();
  TEST_ASSERT(f.in.GetState() == State::IDLE);
}

void TestExactCompletionDuringFill()
{
  Fixture f;
  f.in.Configure({Direction::IN, Type::BULK, 64});
  struct Producer
  {
    MockEndpoint* ep;
    unsigned fills = 0, completions = 0;
    bool inject = true;
  } p{&f.in};
  f.in.SetOnTxFill(Callback<Endpoint::TxFill&>::Create(
      [](bool, Producer* p, Endpoint::TxFill& fill)
      {
        if (p->fills == 3) return;
        std::memset(fill.Buffer().addr_, ++p->fills, 64);
        fill.SetSize(64);
        if (p->fills == 2 && p->inject)
        {
          p->inject = false;
          p->ep->Complete();  // IRQ while the second producer callback still owns USB.
          TEST_ASSERT(p->completions == 0);
        }
      },
      &p));
  f.in.SetOnTransferCompleteCallback(Callback<ConstRawData&>::Create(
      [](bool, Producer* p, ConstRawData&) { ++p->completions; }, &p));
  f.in.RequestTx();
  TEST_ASSERT(p.fills == 3 && p.completions == 1 && f.in.starts.size() == 2);
  f.in.Complete();
  f.in.Complete();
  TEST_ASSERT(p.completions == 3 && f.in.GetState() == State::IDLE);
}

void TestReceiveRetentionAndSmallReadQueue()
{
  Fixture f;
  TestCDC cdc(16);
  cdc.BindEndpoints(f.pool, 0, false);
  const auto packet = Bytes(64, 19);
  f.out.Receive(packet);
  TEST_ASSERT(f.out.GetState() == State::RESULT);
  TEST_ASSERT(f.out.starts.size() == 1);  // No rearm while unread data remains.
  std::vector<uint8_t> received;
  ReadOperation op;
  for (unsigned i = 0; i < 4; ++i)
  {
    std::array<uint8_t, 16> data{};
    TEST_ASSERT(cdc.Read({data.data(), data.size()}, op) == ErrorCode::OK);
    received.insert(received.end(), data.begin(), data.end());
  }
  TEST_ASSERT(received == packet);
  TEST_ASSERT(f.out.GetState() == State::BUSY && f.out.starts.size() == 2);
}

void TestResetWhileFilling()
{
  Fixture f;
  f.in.Configure({Direction::IN, Type::BULK, 64});
  f.pool.SetControlHandler(Callback<uint32_t>::Create(
      [](bool, MockEndpoint* ep, uint32_t) { ep->Close(); }, &f.in));
  struct Producer
  {
    Fixture* f;
    unsigned calls = 0;
  } p{&f};
  f.in.SetOnTxFill(Callback<Endpoint::TxFill&>::Create(
      [](bool, Producer* p, Endpoint::TxFill& fill)
      {
        ++p->calls;
        std::memset(fill.Buffer().addr_, 0x61, 32);
        fill.SetSize(32);
        p->f->pool.PostControl(1U, true);
      },
      &p));
  f.in.RequestTx();
  TEST_ASSERT(p.calls == 1 && f.in.GetState() == State::DISABLED && f.in.starts.empty());
}

void TestReceiveSegmentsAndZero()
{
  Fixture f;
  f.out.Configure({Direction::OUT, Type::BULK, 64, 192});
  unsigned completed = 0;
  std::vector<uint8_t> received;
  struct Result
  {
    unsigned* count;
    std::vector<uint8_t>* bytes;
  } result{&completed, &received};
  f.out.SetOnTransferCompleteCallback(Callback<ConstRawData&>::Create(
      [](bool, Result* r, ConstRawData& data)
      {
        ++*r->count;
        if (data.size_)
          r->bytes->assign(static_cast<const uint8_t*>(data.addr_),
                           static_cast<const uint8_t*>(data.addr_) + data.size_);
      },
      &result));
  TEST_ASSERT(f.out.ArmReceive(192) == ErrorCode::OK);
  f.out.Receive(Bytes(64, 1));
  TEST_ASSERT(completed == 0);
  f.out.Receive(Bytes(20, 65));
  TEST_ASSERT(completed == 1 && received.size() == 84 && f.out.HasReceiveResult());
  const size_t starts = f.out.starts.size();
  f.out.RequestService();
  TEST_ASSERT(f.out.starts.size() == starts);
  TEST_ASSERT(f.out.ArmReceive(64) == ErrorCode::OK);
  f.out.Receive({});
  TEST_ASSERT(completed == 2 && f.out.ReceiveResult().size_ == 0);
}

struct BasicCapabilities : FullSpeedCapabilities
{
  static constexpr bool BOS = false;
  static constexpr bool BUS_TIME = false;
};
static_assert(sizeof(DeviceCore<BasicCapabilities>) <
              sizeof(DeviceCore<FullSpeedCapabilities>));

class ControlClass : public DeviceClass
{
 public:
  std::array<uint8_t, 256> input{}, output{};
  size_t response_size = 0, data_count = 0, complete_count = 0, abort_count = 0;
  size_t last_actual = 0;
  bool reject_data = false;
  std::vector<DeviceEvent> events;
  InterfaceDescriptor interface{};
  size_t GetControlReceiveCapacity() const override { return output.size(); }
  size_t GetInterfaceCount() override { return 1; }
  size_t GetMaxConfigSize() override { return sizeof(interface); }
  bool HasIAD() override { return false; }
  void BindEndpoints(EndpointPool&, uint8_t number, bool) override
  {
    interface = {9, 4, number, 0, 0, 0xff, 0, 0, 0};
    SetData({&interface, sizeof(interface)});
  }
  void UnbindEndpoints(EndpointPool&, bool) override {}
  void OnDeviceEvent(bool, DeviceEvent event, uint8_t) override
  {
    events.push_back(event);
  }

 protected:
  ErrorCode OnControlRequest(bool, const SetupPacket& setup,
                             ControlTransferResult& result) override
  {
    if (setup.bRequest == 0x70 && (setup.bmRequestType & 0x80U))
      result.InData() = {input.data(), response_size};
    else if (setup.bRequest == 0x71 && !(setup.bmRequestType & 0x80U))
      result.OutData() = {output.data(), output.size()};
    else if (setup.bRequest != 0x72 || setup.wLength != 0U)
      return ErrorCode::NOT_SUPPORT;
    return ErrorCode::OK;
  }
  ErrorCode OnControlData(bool, const SetupPacket& setup, ConstRawData& data) override
  {
    if (!(setup.bmRequestType & 0x80U))
    {
      ++data_count;
      last_actual = data.size_;
      TEST_ASSERT(std::equal(static_cast<const uint8_t*>(data.addr_),
                             static_cast<const uint8_t*>(data.addr_) + data.size_,
                             output.begin()));
    }
    return reject_data ? ErrorCode::ARG_ERR : ErrorCode::OK;
  }
  void OnControlComplete(bool, const SetupPacket&) override { ++complete_count; }
  void OnControlAbort(bool, const SetupPacket&) override { ++abort_count; }
};

static constexpr auto TEST_LANGUAGE = DescriptorStrings::MakeLanguagePack(
    DescriptorStrings::Language::EN_US, "LibXR", "USB tests", "0");

class TestDevice : public DeviceCore<BasicCapabilities>
{
 public:
  TestDevice(EndpointPool& pool, DeviceClass& klass)
      : DeviceCore(pool, USBSpec::USB_2_0, Speed::FULL,
                   DeviceDescriptor::PacketSize0::SIZE_64, 0x1209, 0x4321, 0x0100,
                   {&TEST_LANGUAGE}, {{&klass}})
  {
  }
  void Start(bool) override {}
  void Stop(bool) override {}
  std::vector<ControlContext> address_phases;

 protected:
  ErrorCode SetAddress(uint8_t, ControlContext context) override
  {
    address_phases.push_back(context);
    return ErrorCode::OK;
  }
};

void TestControlTransactions()
{
  Fixture f;
  std::array<uint8_t, 64> in_bytes{}, out_bytes{};
  MockEndpoint in0(Number::EP0, Direction::IN, {in_bytes.data(), in_bytes.size()});
  MockEndpoint out0(Number::EP0, Direction::OUT, {out_bytes.data(), out_bytes.size()});
  f.pool.SetEndpoint0(&in0, &out0);
  ControlClass klass;
  TestDevice device(f.pool, klass);
  device.Init(false);
  TEST_ASSERT(device.IsInited() && klass.events.empty());
  auto setup = [&](uint8_t kind, uint8_t request, uint16_t value, uint16_t length)
  {
    // USB SETUP cancels the old hardware control transfer independently from
    // software dispatch. The production core must then cancel old class state.
    in0.active = false;
    out0.active = false;
    SetupPacket packet{kind, request, value, 0, length};
    device.OnSetupPacket(true, &packet);
  };
  setup(0x80, 8, 0, 1);
  TEST_ASSERT(in0.starts.back().data == std::vector<uint8_t>{0});
  in0.Complete();
  out0.Receive({});
  setup(0, 9, 1, 0);
  TEST_ASSERT(klass.events.size() == 1 && klass.events.back() == DeviceEvent::CONFIGURED);
  TEST_ASSERT(in0.active && in0.starts.back().length == 0);
  in0.Complete();

  // Empty IN is data, not "no action" or a STALL.
  klass.response_size = 0;
  setup(0xa1, 0x70, 0, 8);
  TEST_ASSERT(in0.active && in0.starts.back().length == 0 && klass.complete_count == 0);
  in0.Complete();
  TEST_ASSERT(klass.complete_count == 0);
  out0.Receive({});
  TEST_ASSERT(klass.complete_count == 1);

  // Full-packet short response needs one real zero packet, then true status.
  klass.response_size = 64;
  for (unsigned i = 0; i < 64; ++i) klass.input[i] = static_cast<uint8_t>(i);
  setup(0xa1, 0x70, 0, 128);
  in0.Complete();
  TEST_ASSERT(in0.active && in0.starts.back().length == 0);
  out0.Receive({});
  TEST_ASSERT(klass.complete_count == 1);
  in0.Complete();
  TEST_ASSERT(klass.complete_count == 2);

  setup(0x21, 0x71, 0, 150);
  auto a = Bytes(64, 1), b = Bytes(64, 65), c = Bytes(22, 129);
  out0.Receive(a);
  out0.Receive(b);
  TEST_ASSERT(klass.data_count == 0 && klass.complete_count == 2);
  out0.Receive(c);
  TEST_ASSERT(klass.data_count == 1 && klass.last_actual == 150);
  TEST_ASSERT(klass.complete_count == 2 && in0.active && in0.starts.back().length == 0);
  in0.Complete();
  TEST_ASSERT(klass.complete_count == 3);

  // A handler rejection is not acknowledged as a successful status stage.
  klass.reject_data = true;
  setup(0x21, 0x71, 0, 7);
  out0.Receive(Bytes(7, 17));
  TEST_ASSERT(in0.IsStalled() && out0.IsStalled() && klass.complete_count == 3);
  TEST_ASSERT(klass.abort_count == 1);
  klass.reject_data = false;

  setup(0x21, 0x71, 0, 150);
  out0.Receive(a);
  setup(0x21, 0x72, 0, 0);  // New SETUP cancels unfinished data, not a fake completion.
  TEST_ASSERT(klass.abort_count == 2 && klass.data_count == 2);
  in0.Complete();
  TEST_ASSERT(klass.complete_count == 4);

  device.OnSuspend(true);
  TEST_ASSERT(device.IsInited() && klass.events.back() == DeviceEvent::SUSPENDED);
  device.OnResume(true);
  TEST_ASSERT(klass.events.back() == DeviceEvent::RESUMED);
  setup(0x80, 8, 0, 1);
  TEST_ASSERT(in0.starts.back().data[0] == 1);
  in0.Complete();
  out0.Receive({});
  device.OnBusReset(true);
  TEST_ASSERT(device.IsInited() && klass.events.back() == DeviceEvent::RESET);
  setup(0x80, 8, 0, 1);
  TEST_ASSERT(in0.starts.back().data[0] == 0);
  in0.Complete();
  out0.Receive({});
}

class TestFlash : public Flash
{
 public:
  std::array<uint8_t, 1024> bytes;
  unsigned erases = 0, writes = 0;
  std::function<void()> on_erase;
  TestFlash() : Flash(128, 1, {bytes.data(), bytes.size()}) { bytes.fill(0xff); }
  ErrorCode Erase(size_t offset, size_t size) override
  {
    TEST_ASSERT(offset <= bytes.size() && size <= bytes.size() - offset);
    ++erases;
    if (on_erase) on_erase();
    std::fill(bytes.begin() + offset, bytes.begin() + offset + size, 0xff);
    return ErrorCode::OK;
  }
  ErrorCode Write(size_t offset, ConstRawData data) override
  {
    TEST_ASSERT(offset <= bytes.size() && data.size_ <= bytes.size() - offset);
    ++writes;
    const auto* source = static_cast<const uint8_t*>(data.addr_);
    for (size_t i = 0; i < data.size_; ++i) bytes[offset + i] &= source[i];
    return ErrorCode::OK;
  }
  ErrorCode Read(size_t offset, RawData data) override
  {
    TEST_ASSERT(offset <= bytes.size() && data.size_ <= bytes.size() - offset);
    std::memcpy(data.addr_, bytes.data() + offset, data.size_);
    return ErrorCode::OK;
  }
};

void TestDFUWorkerOwnership()
{
  TestFlash flash;
  DfuBootloaderBackend backend(flash, 0, 512, 480, nullptr);
  auto block = Bytes(80, 9);
  uint32_t poll = 0;
  TEST_ASSERT(backend.DfuDownload(0, 0, {block.data(), block.size()}, poll) ==
              DFUStatusCode::OK);
  TEST_ASSERT(!backend.RunPendingWork());  // Merely receiving a block cannot erase.
  backend.DfuAbort(0);
  backend.AllowPendingWork();
  TEST_ASSERT(!backend.RunPendingWork() && flash.erases == 0);

  TEST_ASSERT(backend.DfuDownload(0, 0, {block.data(), block.size()}, poll) ==
              DFUStatusCode::OK);
  backend.AllowPendingWork();
  TEST_ASSERT(backend.RunPendingWork() && flash.writes == 1);
  TEST_ASSERT(backend.HasPendingWork());  // Result publication is not slot release.
  TEST_ASSERT(backend.DfuDownload(0, 1, {block.data(), block.size()}, poll) ==
              DFUStatusCode::ERR_NOTDONE);
  backend.CommitWorkResult();
  TEST_ASSERT(!backend.HasPendingWork());
  backend.DfuAbort(0);

  std::atomic<bool> entered{false}, release{false};
  flash.on_erase = [&]
  {
    entered.store(true, std::memory_order_release);
    while (!release.load(std::memory_order_acquire)) std::this_thread::yield();
  };
  TEST_ASSERT(backend.DfuDownload(0, 0, {block.data(), block.size()}, poll) ==
              DFUStatusCode::OK);
  backend.AllowPendingWork();
  const auto old_writes = flash.writes;
  std::thread worker([&] { TEST_ASSERT(backend.RunPendingWork()); });
  while (!entered.load(std::memory_order_acquire)) std::this_thread::yield();
  backend.DfuAbort(0);
  TEST_ASSERT(backend.HasPendingWork());
  TEST_ASSERT(backend.DfuDownload(0, 0, {block.data(), block.size()}, poll) ==
              DFUStatusCode::ERR_NOTDONE);
  release.store(true, std::memory_order_release);
  worker.join();
  TEST_ASSERT(flash.writes == old_writes && backend.HasPendingWork());
  backend.CommitWorkResult();
  TEST_ASSERT(!backend.HasPendingWork());
  flash.on_erase = {};

  // Cancellation after the manifest erase call reaches its safe boundary must
  // prevent the old session from issuing the following seal write.
  TestFlash manifest_flash;
  DfuBootloaderBackend manifest_backend(manifest_flash, 0, 1024, 896, nullptr);
  auto manifest_block = Bytes(64, 33);
  TEST_ASSERT(manifest_backend.DfuDownload(
                  0, 0, {manifest_block.data(), manifest_block.size()}, poll) ==
              DFUStatusCode::OK);
  manifest_backend.AllowPendingWork();
  TEST_ASSERT(manifest_backend.RunPendingWork());
  manifest_backend.CommitWorkResult();
  const unsigned writes_before_manifest = manifest_flash.writes;
  TEST_ASSERT(manifest_backend.DfuManifest(0, poll) == DFUStatusCode::OK);
  manifest_backend.AllowPendingWork();
  entered.store(false, std::memory_order_release);
  release.store(false, std::memory_order_release);
  manifest_flash.on_erase = [&]
  {
    entered.store(true, std::memory_order_release);
    while (!release.load(std::memory_order_acquire)) std::this_thread::yield();
  };
  std::thread manifest_worker(
      [&] { TEST_ASSERT(manifest_backend.RunPendingWork()); });
  while (!entered.load(std::memory_order_acquire)) std::this_thread::yield();
  manifest_backend.DfuAbort(0);
  release.store(true, std::memory_order_release);
  manifest_worker.join();
  TEST_ASSERT(manifest_flash.writes == writes_before_manifest);
  manifest_backend.CommitWorkResult();
  TEST_ASSERT(!manifest_backend.HasPendingWork());
  manifest_flash.on_erase = {};

  // Preserve the prior seal/payload same-erase-block repair.
  const auto firmware = Bytes(480, 51);
  unsigned block_number = 0;
  for (size_t offset = 0; offset < firmware.size(); offset += 128)
  {
    const size_t size = std::min<size_t>(128, firmware.size() - offset);
    TEST_ASSERT(backend.DfuDownload(0, static_cast<uint16_t>(block_number++),
                                    {firmware.data() + offset, size},
                                    poll) == DFUStatusCode::OK);
    backend.AllowPendingWork();
    TEST_ASSERT(backend.RunPendingWork());
    backend.CommitWorkResult();
  }
  const unsigned erased_before_seal = flash.erases;
  TEST_ASSERT(backend.DfuManifest(0, poll) == DFUStatusCode::OK);
  backend.AllowPendingWork();
  TEST_ASSERT(backend.RunPendingWork());
  backend.CommitWorkResult();
  TEST_ASSERT(flash.erases == erased_before_seal);
  TEST_ASSERT(backend.HasValidImage() && backend.ImageSize() == firmware.size());
  TEST_ASSERT(std::equal(firmware.begin(), firmware.end(), flash.bytes.begin()));
  DfuBootloaderBackend recovered(flash, 0, 512, 480, nullptr);
  TEST_ASSERT(recovered.HasValidImage() && recovered.ImageSize() == firmware.size());
}

void TestDFUProtocolTimer()
{
  // LibXR objects and registered Timer nodes are initialization-lifetime objects.
  // Keep this fixture alive until process exit, and drive the real Timer manually
  // rather than create a scheduler thread whose timing would mask the boundaries.
  Timer::list_ = new LockFreeList();
  auto* f = new Fixture;
  auto* in_bytes = new std::array<uint8_t, 64>{};
  auto* out_bytes = new std::array<uint8_t, 64>{};
  auto* in0 =
      new MockEndpoint(Number::EP0, Direction::IN, {in_bytes->data(), in_bytes->size()});
  auto* out0 = new MockEndpoint(Number::EP0, Direction::OUT,
                                {out_bytes->data(), out_bytes->size()});
  f->pool.SetEndpoint0(in0, out0);
  auto* flash = new TestFlash;
  auto* launches = new unsigned{0};
  auto* dfu = new DfuBootloaderClassT<256>(
      *flash, 0, 1024, 992, +[](void* count) { ++*static_cast<unsigned*>(count); },
      launches);
  auto* device = new TestDevice(f->pool, *dfu);
  device->Init(false);
  TEST_ASSERT(device->IsInited());
  auto setup = [&](uint8_t kind, uint8_t request, uint16_t value, uint16_t length)
  {
    in0->active = false;
    out0->active = false;
    SetupPacket packet{kind, request, value, 0, length};
    device->OnSetupPacket(true, &packet);
  };
  setup(0, 9, 1, 0);
  in0->Complete();
  const auto bytes = Bytes(64, 7);
  setup(0x21, static_cast<uint8_t>(DFURequest::DNLOAD), 0, bytes.size());
  out0->Receive(bytes);
  Timer::Refresh();
  TEST_ASSERT(flash->writes == 0 && flash->erases == 0);
  in0->Complete();
  setup(0xa1, static_cast<uint8_t>(DFURequest::GETSTATUS), 0, 6);
  TEST_ASSERT(in0->starts.back().data[4] == static_cast<uint8_t>(DFUState::DFU_DNBUSY));
  Timer::Refresh();
  TEST_ASSERT(flash->writes == 0);
  in0->Complete();
  Timer::Refresh();
  TEST_ASSERT(flash->writes == 0);
  out0->Receive({});  // Only this actual STATUS OUT makes Flash runnable.
  Timer::Refresh();
  TEST_ASSERT(flash->writes == 1 && !dfu->HasPendingWork());
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  setup(0xa1, static_cast<uint8_t>(DFURequest::GETSTATUS), 0, 6);
  TEST_ASSERT(in0->starts.back().data[4] ==
              static_cast<uint8_t>(DFUState::DFU_DNLOAD_IDLE));
  in0->Complete();
  out0->Receive({});

  setup(0x21, static_cast<uint8_t>(DFURequest::DNLOAD), 1, 0);
  in0->Complete();
  setup(0xa1, static_cast<uint8_t>(DFURequest::GETSTATUS), 0, 6);
  TEST_ASSERT(in0->starts.back().data[4] == static_cast<uint8_t>(DFUState::DFU_MANIFEST));
  in0->Complete();
  out0->Receive({});
  Timer::Refresh();
  TEST_ASSERT(dfu->HasValidImage() && *launches == 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(55));
  setup(0xa1, static_cast<uint8_t>(DFURequest::GETSTATUS), 0, 6);
  TEST_ASSERT(in0->starts.back().data[4] ==
              static_cast<uint8_t>(DFUState::DFU_MANIFEST_WAIT_RESET));
  in0->Complete();
  TEST_ASSERT(!dfu->TryConsumeAppLaunch(0));
  out0->Receive({});
  TEST_ASSERT(*launches == 0);
  TEST_ASSERT(dfu->TryConsumeAppLaunch(0) && *launches == 1);
}

class TestHID : public HID<1, 8, 8>
{
 public:
  TestHID() : HID(Number::EP1, Number::EP_INVALID, false) {}
  using HID::BindEndpoints;
  using HID::SendInputReport;
  using HID::UnbindEndpoints;

 protected:
  ConstRawData GetReportDesc() override
  {
    static const uint8_t descriptor = 0;
    return {&descriptor, 1};
  }
};

void TestHIDStableInput()
{
  Fixture f;
  TestHID hid;
  hid.BindEndpoints(f.pool, 0, false);
  auto data = Bytes(8, 30);
  // Simulate a different current controller owner. The caller's temporary data
  // disappears before TX_WORK is drained, so a retained pointer would fail.
  f.pool.SetControlHandler(Callback<uint32_t>::Create(
      [](bool, TestHID* h, uint32_t)
      {
        auto temporary = Bytes(8, 30);
        TEST_ASSERT(h->SendInputReport({temporary.data(), temporary.size()}) ==
                    ErrorCode::OK);
        std::fill(temporary.begin(), temporary.end(), 0xee);
      },
      &hid));
  f.pool.PostControl(1U, false);
  TEST_ASSERT(f.in.starts.size() == 1 && f.in.starts.back().data == data);
  TEST_ASSERT(hid.SendInputReport({data.data(), data.size()}) == ErrorCode::BUSY);
  f.in.Complete();
  TEST_ASSERT(hid.SendInputReport({data.data(), data.size()}) == ErrorCode::OK);
  f.in.Complete();
}

class TestAudio : public UAC1MicrophoneQ<2, 16>
{
 public:
  TestAudio() : UAC1MicrophoneQ(Number::EP1, 44100) {}
  using UAC1MicrophoneQ::BindEndpoints;
  using UAC1MicrophoneQ::SetAltSetting;
};

void TestAudioIntegerFrames()
{
  Fixture f;
  f.in.segment_limit = 1024;
  TestAudio audio;
  audio.BindEndpoints(f.pool, 0, false);
  TEST_ASSERT(f.in.GetState() == State::DISABLED);  // Alt0 is genuinely inactive.
  TEST_ASSERT(audio.SetAltSetting(1, 1) == ErrorCode::OK);
  for (unsigned i = 0; i < 10; ++i) f.in.Complete();
  size_t bytes = 0;
  for (size_t i = 0; i < 10; ++i)
  {
    TEST_ASSERT(f.in.sent[i].size() == 176 || f.in.sent[i].size() == 180);
    bytes += f.in.sent[i].size();
  }
  TEST_ASSERT(bytes == 1764 && audio.UnderrunCount() >= 10);
  TEST_ASSERT(audio.SetAltSetting(1, 0) == ErrorCode::OK);
}

class TestHighSpeedDevice : public DeviceCore<HighSpeedCapabilities>
{
 public:
  TestHighSpeedDevice(EndpointPool& pool, DeviceClass& item)
      : DeviceCore(pool, USBSpec::USB_2_0, Speed::HIGH,
                   DeviceDescriptor::PacketSize0::SIZE_64, 0x1209, 0x4321, 0x100,
                   {&TEST_LANGUAGE}, {{&item}})
  {
  }
  Speed observed = Speed::HIGH;
  void Start(bool) override {}
  void Stop(bool) override {}

 protected:
  ErrorCode SetAddress(uint8_t, ControlContext) override { return ErrorCode::OK; }
  Speed ReadBusSpeed() const override { return observed; }
};

void TestSpeedProfiles()
{
  Fixture f;
  std::array<uint8_t, 64> a{}, b{};
  MockEndpoint in0(Number::EP0, Direction::IN, {a.data(), a.size()});
  MockEndpoint out0(Number::EP0, Direction::OUT, {b.data(), b.size()});
  f.pool.SetEndpoint0(&in0, &out0);
  TestCDC cdc;
  TestHighSpeedDevice device(f.pool, cdc);
  device.Init(false);
  TEST_ASSERT(device.IsInited());
  auto request = [&](uint16_t value, uint16_t length)
  {
    in0.active = false;
    out0.active = false;
    SetupPacket setup{0x80, 6, value, 0, length};
    device.OnSetupPacket(true, &setup);
  };
  request(0x0600, 10);
  TEST_ASSERT(in0.starts.back().data.size() == 10 && in0.starts.back().data[1] == 6);
  in0.Complete();
  out0.Receive({});
  request(0x0700, 255);
  TEST_ASSERT(in0.starts.back().data[1] == 7);
  while (in0.active) in0.Complete();
  out0.Receive({});
  device.observed = Speed::FULL;
  device.OnBusReset(true);
  TEST_ASSERT(device.IsInited() && f.pool.GetSpeed() == Speed::FULL);
  SetupPacket configure{0, 9, 1, 0, 0};
  device.OnSetupPacket(true, &configure);
  in0.Complete();
  TEST_ASSERT(f.in.MaxPacketSize() == 64 && f.out.MaxPacketSize() == 64);
  request(0x0700, 255);
  TEST_ASSERT(in0.starts.back().data[1] == 7);
  while (in0.active) in0.Complete();
  out0.Receive({});
}

void TestCrossCoreProducerAndIRQ()
{
  Fixture f;
  std::recursive_mutex hardware;
  f.pool.SetHardwareGuard(
      &hardware,
      +[](void* mutex) -> uintptr_t
      {
        static_cast<std::recursive_mutex*>(mutex)->lock();
        return 0;
      },
      +[](void* mutex, uintptr_t)
      { static_cast<std::recursive_mutex*>(mutex)->unlock(); });
  TestCDC cdc;
  cdc.BindEndpoints(f.pool, 0, false);
  std::atomic<unsigned> finished{0};
  auto callback = Callback<ErrorCode>::Create(
      [](bool, std::atomic<unsigned>* count, ErrorCode error)
      {
        TEST_ASSERT(error == ErrorCode::OK);
        count->fetch_add(1, std::memory_order_relaxed);
      },
      &finished);
  WriteOperation op(callback);
  std::atomic<bool> stop{false};
  std::thread irq(
      [&]
      {
        while (!stop.load(std::memory_order_acquire))
        {
          {
            EndpointPool::InterruptScope batch(f.pool);
            if (f.in.active) f.in.Complete(true);
          }
          std::this_thread::yield();
        }
      });
  std::vector<uint8_t> expected;
  for (unsigned i = 0; i < 1000; ++i)
  {
    auto packet = Bytes(1U + (i * 17U) % 173U, static_cast<uint8_t>(i));
    ErrorCode result;
    do
    {
      result = cdc.Write({packet.data(), packet.size()}, op);
      TEST_ASSERT(result == ErrorCode::OK || result == ErrorCode::FULL);
      if (result != ErrorCode::OK) std::this_thread::yield();
    } while (result != ErrorCode::OK);
    expected.insert(expected.end(), packet.begin(), packet.end());
    std::fill(packet.begin(), packet.end(), 0xa7);  // Input lifetime ends immediately.
  }
  // IDLE is temporarily observable while the owner retires one transfer before
  // promoting the prepared one. Completion of the last Write is also only stable
  // storage admission. Neither is a cross-core "all bytes on wire" barrier.
  while (finished.load(std::memory_order_acquire) != 1000U ||
         f.in.wire_bytes.load(std::memory_order_acquire) < expected.size())
    std::this_thread::yield();
  stop.store(true, std::memory_order_release);
  irq.join();
  while (f.in.active)
  {
    EndpointPool::InterruptScope batch(f.pool);
    f.in.Complete(true);
  }
  CheckData(f.in, expected);
}

class TestSWD : public Debug::Swd
{
 public:
  unsigned clock_changes = 0;
  void Close() override {}
  ErrorCode SetClockHz(uint32_t) override
  {
    ++clock_changes;
    return ErrorCode::OK;
  }
  ErrorCode LineReset() override { return ErrorCode::OK; }
  ErrorCode EnterSwd() override { return ErrorCode::OK; }
  ErrorCode Transfer(const Debug::SwdProtocol::Request&,
                     Debug::SwdProtocol::Response&) override
  {
    return ErrorCode::OK;
  }
  void IdleClocks(uint32_t) override {}
  ErrorCode SeqWriteBits(uint32_t, const uint8_t*) override { return ErrorCode::OK; }
  ErrorCode SeqReadBits(uint32_t, uint8_t*) override { return ErrorCode::OK; }
};
class TestDAP1 : public DapLinkV1Class<TestSWD>
{
 public:
  explicit TestDAP1(TestSWD& swd) : DapLinkV1Class(Number::EP1, Number::EP2, swd) {}
  using DapLinkV1Class::BindEndpoints;
};
class TestDAP2 : public DapLinkV2Class<TestSWD, 64, 4, 64>
{
 public:
  explicit TestDAP2(TestSWD& swd) : DapLinkV2Class(Number::EP1, Number::EP2, swd) {}
  using DapLinkV2Class::BindEndpoints;
};

template <typename DAP>
void TestDAPResponseOwnership()
{
  Fixture f;
  TestSWD swd;
  DAP dap(swd);
  dap.BindEndpoints(f.pool, 0, false);
  const unsigned initial = swd.clock_changes;
  // DAP_SWJ_Clock causes an observable side effect; every received command must
  // execute once even when Endpoint immediately asks for a second prewrite.
  const std::vector<uint8_t> command{0x11, 0x40, 0x42, 0x0f, 0};
  TEST_ASSERT(f.out.active);
  f.out.Receive(command);
  TEST_ASSERT(swd.clock_changes == initial + 1 && f.in.active);
  TEST_ASSERT(f.out.active);
  f.out.Receive(command);
  TEST_ASSERT(swd.clock_changes == initial + 2 && f.in.GetActiveLength() != 0);
  f.in.Complete();
  TEST_ASSERT(swd.clock_changes == initial + 2 && f.in.active);
  f.in.Complete();
  TEST_ASSERT(swd.clock_changes == initial + 2 && f.in.GetState() == State::IDLE);
}
}  // namespace

int main()
{
  TestCDCPrewriteAndZero();
  TestLateWriteAndCallbackReentry();
  TestExactCompletionDuringFill();
  TestReceiveRetentionAndSmallReadQueue();
  TestResetWhileFilling();
  TestReceiveSegmentsAndZero();
  TestControlTransactions();
  TestDFUWorkerOwnership();
  TestDFUProtocolTimer();
  TestHIDStableInput();
  TestAudioIntegerFrames();
  TestSpeedProfiles();
  TestCrossCoreProducerAndIRQ();
  TestDAPResponseOwnership<TestDAP1>();
  TestDAPResponseOwnership<TestDAP2>();
  std::puts(
      "USB endpoint/control: prewrite, ZLP, reentry, reset, RX, EP0 stages and lifecycle "
      "passed");
}
