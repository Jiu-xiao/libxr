#include <array>
#include <barrier>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

#include "cdc_uart.hpp"
#include "test.hpp"

namespace
{

using LibXR::ConstRawData;
using LibXR::ErrorCode;
using LibXR::RawData;
using LibXR::USB::Endpoint;
using LibXR::USB::EndpointPool;

class FakeEndpoint final : public Endpoint
{
 public:
  static constexpr uint16_t BULK_PACKET_SIZE = 4U;

  FakeEndpoint(EPNumber number, Direction direction, RawData storage)
      : Endpoint(number, direction, storage)
  {
  }

  void Configure(const Config& config) override
  {
    GetConfig() = config;
    const size_t capacity = GetBuffer().size_;
    const size_t configured_capacity =
        (config.type == Type::BULK && capacity > BULK_PACKET_SIZE) ? BULK_PACKET_SIZE
                                                                   : capacity;
    if (GetConfig().max_packet_size > configured_capacity)
    {
      GetConfig().max_packet_size = static_cast<uint16_t>(configured_capacity);
    }
    SetState(State::IDLE);
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
    if (GetState() == State::BUSY)
    {
      return ErrorCode::BUSY;
    }

    RawData buffer = GetBuffer();
    if (size > buffer.size_)
    {
      return ErrorCode::NO_BUFF;
    }

    if (fail_next_in_transfer_ && GetDirection() == Direction::IN)
    {
      fail_next_in_transfer_ = false;
      return ErrorCode::FAILED;
    }

    if (GetDirection() == Direction::IN)
    {
      const auto* bytes = static_cast<const uint8_t*>(buffer.addr_);
      accepted_in_payloads_.emplace_back(bytes, bytes + size);
      if (UseDoubleBuffer() && size > 0U)
      {
        SwitchBuffer();
      }
    }

    last_transfer_size_ = size;
    SetState(State::BUSY);
    if (complete_in_before_return_ && GetDirection() == Direction::IN)
    {
      OnTransferCompleteCallback(false, last_transfer_size_);
    }
    else if (!synchronous_out_payload_.empty() && GetDirection() == Direction::OUT)
    {
      ASSERT(synchronous_out_payload_.size() <= last_transfer_size_);
      const size_t received_size = synchronous_out_payload_.size();
      LibXR::Memory::FastCopy(buffer.addr_, synchronous_out_payload_.data(),
                              received_size);
      synchronous_out_payload_.clear();
      OnTransferCompleteCallback(false, received_size);
    }
    return ErrorCode::OK;
  }

  void SetCompleteInBeforeTransferReturns(bool enabled)
  {
    complete_in_before_return_ = enabled;
  }

  void FailNextInTransfer()
  {
    ASSERT(GetDirection() == Direction::IN);
    fail_next_in_transfer_ = true;
  }

  void CompleteNextOutBeforeTransferReturns(ConstRawData data)
  {
    ASSERT(GetDirection() == Direction::OUT);
    const auto* bytes = static_cast<const uint8_t*>(data.addr_);
    synchronous_out_payload_.assign(bytes, bytes + data.size_);
  }

  void CompleteInTransfer()
  {
    ASSERT(GetDirection() == Direction::IN);
    ASSERT(GetState() == State::BUSY);
    OnTransferCompleteCallback(false, last_transfer_size_);
  }

  void ReceiveOut(ConstRawData data)
  {
    ASSERT(GetDirection() == Direction::OUT);
    ASSERT(GetState() == State::BUSY);
    ASSERT(data.size_ <= last_transfer_size_);

    RawData buffer = GetBuffer();
    ASSERT(data.size_ <= buffer.size_);
    LibXR::Memory::FastCopy(buffer.addr_, data.addr_, data.size_);
    OnTransferCompleteCallback(false, data.size_);
  }

  const std::vector<std::vector<uint8_t>>& AcceptedInPayloads() const
  {
    return accepted_in_payloads_;
  }

