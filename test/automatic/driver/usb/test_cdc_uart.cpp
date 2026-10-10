/**
 * @file test_cdc_uart.cpp
 * @brief CDC 发送唤醒与中断重入回归 / CDC TX wakeup and interrupt reentry regressions.
 *
 * 使用真实 CDC、WritePort 和端点完成状态机，模拟 Transfer 尚未返回时到达的 USB 中断，
 * 并检查发送串行化后完成回调上下文与 in_isr 一致、合并唤醒不丢数据。
 * Use the actual CDC, WritePort and endpoint completion state machines, injecting an
 * interrupt before the endpoint's Transfer call returns, and check that completion
 * callbacks report the claiming context and coalesced wakeups lose no data.
 */

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "cdc_uart.hpp"
#include "test_assert.hpp"

namespace
{
using LibXR::ErrorCode;
using LibXR::USB::CDCUart;
using LibXR::USB::Endpoint;
using LibXR::USB::EndpointPool;

class MockEndpoint : public Endpoint
{
 public:
  MockEndpoint(EPNumber number, Direction direction, LibXR::RawData buffer)
      : Endpoint(number, direction, buffer)
  {
  }

  void Configure(const Config& config) override
  {
    GetConfig() = config;
    GetConfig().max_packet_size = std::min<uint16_t>(config.max_packet_size, 64U);
    GetConfig().double_buffer = config.double_buffer && enable_double_buffer;
    SetState(State::IDLE);
    SetActiveLength(0U);
  }

  void Close() override { SetState(State::DISABLED); }
  ErrorCode Stall() override
  {
    SetState(State::STALLED);
    return ErrorCode::OK;
  }
  ErrorCode ClearStall() override
  {
    SetState(State::IDLE);
    return ErrorCode::OK;
  }

  ErrorCode Transfer(size_t size) override
  {
    TEST_ASSERT(GetState() == State::IDLE);
    TEST_ASSERT(size <= GetBuffer().size_);
    ++transfer_depth;
    max_transfer_depth = std::max(max_transfer_depth, transfer_depth);
    if (GetDirection() == Direction::IN)
    {
      const auto* bytes = static_cast<const uint8_t*>(GetBuffer().addr_);
      if (size != 0U)
      {
        transmitted.insert(transmitted.end(), bytes, bytes + size);
      }
      if (UseDoubleBuffer() && size != 0U)
      {
        SwitchBuffer();
      }
    }
    transfer_size = size;
    transfer_lengths.push_back(size);
    SetState(State::BUSY);
    const bool wants_inline =
        (size != 0U) ? (complete_once || complete_inline) : complete_inline_zlp;
    if (wants_inline && GetDirection() == Direction::IN)
    {
      complete_once = false;
      OnTransferCompleteCallback(true, size);
    }
    --transfer_depth;
    return ErrorCode::OK;
  }

  void Drain()
  {
    size_t remaining = 100U;
    while (GetState() == State::BUSY && remaining-- != 0U)
    {
      OnTransferCompleteCallback(true, transfer_size);
    }
    TEST_ASSERT(GetState() == State::IDLE);
  }

  bool complete_once = false;
  bool complete_inline = false;
  bool complete_inline_zlp = false;
  bool enable_double_buffer = true;
  unsigned transfer_depth = 0U;
  unsigned max_transfer_depth = 0U;
  size_t transfer_size = 0U;
  std::vector<size_t> transfer_lengths;
  std::vector<uint8_t> transmitted;
};

class TestCDC : public CDCUart
{
 public:
  TestCDC() : CDCUart(EPNumber::EP1, EPNumber::EP1, EPNumber::EP2, 128, 128, 3) {}
  void Bind(EndpointPool& pool, bool in_isr) { BindEndpoints(pool, 0, in_isr); }
  void Unbind(EndpointPool& pool, bool in_isr) { UnbindEndpoints(pool, in_isr); }

 private:
  using EPNumber = Endpoint::EPNumber;
};

struct Fixture
{
  Fixture()
  {
    TEST_ASSERT(pool.Put(&in) == ErrorCode::OK);
    TEST_ASSERT(pool.Put(&out) == ErrorCode::OK);
    TEST_ASSERT(pool.Put(&comm) == ErrorCode::OK);
  }