 private:
  size_t last_transfer_size_ = 0U;
  bool complete_in_before_return_ = false;
  bool fail_next_in_transfer_ = false;
  std::vector<uint8_t> synchronous_out_payload_{};
  std::vector<std::vector<uint8_t>> accepted_in_payloads_;
};

class FakeEndpointPool final : public EndpointPool
{
 public:
  static constexpr Endpoint::EPNumber DATA_IN_EP = Endpoint::EPNumber::EP1;
  static constexpr Endpoint::EPNumber DATA_OUT_EP = Endpoint::EPNumber::EP2;
  static constexpr Endpoint::EPNumber COMM_IN_EP = Endpoint::EPNumber::EP3;

  FakeEndpointPool()
      : EndpointPool(4U),
        data_in(DATA_IN_EP, Endpoint::Direction::IN,
                RawData{data_in_storage_.data(), data_in_storage_.size()}),
        data_out(DATA_OUT_EP, Endpoint::Direction::OUT,
                 RawData{data_out_storage_.data(), data_out_storage_.size()}),
        comm_in(COMM_IN_EP, Endpoint::Direction::IN,
                RawData{comm_in_storage_.data(), comm_in_storage_.size()})
  {
    ASSERT(Put(&data_in) == ErrorCode::OK);
    ASSERT(Put(&data_out) == ErrorCode::OK);
    ASSERT(Put(&comm_in) == ErrorCode::OK);
  }

 private:
  alignas(size_t) std::array<uint8_t, 16U> data_in_storage_{};
  alignas(size_t) std::array<uint8_t, 16U> data_out_storage_{};
  alignas(size_t) std::array<uint8_t, 16U> comm_in_storage_{};

 public:
  FakeEndpoint data_in;
  FakeEndpoint data_out;
  FakeEndpoint comm_in;
};

class TestCDCUart final : public LibXR::USB::CDCUart
{
 public:
  explicit TestCDCUart(size_t rx_buffer_size = 16U)
      : CDCUart(FakeEndpointPool::DATA_IN_EP, FakeEndpointPool::DATA_OUT_EP,
                FakeEndpointPool::COMM_IN_EP, rx_buffer_size, 32U, 4U)
  {
  }

  void Bind(FakeEndpointPool& pool) { BindEndpoints(pool, 0U, false); }

  void Unbind(FakeEndpointPool& pool) { UnbindEndpoints(pool, false); }
};

void PendingReadSurvivesRebind()
{
  TestCDCUart cdc;
  FakeEndpointPool first_pool;
  FakeEndpointPool second_pool;
  cdc.Bind(first_pool);

  std::array<uint8_t, 4U> received{};
  LibXR::OperationPollingStatus status;
  LibXR::ReadOperation operation(status);
  ASSERT(cdc.Read(RawData{received.data(), received.size()}, operation) == ErrorCode::OK);
  ASSERT(status.Load() == LibXR::OperationPollingStatus::RUNNING);

  const std::array<uint8_t, 2U> prefix{0x11U, 0x22U};
  first_pool.data_out.ReceiveOut(ConstRawData{prefix.data(), prefix.size()});
  ASSERT(status.Load() == LibXR::OperationPollingStatus::RUNNING);

  cdc.Unbind(first_pool);
  ASSERT(status.Load() == LibXR::OperationPollingStatus::RUNNING);
  cdc.Bind(second_pool);

  const std::array<uint8_t, 2U> suffix{0x33U, 0x44U};
  second_pool.data_out.ReceiveOut(ConstRawData{suffix.data(), suffix.size()});
  ASSERT(status.Load() == LibXR::OperationPollingStatus::DONE);
  ASSERT((received == std::array<uint8_t, 4U>{0x11U, 0x22U, 0x33U, 0x44U}));

  cdc.Unbind(second_pool);
}

void QueuedRxBackpressureSurvivesRebind()
{
  TestCDCUart cdc;
  FakeEndpointPool first_pool;
  FakeEndpointPool second_pool;
  cdc.Bind(first_pool);

  const std::array<std::array<uint8_t, 4U>, 4U> packets{{
      {0x10U, 0x11U, 0x12U, 0x13U},
      {0x20U, 0x21U, 0x22U, 0x23U},
      {0x30U, 0x31U, 0x32U, 0x33U},
      {0x40U, 0x41U, 0x42U, 0x43U},
  }};
  for (const auto& packet : packets)
  {
    first_pool.data_out.ReceiveOut(ConstRawData{packet.data(), packet.size()});
  }
  ASSERT(first_pool.data_out.GetState() == Endpoint::State::IDLE);

  cdc.Unbind(first_pool);
  cdc.Bind(second_pool);
  ASSERT(second_pool.data_out.GetState() == Endpoint::State::IDLE);

  std::array<uint8_t, 4U> received{};
  LibXR::ReadOperation operation;
  for (const auto& packet : packets)
  {
    received.fill(0U);
    ASSERT(cdc.Read(RawData{received.data(), received.size()}, operation) ==
           ErrorCode::OK);
    ASSERT(received == packet);
  }
  ASSERT(second_pool.data_out.GetState() == Endpoint::State::BUSY);

  const std::array<uint8_t, 4U> resumed_packet{0x50U, 0x51U, 0x52U, 0x53U};
  second_pool.data_out.ReceiveOut(
      ConstRawData{resumed_packet.data(), resumed_packet.size()});
  received.fill(0U);
  ASSERT(cdc.Read(RawData{received.data(), received.size()}, operation) == ErrorCode::OK);
  ASSERT(received == resumed_packet);
  ASSERT(second_pool.data_out.GetState() == Endpoint::State::BUSY);

  cdc.Unbind(second_pool);
}

void RxSpaceReleaseAroundRearmCannotLoseWakeup()
{
  constexpr size_t ITERATIONS = 2000U;
  TestCDCUart cdc(FakeEndpoint::BULK_PACKET_SIZE);
  FakeEndpointPool pool;
  cdc.Bind(pool);

  const std::array<uint8_t, 4U> packet{0x10U, 0x11U, 0x12U, 0x13U};
  std::array<uint8_t, 4U> received{};
  LibXR::ReadOperation operation;
  std::barrier iteration_start{2};
  std::barrier iteration_done{2};
  std::thread consumer(
      [&]
      {
        for (size_t iteration = 0U; iteration < ITERATIONS; ++iteration)
        {
          iteration_start.arrive_and_wait();
          if ((iteration & 1U) == 0U)
          {
            std::this_thread::yield();
          }
          ASSERT(cdc.Read(RawData{received.data(), received.size()}, operation) ==
                 ErrorCode::OK);
          iteration_done.arrive_and_wait();
        }
      });

  for (size_t iteration = 0U; iteration < ITERATIONS; ++iteration)
  {
    received.fill(0U);
    pool.data_out.ReceiveOut(ConstRawData{packet.data(), packet.size()});
    ASSERT(pool.data_out.GetState() == Endpoint::State::IDLE);

    iteration_start.arrive_and_wait();
    if ((iteration & 1U) != 0U)
    {
      std::this_thread::yield();
    }
    (void)cdc.TryRearmOut(false);
    iteration_done.arrive_and_wait();
    ASSERT(received == packet);
    ASSERT(pool.data_out.GetState() == Endpoint::State::BUSY);
  }

  consumer.join();
  cdc.Unbind(pool);
}

void SynchronousOutCompletionPreservesBackpressureRecovery()
{
  TestCDCUart cdc(FakeEndpoint::BULK_PACKET_SIZE);
  FakeEndpointPool pool;
  cdc.Bind(pool);

  const std::array<uint8_t, 4U> first_packet{0x10U, 0x11U, 0x12U, 0x13U};
  pool.data_out.ReceiveOut(ConstRawData{first_packet.data(), first_packet.size()});
  ASSERT(pool.data_out.GetState() == Endpoint::State::IDLE);

  const std::array<uint8_t, 4U> synchronous_packet{0x20U, 0x21U, 0x22U, 0x23U};
  pool.data_out.CompleteNextOutBeforeTransferReturns(
      ConstRawData{synchronous_packet.data(), synchronous_packet.size()});

  std::array<uint8_t, 4U> received{};
  LibXR::ReadOperation operation;
  ASSERT(cdc.Read(RawData{received.data(), received.size()}, operation) == ErrorCode::OK);
  ASSERT(received == first_packet);
  ASSERT(pool.data_out.GetState() == Endpoint::State::IDLE);

  received.fill(0U);
  ASSERT(cdc.Read(RawData{received.data(), received.size()}, operation) == ErrorCode::OK);
  ASSERT(received == synchronous_packet);
  ASSERT(pool.data_out.GetState() == Endpoint::State::BUSY);

  const std::array<uint8_t, 4U> resumed_packet{0x30U, 0x31U, 0x32U, 0x33U};
  pool.data_out.ReceiveOut(ConstRawData{resumed_packet.data(), resumed_packet.size()});
  received.fill(0U);
  ASSERT(cdc.Read(RawData{received.data(), received.size()}, operation) == ErrorCode::OK);
  ASSERT(received == resumed_packet);

  cdc.Unbind(pool);
}

void AcceptedWritePrefixIsNotReplayedAfterRebind()
{
  TestCDCUart cdc;
  FakeEndpointPool first_pool;
  FakeEndpointPool second_pool;
  cdc.Bind(first_pool);

  const std::array<uint8_t, 18U> payload{0U, 1U,  2U,  3U,  4U,  5U,  6U,  7U,  8U,
                                         9U, 10U, 11U, 12U, 13U, 14U, 15U, 16U, 17U};
  LibXR::OperationPollingStatus status;
  LibXR::WriteOperation operation(status);
  ASSERT(cdc.Write(ConstRawData{payload.data(), payload.size()}, operation) ==
         ErrorCode::OK);
  ASSERT(status.Load() == LibXR::OperationPollingStatus::RUNNING);
  ASSERT(first_pool.data_in.AcceptedInPayloads().size() == 1U);
  ASSERT((first_pool.data_in.AcceptedInPayloads()[0] ==
          std::vector<uint8_t>{0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U}));

  cdc.Unbind(first_pool);
  cdc.Bind(second_pool);
  ASSERT(status.Load() == LibXR::OperationPollingStatus::RUNNING);
  ASSERT(second_pool.data_in.AcceptedInPayloads().size() == 1U);
  ASSERT((second_pool.data_in.AcceptedInPayloads()[0] ==
          std::vector<uint8_t>{8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U}));

  second_pool.data_in.CompleteInTransfer();
  ASSERT(status.Load() == LibXR::OperationPollingStatus::DONE);
  ASSERT(second_pool.data_in.AcceptedInPayloads().size() == 2U);
  ASSERT((second_pool.data_in.AcceptedInPayloads()[1] == std::vector<uint8_t>{16U, 17U}));

  std::vector<uint8_t> accepted;
  for (const auto& chunk : first_pool.data_in.AcceptedInPayloads())
  {
    accepted.insert(accepted.end(), chunk.begin(), chunk.end());
  }
  for (const auto& chunk : second_pool.data_in.AcceptedInPayloads())
  {
    accepted.insert(accepted.end(), chunk.begin(), chunk.end());
  }
  ASSERT(accepted == std::vector<uint8_t>(payload.begin(), payload.end()));

  cdc.Unbind(second_pool);
}

void FailedTransferRetriesOnlyUnacceptedSuffix()
{
  TestCDCUart cdc;
  FakeEndpointPool first_pool;
  FakeEndpointPool second_pool;
  cdc.Bind(first_pool);

  const std::array<uint8_t, 18U> payload{0U, 1U,  2U,  3U,  4U,  5U,  6U,  7U,  8U,
                                         9U, 10U, 11U, 12U, 13U, 14U, 15U, 16U, 17U};
  LibXR::OperationPollingStatus status;
  LibXR::WriteOperation operation(status);
  ASSERT(cdc.Write(ConstRawData{payload.data(), payload.size()}, operation) ==
         ErrorCode::OK);
  ASSERT(status.Load() == LibXR::OperationPollingStatus::RUNNING);
  ASSERT(first_pool.data_in.AcceptedInPayloads().size() == 1U);

  first_pool.data_in.CompleteInTransfer();
  ASSERT(first_pool.data_in.AcceptedInPayloads().size() == 2U);
  first_pool.data_in.FailNextInTransfer();
  first_pool.data_in.CompleteInTransfer();
  ASSERT(status.Load() == LibXR::OperationPollingStatus::RUNNING);
  ASSERT(first_pool.data_in.AcceptedInPayloads().size() == 2U);

  cdc.Unbind(first_pool);
  cdc.Bind(second_pool);
  ASSERT(status.Load() == LibXR::OperationPollingStatus::DONE);
  ASSERT(second_pool.data_in.AcceptedInPayloads().size() == 1U);
  ASSERT((second_pool.data_in.AcceptedInPayloads()[0] == std::vector<uint8_t>{16U, 17U}));

  std::vector<uint8_t> accepted;
  for (const auto& chunk : first_pool.data_in.AcceptedInPayloads())
  {
    accepted.insert(accepted.end(), chunk.begin(), chunk.end());
  }
  for (const auto& chunk : second_pool.data_in.AcceptedInPayloads())
  {
    accepted.insert(accepted.end(), chunk.begin(), chunk.end());
  }
  ASSERT(accepted == std::vector<uint8_t>(payload.begin(), payload.end()));

  cdc.Unbind(second_pool);
}

struct ReentrantWriteState
{
  TestCDCUart* cdc = nullptr;
  const std::array<uint8_t, 2U>* next_payload = nullptr;
  LibXR::WriteOperation* next_operation = nullptr;
  size_t completion_count = 0U;
  ErrorCode completion_result = ErrorCode::PENDING;
  ErrorCode nested_submit_result = ErrorCode::PENDING;
};

void SubmitNextWrite(bool, ReentrantWriteState* state, ErrorCode result)
{
  ++state->completion_count;
  state->completion_result = result;
  state->nested_submit_result = state->cdc->Write(
      ConstRawData{state->next_payload->data(), state->next_payload->size()},
      *state->next_operation);
}

struct CompletionState
{
  size_t count = 0U;
  ErrorCode result = ErrorCode::PENDING;
};

void RecordCompletion(bool, CompletionState* state, ErrorCode result)
{
  ++state->count;
  state->result = result;
}

struct ReentrantReadState
{
  TestCDCUart* cdc = nullptr;
  std::array<uint8_t, 2U>* next_buffer = nullptr;
  LibXR::ReadOperation* next_operation = nullptr;
  size_t completion_count = 0U;
  ErrorCode completion_result = ErrorCode::PENDING;
  ErrorCode nested_submit_result = ErrorCode::PENDING;
};

void SubmitNextRead(bool, ReentrantReadState* state, ErrorCode result)
{
  ++state->completion_count;
  state->completion_result = result;
  state->nested_submit_result =
      state->cdc->Read(RawData{state->next_buffer->data(), state->next_buffer->size()},
                       *state->next_operation);
}

void ReboundReadCallbackCanSubmitNextRead()
{
  TestCDCUart cdc;
  FakeEndpointPool first_pool;
  FakeEndpointPool second_pool;
  cdc.Bind(first_pool);

  std::array<uint8_t, 2U> next_received{};
  CompletionState next_completion;
  auto next_callback =
      LibXR::Callback<ErrorCode>::Create(RecordCompletion, &next_completion);
  LibXR::ReadOperation next_operation(next_callback);

  std::array<uint8_t, 4U> first_received{};
  ReentrantReadState first_completion;
  first_completion.cdc = &cdc;
  first_completion.next_buffer = &next_received;
  first_completion.next_operation = &next_operation;
  auto first_callback =
      LibXR::Callback<ErrorCode>::Create(SubmitNextRead, &first_completion);
  LibXR::ReadOperation first_operation(first_callback);

  ASSERT(cdc.Read(RawData{first_received.data(), first_received.size()},
                  first_operation) == ErrorCode::OK);
  const std::array<uint8_t, 2U> old_bytes{0x51U, 0x52U};
  first_pool.data_out.ReceiveOut(ConstRawData{old_bytes.data(), old_bytes.size()});
  ASSERT(first_completion.completion_count == 0U);

  cdc.Unbind(first_pool);
  cdc.Bind(second_pool);
  const std::array<uint8_t, 2U> rebound_bytes{0x53U, 0x54U};
  second_pool.data_out.ReceiveOut(
      ConstRawData{rebound_bytes.data(), rebound_bytes.size()});
  ASSERT(first_completion.completion_count == 1U);
  ASSERT(first_completion.completion_result == ErrorCode::OK);
  ASSERT(first_completion.nested_submit_result == ErrorCode::OK);
  ASSERT(next_completion.count == 0U);
  ASSERT((first_received == std::array<uint8_t, 4U>{0x51U, 0x52U, 0x53U, 0x54U}));

  const std::array<uint8_t, 2U> next_bytes{0x61U, 0x62U};
  second_pool.data_out.ReceiveOut(ConstRawData{next_bytes.data(), next_bytes.size()});
  ASSERT(first_completion.completion_count == 1U);
  ASSERT(next_completion.count == 1U);
  ASSERT(next_completion.result == ErrorCode::OK);
  ASSERT(next_received == next_bytes);

  cdc.Unbind(second_pool);
}

void SynchronousInCompletionPreservesStagingAndCompletesOnce()
{
  TestCDCUart cdc;
  FakeEndpointPool pool;
  cdc.Bind(pool);
  pool.data_in.SetCompleteInBeforeTransferReturns(true);

  const std::array<uint8_t, 18U> payload{0x30U, 0x31U, 0x32U, 0x33U, 0x34U, 0x35U,
                                         0x36U, 0x37U, 0x38U, 0x39U, 0x3AU, 0x3BU,
                                         0x3CU, 0x3DU, 0x3EU, 0x3FU, 0x40U, 0x41U};
  CompletionState completion;
  auto callback = LibXR::Callback<ErrorCode>::Create(RecordCompletion, &completion);
  LibXR::WriteOperation operation(callback);

  ASSERT(cdc.Write(ConstRawData{payload.data(), payload.size()}, operation) ==
         ErrorCode::OK);
  ASSERT(completion.count == 1U);
  ASSERT(completion.result == ErrorCode::OK);
  ASSERT(pool.data_in.GetState() == Endpoint::State::IDLE);
  ASSERT(pool.data_in.AcceptedInPayloads().size() == 3U);
  ASSERT((pool.data_in.AcceptedInPayloads()[0] ==
          std::vector<uint8_t>{0x30U, 0x31U, 0x32U, 0x33U, 0x34U, 0x35U, 0x36U, 0x37U}));
  ASSERT((pool.data_in.AcceptedInPayloads()[1] ==
          std::vector<uint8_t>{0x38U, 0x39U, 0x3AU, 0x3BU, 0x3CU, 0x3DU, 0x3EU, 0x3FU}));
  ASSERT((pool.data_in.AcceptedInPayloads()[2] == std::vector<uint8_t>{0x40U, 0x41U}));

  std::vector<uint8_t> accepted;
  for (const auto& chunk : pool.data_in.AcceptedInPayloads())
  {
    accepted.insert(accepted.end(), chunk.begin(), chunk.end());
  }
  ASSERT(accepted == std::vector<uint8_t>(payload.begin(), payload.end()));
  ASSERT(completion.count == 1U);

  cdc.Unbind(pool);
}

void ExactPacketWriteSendsZlpAndCompletesOnce()
{
  TestCDCUart cdc;
  FakeEndpointPool pool;
  cdc.Bind(pool);

  const std::array<uint8_t, 4U> payload{0x30U, 0x31U, 0x32U, 0x33U};
  CompletionState completion;
  auto callback = LibXR::Callback<ErrorCode>::Create(RecordCompletion, &completion);
  LibXR::WriteOperation operation(callback);

  ASSERT(cdc.Write(ConstRawData{payload.data(), payload.size()}, operation) ==
         ErrorCode::OK);
  ASSERT(completion.count == 1U);
  ASSERT(completion.result == ErrorCode::OK);
  ASSERT(pool.data_in.AcceptedInPayloads().size() == 1U);
  ASSERT(pool.data_in.AcceptedInPayloads()[0] ==
         std::vector<uint8_t>(payload.begin(), payload.end()));

  pool.data_in.CompleteInTransfer();
  ASSERT(completion.count == 1U);
  ASSERT(pool.data_in.AcceptedInPayloads().size() == 2U);
  ASSERT(pool.data_in.AcceptedInPayloads()[1].empty());

  pool.data_in.CompleteInTransfer();
  ASSERT(completion.count == 1U);
  ASSERT(pool.data_in.AcceptedInPayloads().size() == 2U);

  cdc.Unbind(pool);
}

void FinalAcceptedChunkCompletesOnceAndAllowsReentry()
{
  TestCDCUart cdc;
  FakeEndpointPool pool;
  cdc.Bind(pool);

  const std::array<uint8_t, 10U> first_payload{0x10U, 0x11U, 0x12U, 0x13U, 0x14U,
                                               0x15U, 0x16U, 0x17U, 0x18U, 0x19U};
  const std::array<uint8_t, 2U> next_payload{0x21U, 0x22U};

  CompletionState next_completion;
  auto next_callback =
      LibXR::Callback<ErrorCode>::Create(RecordCompletion, &next_completion);
  LibXR::WriteOperation next_operation(next_callback);

  ReentrantWriteState first_completion;
  first_completion.cdc = &cdc;
  first_completion.next_payload = &next_payload;
  first_completion.next_operation = &next_operation;
  auto first_callback =
      LibXR::Callback<ErrorCode>::Create(SubmitNextWrite, &first_completion);
  LibXR::WriteOperation first_operation(first_callback);

  ASSERT(cdc.Write(ConstRawData{first_payload.data(), first_payload.size()},
                   first_operation) == ErrorCode::OK);
  ASSERT(first_completion.completion_count == 0U);
  ASSERT(pool.data_in.AcceptedInPayloads().size() == 1U);

  pool.data_in.CompleteInTransfer();
  ASSERT(first_completion.completion_count == 1U);
  ASSERT(first_completion.completion_result == ErrorCode::OK);
  ASSERT(first_completion.nested_submit_result == ErrorCode::OK);
  ASSERT(next_completion.count == 0U);
  ASSERT(pool.data_in.AcceptedInPayloads().size() == 2U);
  ASSERT((pool.data_in.AcceptedInPayloads()[1] == std::vector<uint8_t>{0x18U, 0x19U}));

  pool.data_in.CompleteInTransfer();
  ASSERT(first_completion.completion_count == 1U);
  ASSERT(next_completion.count == 1U);
  ASSERT(next_completion.result == ErrorCode::OK);
  ASSERT(pool.data_in.AcceptedInPayloads().size() == 3U);
  ASSERT((pool.data_in.AcceptedInPayloads()[2] == std::vector<uint8_t>{0x21U, 0x22U}));

  pool.data_in.CompleteInTransfer();
  ASSERT(first_completion.completion_count == 1U);
  ASSERT(next_completion.count == 1U);
  ASSERT(pool.data_in.AcceptedInPayloads().size() == 3U);

  cdc.Unbind(pool);
}

}  // namespace

void test_cdc_uart_lifecycle()
{
  PendingReadSurvivesRebind();
  QueuedRxBackpressureSurvivesRebind();
  RxSpaceReleaseAroundRearmCannotLoseWakeup();
  SynchronousOutCompletionPreservesBackpressureRecovery();
  ReboundReadCallbackCanSubmitNextRead();
  AcceptedWritePrefixIsNotReplayedAfterRebind();
  FailedTransferRetriesOnlyUnacceptedSuffix();
  SynchronousInCompletionPreservesStagingAndCompletesOnce();
  ExactPacketWriteSendsZlpAndCompletesOnce();
  FinalAcceptedChunkCompletesOnceAndAllowsReentry();
}