  std::array<uint8_t, 128> in_buffer{};
  std::array<uint8_t, 128> out_buffer{};
  std::array<uint8_t, 32> comm_buffer{};
  MockEndpoint in{Endpoint::EPNumber::EP1,
                  Endpoint::Direction::IN,
                  {in_buffer.data(), in_buffer.size()}};
  MockEndpoint out{Endpoint::EPNumber::EP1,
                   Endpoint::Direction::OUT,
                   {out_buffer.data(), out_buffer.size()}};
  MockEndpoint comm{Endpoint::EPNumber::EP2,
                    Endpoint::Direction::IN,
                    {comm_buffer.data(), comm_buffer.size()}};
  EndpointPool pool{3};
  TestCDC cdc;
};

std::array<uint8_t, 33> Packet(unsigned number)
{
  std::array<uint8_t, 33> packet{};
  for (size_t i = 0; i < packet.size(); ++i)
  {
    packet[i] = static_cast<uint8_t>(number * packet.size() + i);
  }
  return packet;
}

void CheckQueuedBind(bool rebind, bool in_isr)
{
  Fixture fixture;
  LibXR::WriteOperation operation;
  std::vector<uint8_t> expected;
  if (rebind)
  {
    fixture.cdc.Bind(fixture.pool, in_isr);
    const auto warmup = Packet(8);
    TEST_ASSERT(fixture.cdc.Write({warmup.data(), warmup.size()}, operation) ==
                ErrorCode::OK);
  }
  for (unsigned i = 0; i < 3U; ++i)
  {
    const auto packet = Packet(i);
    TEST_ASSERT(fixture.cdc.Write({packet.data(), packet.size()}, operation) ==
                ErrorCode::OK);
    expected.insert(expected.end(), packet.begin(), packet.end());
  }
  TEST_ASSERT(fixture.cdc.write_port_->Size() == 99U);
  if (rebind)
  {
    fixture.cdc.Unbind(fixture.pool, in_isr);
    TEST_ASSERT(fixture.cdc.write_port_->Size() == 99U);
    fixture.in.transfer_lengths.clear();
    fixture.in.transmitted.clear();
  }

  fixture.in.complete_once = true;
  fixture.cdc.Bind(fixture.pool, in_isr);
  fixture.in.Drain();
  TEST_ASSERT(fixture.in.transmitted == expected);
  TEST_ASSERT(fixture.cdc.write_port_->Size() == 0U);
  TEST_ASSERT(fixture.in.max_transfer_depth == 1U);

  const auto next = Packet(4);
  TEST_ASSERT(fixture.cdc.Write({next.data(), next.size()}, operation) == ErrorCode::OK);
  expected.insert(expected.end(), next.begin(), next.end());
  fixture.in.Drain();
  TEST_ASSERT(fixture.in.transmitted == expected);
}

void CheckCompletionReentry(size_t length, bool callback_write, bool double_buffer)
{
  Fixture fixture;
  fixture.in.enable_double_buffer = double_buffer;
  fixture.cdc.Bind(fixture.pool, false);
  std::vector<uint8_t> packet(length);
  for (size_t i = 0; i < length; ++i)
  {
    packet[i] = static_cast<uint8_t>(i);
  }
  struct CallbackContext
  {
    TestCDC* cdc;
    std::array<uint8_t, 33> packet = Packet(1);
    unsigned calls = 0U;
  } context{&fixture.cdc};
  auto callback = LibXR::Callback<ErrorCode>::Create(
      [](bool in_isr, CallbackContext* self, ErrorCode result)
      {
        TEST_ASSERT(result == ErrorCode::OK);
        ++self->calls;
        LibXR::WriteOperation operation;
        TEST_ASSERT(self->cdc->Write({self->packet.data(), self->packet.size()},
                                     operation, in_isr) == ErrorCode::OK);
      },
      &context);
  LibXR::WriteOperation callback_operation(callback);
  LibXR::WriteOperation plain_operation;
  auto& operation = callback_write ? callback_operation : plain_operation;
  fixture.in.complete_once = true;
  TEST_ASSERT(fixture.cdc.Write({packet.data(), packet.size()}, operation) ==
              ErrorCode::OK);

  std::vector<uint8_t> expected = packet;
  std::vector<size_t> expected_transfers{length};
  if (callback_write)
  {
    TEST_ASSERT(context.calls == 1U);
    expected.insert(expected.end(), context.packet.begin(), context.packet.end());
    expected_transfers.push_back(context.packet.size());
  }
  else if (length == 64U)
  {
    expected_transfers.push_back(0U);
  }
  fixture.in.Drain();
  TEST_ASSERT(fixture.in.transmitted == expected);
  TEST_ASSERT(fixture.in.transfer_lengths == expected_transfers);
  TEST_ASSERT(fixture.cdc.write_port_->Size() == 0U);
  TEST_ASSERT(fixture.in.max_transfer_depth == 1U);
}

void CheckRebindClearsPreparedData(bool in_isr)
{
  Fixture fixture;
  LibXR::WriteOperation operation;
  for (unsigned i = 0; i < 3U; ++i)
  {
    const auto packet = Packet(i);
    TEST_ASSERT(fixture.cdc.Write({packet.data(), packet.size()}, operation) ==
                ErrorCode::OK);
  }
  fixture.cdc.Bind(fixture.pool, in_isr);
  TEST_ASSERT(fixture.in.GetActiveLength() == 33U);
  TEST_ASSERT(fixture.cdc.write_port_->Size() == 33U);
  fixture.cdc.Unbind(fixture.pool, in_isr);
  TEST_ASSERT(fixture.in.GetActiveLength() == 0U);
  fixture.in.transmitted.clear();
  fixture.cdc.Bind(fixture.pool, in_isr);
  fixture.in.Drain();
  const auto queued = Packet(2);
  TEST_ASSERT(fixture.in.transmitted ==
              std::vector<uint8_t>(queued.begin(), queued.end()));
  TEST_ASSERT(fixture.cdc.write_port_->Size() == 0U);
}

void CheckRebindClearsPendingZlp(bool in_isr)
{
  Fixture fixture;
  fixture.cdc.Bind(fixture.pool, in_isr);
  const std::array<uint8_t, 64> packet{};
  LibXR::WriteOperation operation;
  TEST_ASSERT(fixture.cdc.Write({packet.data(), packet.size()}, operation) ==
              ErrorCode::OK);
  TEST_ASSERT(fixture.in.GetState() == Endpoint::State::BUSY);
  fixture.cdc.Unbind(fixture.pool, in_isr);
  fixture.in.transfer_lengths.clear();
  fixture.cdc.Bind(fixture.pool, in_isr);
  TEST_ASSERT(fixture.in.GetState() == Endpoint::State::IDLE);
  TEST_ASSERT(fixture.in.transfer_lengths.empty());
  const auto next = Packet(1);
  TEST_ASSERT(fixture.cdc.Write({next.data(), next.size()}, operation) == ErrorCode::OK);
  fixture.in.Drain();
  TEST_ASSERT(fixture.in.transfer_lengths == std::vector<size_t>{next.size()});
}

void CheckRepeatedInlineCompletion()
{
  Fixture fixture;
  fixture.in.complete_inline = true;
  fixture.cdc.Bind(fixture.pool, false);
  LibXR::WriteOperation operation;
  std::vector<uint8_t> expected;
  for (unsigned i = 0; i < 1024U; ++i)
  {
    const auto packet = Packet(i);
    TEST_ASSERT(fixture.cdc.Write({packet.data(), packet.size()}, operation) ==
                ErrorCode::OK);
    expected.insert(expected.end(), packet.begin(), packet.end());
  }
  fixture.in.Drain();
  TEST_ASSERT(fixture.in.transmitted == expected);
  TEST_ASSERT(fixture.cdc.write_port_->Size() == 0U);
  TEST_ASSERT(fixture.in.max_transfer_depth == 1U);
}

/**
 * 满包传输在 Transfer 返回前内联完成时，ZLP 仍须由后续的唤醒补发且只发一次。
 * When a full-packet transfer completes inline before Transfer returns, the pending
 * ZLP must still be sent exactly once by a later wakeup instead of being lost.
 */
void CheckInlineZlpStillSent()
{
  Fixture fixture;
  fixture.in.complete_inline = true;
  fixture.in.complete_inline_zlp = true;
  fixture.in.enable_double_buffer = false;
  fixture.cdc.Bind(fixture.pool, false);
  const std::array<uint8_t, 64> packet{};
  LibXR::WriteOperation operation;
  TEST_ASSERT(fixture.cdc.Write({packet.data(), packet.size()}, operation) ==
              ErrorCode::OK);
  fixture.in.Drain();
  TEST_ASSERT(fixture.in.transmitted.size() == 64U);
  const std::vector<size_t> expected_lengths{64U, 0U};
  TEST_ASSERT(fixture.in.transfer_lengths == expected_lengths);
  TEST_ASSERT(fixture.cdc.write_port_->Size() == 0U);
  TEST_ASSERT(fixture.in.max_transfer_depth == 1U);
}

/**
 * 完成回调内连续两次写入只产生一次合并唤醒，两个请求仍按 FIFO 全部发出。
 * Two writes issued from one completion callback coalesce into a single wakeup; both
 * requests must still be transmitted in FIFO order without data loss.
 */
void CheckCoalescedCallbackWrites(bool double_buffer)
{
  Fixture fixture;
  fixture.in.enable_double_buffer = double_buffer;
  fixture.cdc.Bind(fixture.pool, false);
  std::vector<uint8_t> packet(33);
  for (size_t i = 0; i < packet.size(); ++i)
  {
    packet[i] = static_cast<uint8_t>(i);
  }
  struct CallbackContext
  {
    TestCDC* cdc;
    std::array<uint8_t, 33> first = Packet(1);
    std::array<uint8_t, 33> second = Packet(2);
    unsigned calls = 0U;
  } context{&fixture.cdc};
  auto callback = LibXR::Callback<ErrorCode>::Create(
      [](bool in_isr, CallbackContext* self, ErrorCode result)
      {
        TEST_ASSERT(result == ErrorCode::OK);
        ++self->calls;
        LibXR::WriteOperation operation;
        TEST_ASSERT(self->cdc->Write({self->first.data(), self->first.size()}, operation,
                                     in_isr) == ErrorCode::OK);
        TEST_ASSERT(self->cdc->Write({self->second.data(), self->second.size()},
                                     operation, in_isr) == ErrorCode::OK);
      },
      &context);
  LibXR::WriteOperation callback_operation(callback);
  fixture.in.complete_once = true;
  TEST_ASSERT(fixture.cdc.Write({packet.data(), packet.size()}, callback_operation) ==
              ErrorCode::OK);
  TEST_ASSERT(context.calls == 1U);

  std::vector<uint8_t> expected = packet;
  expected.insert(expected.end(), context.first.begin(), context.first.end());
  expected.insert(expected.end(), context.second.begin(), context.second.end());
  fixture.in.Drain();
  TEST_ASSERT(fixture.in.transmitted == expected);
  TEST_ASSERT(fixture.cdc.write_port_->Size() == 0U);
  TEST_ASSERT(fixture.in.max_transfer_depth == 1U);
}

/**
 * 写完成回调收到的 in_isr 必须等于实际取得执行权的上下文：任务链路内结算为 false，
 * 由完成中断唤醒的一方结算为 true。
 * The write-completion callback must observe the in_isr of the context that actually
 * claimed execution: false when settled inside the task-claimed chain, true when the
 * completion interrupt wakeup claimed execution.
 */
void CheckCallbackContextMatchesCaller()
{
  Fixture fixture;
  fixture.cdc.Bind(fixture.pool, false);
  struct CallbackContext
  {
    std::vector<bool> seen_isr;
  } context;
  auto callback = LibXR::Callback<ErrorCode>::Create(
      [](bool in_isr, CallbackContext* self, ErrorCode result)
      {
        TEST_ASSERT(result == ErrorCode::OK);
        self->seen_isr.push_back(in_isr);
      },
      &context);
  LibXR::WriteOperation callback_operation(callback);

  const auto first = Packet(0);
  TEST_ASSERT(fixture.cdc.Write({first.data(), first.size()}, callback_operation) ==
              ErrorCode::OK);
  const auto second = Packet(1);
  TEST_ASSERT(fixture.cdc.Write({second.data(), second.size()}, callback_operation) ==
              ErrorCode::OK);
  fixture.in.Drain();
  TEST_ASSERT(fixture.in.transmitted.size() == 66U);
  TEST_ASSERT(fixture.cdc.write_port_->Size() == 0U);
  TEST_ASSERT(context.seen_isr.size() == 2U);
  TEST_ASSERT(context.seen_isr[0] == false);
  TEST_ASSERT(context.seen_isr[1] == true);
}
}  // namespace

void test_usb_cdc_tx()
{
  for (const bool in_isr : {false, true})
  {
    CheckQueuedBind(false, in_isr);
    CheckQueuedBind(true, in_isr);
    CheckRebindClearsPreparedData(in_isr);
    CheckRebindClearsPendingZlp(in_isr);
  }
  for (const bool double_buffer : {false, true})
  {
    CheckCompletionReentry(33U, false, double_buffer);
    CheckCompletionReentry(64U, false, double_buffer);
    CheckCompletionReentry(33U, true, double_buffer);
    CheckCoalescedCallbackWrites(double_buffer);
  }
  CheckRepeatedInlineCompletion();
  CheckInlineZlpStillSent();
  CheckCallbackContextMatchesCaller();
}
