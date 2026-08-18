#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "cdc_uart.hpp"
#include "dev_core.hpp"
#include "test.hpp"

namespace
{

using LibXR::ConstRawData;
using LibXR::ErrorCode;
using LibXR::RawData;
using LibXR::USB::CDCBase;
using LibXR::USB::DescriptorStrings;
using LibXR::USB::DeviceCore;
using LibXR::USB::Endpoint;
using LibXR::USB::EndpointPool;
using LibXR::USB::Recipient;
using LibXR::USB::RequestDirection;
using LibXR::USB::RequestType;
using LibXR::USB::SetupPacket;
using LibXR::USB::Speed;
using LibXR::USB::StandardRequest;

constexpr auto TEST_LANGUAGE = DescriptorStrings::MakeLanguagePack(
    DescriptorStrings::Language::EN_US, "LibXR", "CDC owner test", "TEST");

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
    ++binding_epoch_;
    GetConfig() = config;
    GetConfig().double_buffer = config.double_buffer && double_buffer_supported_;
    const size_t capacity = GetBuffer().size_;
    const size_t configured_capacity =
        config.type == Type::BULK
            ? std::min(capacity, static_cast<size_t>(bulk_packet_size_))
            : capacity;
    if (GetConfig().max_packet_size > configured_capacity)
    {
      GetConfig().max_packet_size = static_cast<uint16_t>(configured_capacity);
    }
    SetState(State::IDLE);
  }

  void Close() override
  {
    completion_latched_ = false;
    last_transfer_size_ = 0U;
    ++binding_epoch_;
    SetState(State::DISABLED);
  }

  ErrorCode Stall() override
  {
    SetState(State::STALLED);
    return ErrorCode::OK;
  }

  ErrorCode ClearStall() override
  {
    ++binding_epoch_;
    SetState(State::IDLE);
    return ErrorCode::OK;
  }

  size_t MaxTransferSize() const override
  {
    if (max_transfer_size_ != 0U)
    {
      return max_transfer_size_;
    }
    return Endpoint::MaxTransferSize();
  }

  ErrorCode Transfer(size_t size) override
  {
    ++transfer_attempts_;
    ++transfer_depth_;
    max_transfer_depth_ = std::max(max_transfer_depth_, transfer_depth_);
    struct DepthGuard
    {
      size_t& depth;
      ~DepthGuard() { --depth; }
    } depth_guard{transfer_depth_};

    if (GetState() == State::BUSY)
    {
      return ErrorCode::BUSY;
    }

    RawData buffer = GetBuffer();
    if (size > buffer.size_ || size > MaxTransferSize())
    {
      return ErrorCode::NO_BUFF;
    }

    const bool fail_in = GetDirection() == Direction::IN &&
                         (fail_all_in_transfers_ || fail_next_in_transfers_ != 0U);
    const bool fail_transfer = fail_in || fail_next_transfers_ != 0U;
    if (fail_transfer)
    {
      if (fail_in && fail_next_in_transfers_ != 0U)
      {
        --fail_next_in_transfers_;
      }
      if (fail_next_transfers_ != 0U)
      {
        --fail_next_transfers_;
      }
      ++failed_in_attempts_;
      if (enter_error_on_failure_)
      {
        SetState(State::ERROR);
      }
      return ErrorCode::FAILED;
    }

    if (GetDirection() == Direction::IN)
    {
      const auto* bytes = static_cast<const uint8_t*>(buffer.addr_);
      in_transfers_.emplace_back(bytes, bytes + size);
      if (UseDoubleBuffer() && size != 0U)
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
    return ErrorCode::OK;
  }

  void SetMaxTransferSize(size_t size) { max_transfer_size_ = size; }

  void SetDoubleBufferSupported(bool supported) { double_buffer_supported_ = supported; }

  void SetBulkPacketSize(uint16_t size) { bulk_packet_size_ = size; }

  void FailNextInTransfers(size_t count, bool enter_error = false)
  {
    fail_next_in_transfers_ = count;
    enter_error_on_failure_ = enter_error;
  }

  void FailNextTransfers(size_t count, bool enter_error = false)
  {
    fail_next_transfers_ = count;
    enter_error_on_failure_ = enter_error;
  }

  void SetFailAllInTransfers(bool enabled, bool enter_error = false)
  {
    fail_all_in_transfers_ = enabled;
    enter_error_on_failure_ = enter_error;
  }

  void SetCompleteInBeforeTransferReturns(bool enabled)
  {
    complete_in_before_return_ = enabled;
  }

  void LatchInCompletion()
  {
    ASSERT(GetDirection() == Direction::IN);
    ASSERT(GetState() == State::BUSY);
    completion_latched_ = true;
    latched_transfer_size_ = last_transfer_size_;
  }

  void InjectStaleInCompletion()
  {
    ASSERT(GetDirection() == Direction::IN);
    ASSERT(GetState() == State::BUSY);
    stale_in_completion_latched_ = true;
    stale_in_transfer_size_ = last_transfer_size_;
    stale_in_binding_epoch_ = binding_epoch_;
  }

  template <size_t Size>
  void InjectStaleOutCompletion(const std::array<uint8_t, Size>& payload)
  {
    ASSERT(GetDirection() == Direction::OUT);
    ASSERT(GetState() == State::BUSY);
    ASSERT(payload.size() <= GetBuffer().size_);
    stale_out_payload_.assign(payload.begin(), payload.end());
    stale_out_completion_latched_ = true;
    stale_out_binding_epoch_ = binding_epoch_;
  }

  bool DispatchLatchedInCompletion(bool in_isr)
  {
    ASSERT(GetDirection() == Direction::IN);
    if (!completion_latched_)
    {
      return false;
    }

    completion_latched_ = false;
    OnTransferCompleteCallback(in_isr, latched_transfer_size_);
    return true;
  }

  bool DispatchStaleInCompletion(bool in_isr)
  {
    ASSERT(GetDirection() == Direction::IN);
    if (!stale_in_completion_latched_)
    {
      return false;
    }

    ++stale_in_dispatch_attempts_;
    stale_in_completion_latched_ = false;
    if (stale_in_binding_epoch_ != binding_epoch_)
    {
      return false;
    }

    OnTransferCompleteCallback(in_isr, stale_in_transfer_size_);
    return true;
  }

  bool DispatchStaleOutCompletion(bool in_isr)
  {
    ASSERT(GetDirection() == Direction::OUT);
    if (!stale_out_completion_latched_)
    {
      return false;
    }

    ++stale_out_dispatch_attempts_;
    stale_out_completion_latched_ = false;
    if (stale_out_binding_epoch_ != binding_epoch_)
    {
      stale_out_payload_.clear();
      return false;
    }

    RawData buffer = GetBuffer();
    ASSERT(stale_out_payload_.size() <= buffer.size_);
    std::copy(stale_out_payload_.begin(), stale_out_payload_.end(),
              static_cast<uint8_t*>(buffer.addr_));
    const size_t size = stale_out_payload_.size();
    stale_out_payload_.clear();
    OnTransferCompleteCallback(in_isr, size);
    return true;
  }

  void CompleteInTransfer(bool in_isr)
  {
    LatchInCompletion();
    ASSERT(DispatchLatchedInCompletion(in_isr));
  }

  template <size_t Size>
  void CompleteOutTransfer(const std::array<uint8_t, Size>& payload, bool in_isr)
  {
    ASSERT(GetDirection() == Direction::OUT);
    ASSERT(GetState() == State::BUSY);
    RawData buffer = GetBuffer();
    ASSERT(payload.size() <= buffer.size_);
    std::copy(payload.begin(), payload.end(), static_cast<uint8_t*>(buffer.addr_));
    OnTransferCompleteCallback(in_isr, payload.size());
  }

  [[nodiscard]] bool HasLatchedCompletion() const { return completion_latched_; }

  [[nodiscard]] bool HasStaleInCompletion() const { return stale_in_completion_latched_; }

  [[nodiscard]] bool HasStaleOutCompletion() const
  {
    return stale_out_completion_latched_;
  }

  [[nodiscard]] uint32_t BindingEpoch() const { return binding_epoch_; }

  [[nodiscard]] size_t StaleInDispatchAttempts() const
  {
    return stale_in_dispatch_attempts_;
  }

  [[nodiscard]] size_t StaleOutDispatchAttempts() const
  {
    return stale_out_dispatch_attempts_;
  }

  [[nodiscard]] const std::vector<std::vector<uint8_t>>& InTransfers() const
  {
    return in_transfers_;
  }

  [[nodiscard]] size_t FailedInAttempts() const { return failed_in_attempts_; }

  [[nodiscard]] size_t MaxTransferDepth() const { return max_transfer_depth_; }

  [[nodiscard]] size_t TransferAttempts() const { return transfer_attempts_; }

  [[nodiscard]] std::vector<uint8_t> CurrentBufferPrefix(size_t size) const
  {
    const RawData buffer = GetBuffer();
    ASSERT(size <= buffer.size_);
    const auto* bytes = static_cast<const uint8_t*>(buffer.addr_);
    return {bytes, bytes + size};
  }

 private:
  size_t max_transfer_size_ = 0U;
  size_t last_transfer_size_ = 0U;
  size_t latched_transfer_size_ = 0U;
  size_t stale_in_transfer_size_ = 0U;
  size_t stale_in_dispatch_attempts_ = 0U;
  size_t stale_out_dispatch_attempts_ = 0U;
  size_t transfer_depth_ = 0U;
  size_t max_transfer_depth_ = 0U;
  size_t transfer_attempts_ = 0U;
  size_t failed_in_attempts_ = 0U;
  size_t fail_next_in_transfers_ = 0U;
  size_t fail_next_transfers_ = 0U;
  uint16_t bulk_packet_size_ = BULK_PACKET_SIZE;
  bool complete_in_before_return_ = false;
  bool double_buffer_supported_ = true;
  bool enter_error_on_failure_ = false;
  bool fail_all_in_transfers_ = false;
  bool completion_latched_ = false;
  bool stale_in_completion_latched_ = false;
  bool stale_out_completion_latched_ = false;
  uint32_t binding_epoch_ = 0U;
  uint32_t stale_in_binding_epoch_ = 0U;
  uint32_t stale_out_binding_epoch_ = 0U;
  std::vector<uint8_t> stale_out_payload_;
  std::vector<std::vector<uint8_t>> in_transfers_;
};

class TestCDCUart final : public LibXR::USB::CDCUart
{
 public:
  TestCDCUart(Endpoint::EPNumber data_in_ep = Endpoint::EPNumber::EP1,
              Endpoint::EPNumber data_out_ep = Endpoint::EPNumber::EP2,
              Endpoint::EPNumber comm_in_ep = Endpoint::EPNumber::EP3,
              size_t rx_buffer_size = 16U)
      : CDCUart(data_in_ep, data_out_ep, comm_in_ep, rx_buffer_size, 32U, 8U)
  {
  }

  [[nodiscard]] size_t QueuedBytes() { return write_port_->Size(); }

  [[nodiscard]] size_t QueuedRxBytes() { return read_port_->Size(); }

  [[nodiscard]] bool ConfiguredForTest() const { return DeviceConfigured(); }

  [[nodiscard]] bool FatalForTest() const { return DeviceGenerationFatal(); }

  [[nodiscard]] uint32_t GenerationForTest() const { return DeviceGeneration(); }
};

class TestCDCBase final : public CDCBase
{
 public:
  TestCDCBase(Endpoint::EPNumber data_in_ep = Endpoint::EPNumber::EP1,
              Endpoint::EPNumber data_out_ep = Endpoint::EPNumber::EP2,
              Endpoint::EPNumber comm_in_ep = Endpoint::EPNumber::EP3)
      : CDCBase(data_in_ep, data_out_ep, comm_in_ep)
  {
  }

  [[nodiscard]] bool ConfiguredForTest() const { return DeviceConfigured(); }

  [[nodiscard]] bool FatalForTest() const { return DeviceGenerationFatal(); }

  [[nodiscard]] uint32_t GenerationForTest() const { return DeviceGeneration(); }

  void MarkFatalForTest() { MarkDeviceGenerationFatal(); }

  [[nodiscard]] size_t OutCompletionCount() const { return out_completion_count_; }

  [[nodiscard]] size_t InCompletionCount() const { return in_completion_count_; }

 protected:
  void OnDataOutComplete(bool in_isr, ConstRawData& data) override
  {
    UNUSED(in_isr);
    UNUSED(data);
    ++out_completion_count_;
  }

  void OnDataInComplete(bool in_isr, ConstRawData& data) override
  {
    UNUSED(in_isr);
    UNUSED(data);
    ++in_completion_count_;
  }

 private:
  size_t out_completion_count_ = 0U;
  size_t in_completion_count_ = 0U;
};

class SyncArmCDCBase final : public CDCBase
{
 public:
  SyncArmCDCBase(Endpoint::EPNumber data_in_ep = Endpoint::EPNumber::EP1,
                 Endpoint::EPNumber data_out_ep = Endpoint::EPNumber::EP2,
                 Endpoint::EPNumber comm_in_ep = Endpoint::EPNumber::EP3)
      : CDCBase(data_in_ep, data_out_ep, comm_in_ep)
  {
  }

  [[nodiscard]] size_t OutCompletionCount() const { return out_completion_count_; }

  [[nodiscard]] size_t InCompletionCount() const { return in_completion_count_; }

 protected:
  void BindEndpoints(EndpointPool& endpoint_pool, uint8_t start_itf_num,
                     bool in_isr) override
  {
    CDCBase::BindEndpoints(endpoint_pool, start_itf_num, in_isr);
    if (DeviceConfigured())
    {
      auto* endpoint = GetDataOutEndpoint();
      ASSERT(endpoint != nullptr);
      ASSERT(endpoint->Transfer(endpoint->MaxPacketSize()) == ErrorCode::OK);
    }
  }

  void OnDataOutComplete(bool in_isr, ConstRawData& data) override
  {
    UNUSED(in_isr);
    UNUSED(data);
    ++out_completion_count_;
  }

  void OnDataInComplete(bool in_isr, ConstRawData& data) override
  {
    UNUSED(in_isr);
    UNUSED(data);
    ++in_completion_count_;
  }

 private:
  size_t out_completion_count_ = 0U;
  size_t in_completion_count_ = 0U;
};

class FakeUSBDevice final : public EndpointPool, public DeviceCore
{
 private:
  static constexpr size_t HIGH_SPEED_BULK_PACKET_SIZE = 512U;

  // DoubleBufferStorage requires a raw size divisible by 2 * alignof(size_t).
  alignas(size_t) std::array<uint8_t, 64U> ep0_in_storage_{};
  alignas(size_t) std::array<uint8_t, 64U> ep0_out_storage_{};
  alignas(size_t) std::array<uint8_t, 32U> data_in_storage_{};
  alignas(
      size_t) std::array<uint8_t, HIGH_SPEED_BULK_PACKET_SIZE * 2U> data_out_storage_{};
  alignas(size_t) std::array<uint8_t, 32U> comm_in_storage_{};
  alignas(size_t) std::array<uint8_t, 32U> data_in_2_storage_{};
  alignas(size_t) std::array<uint8_t, 32U> data_out_2_storage_{};
  alignas(size_t) std::array<uint8_t, 32U> comm_in_2_storage_{};

 public:
  FakeUSBDevice(CDCBase& cdc, Speed speed = Speed::FULL)
      : EndpointPool(7U),
        DeviceCore(*this, LibXR::USB::USBSpec::USB_2_1, speed,
                   LibXR::USB::DeviceDescriptor::PacketSize0::SIZE_64, 0x1209U, 0x0001U,
                   0x0100U, {&TEST_LANGUAGE}, {{&cdc}}),
        ep0_in(Endpoint::EPNumber::EP0, Endpoint::Direction::IN,
               RawData{ep0_in_storage_.data(), ep0_in_storage_.size()}),
        ep0_out(Endpoint::EPNumber::EP0, Endpoint::Direction::OUT,
                RawData{ep0_out_storage_.data(), ep0_out_storage_.size()}),
        data_in(Endpoint::EPNumber::EP1, Endpoint::Direction::IN,
                RawData{data_in_storage_.data(), data_in_storage_.size()}),
        data_out(Endpoint::EPNumber::EP2, Endpoint::Direction::OUT,
                 RawData{data_out_storage_.data(), data_out_storage_.size()}),
        comm_in(Endpoint::EPNumber::EP3, Endpoint::Direction::IN,
                RawData{comm_in_storage_.data(), comm_in_storage_.size()}),
        data_in_2(Endpoint::EPNumber::EP4, Endpoint::Direction::IN,
                  RawData{data_in_2_storage_.data(), data_in_2_storage_.size()}),
        data_out_2(Endpoint::EPNumber::EP5, Endpoint::Direction::OUT,
                   RawData{data_out_2_storage_.data(), data_out_2_storage_.size()}),
        comm_in_2(Endpoint::EPNumber::EP6, Endpoint::Direction::IN,
                  RawData{comm_in_2_storage_.data(), comm_in_2_storage_.size()})
  {
    RegisterEndpoints();
  }

  FakeUSBDevice(CDCBase& first, CDCBase& second)
      : EndpointPool(7U),
        DeviceCore(*this, LibXR::USB::USBSpec::USB_2_1, LibXR::USB::Speed::FULL,
                   LibXR::USB::DeviceDescriptor::PacketSize0::SIZE_64, 0x1209U, 0x0002U,
                   0x0100U, {&TEST_LANGUAGE}, {{&first, &second}}),
        ep0_in(Endpoint::EPNumber::EP0, Endpoint::Direction::IN,
               RawData{ep0_in_storage_.data(), ep0_in_storage_.size()}),
        ep0_out(Endpoint::EPNumber::EP0, Endpoint::Direction::OUT,
                RawData{ep0_out_storage_.data(), ep0_out_storage_.size()}),
        data_in(Endpoint::EPNumber::EP1, Endpoint::Direction::IN,
                RawData{data_in_storage_.data(), data_in_storage_.size()}),
        data_out(Endpoint::EPNumber::EP2, Endpoint::Direction::OUT,
                 RawData{data_out_storage_.data(), data_out_storage_.size()}),
        comm_in(Endpoint::EPNumber::EP3, Endpoint::Direction::IN,
                RawData{comm_in_storage_.data(), comm_in_storage_.size()}),
        data_in_2(Endpoint::EPNumber::EP4, Endpoint::Direction::IN,
                  RawData{data_in_2_storage_.data(), data_in_2_storage_.size()}),
        data_out_2(Endpoint::EPNumber::EP5, Endpoint::Direction::OUT,
                   RawData{data_out_2_storage_.data(), data_out_2_storage_.size()}),
        comm_in_2(Endpoint::EPNumber::EP6, Endpoint::Direction::IN,
                  RawData{comm_in_2_storage_.data(), comm_in_2_storage_.size()})
  {
    RegisterEndpoints();
  }

  template <typename Hook>
  void Initialize(Hook&& after_bind)
  {
    InitializeUnconfigured();
    SetConfiguration(1U, std::forward<Hook>(after_bind));
  }

  void Initialize()
  {
    Initialize([] {});
  }

  void Shutdown()
  {
    ASSERT(IsInited());
    ASSERT(RunIrq([this] { DeviceCore::Deinit(true); }));
  }

  void InitializeUnconfigured()
  {
    ASSERT(!IsInited());
    ASSERT(RunIrq([this] { DeviceCore::Init(true); }));
  }

  void SetConfigurationInOwner(uint16_t value)
  {
    const SetupPacket setup{static_cast<uint8_t>(RequestDirection::OUT) |
                                static_cast<uint8_t>(RequestType::STANDARD) |
                                static_cast<uint8_t>(Recipient::DEVICE),
                            static_cast<uint8_t>(StandardRequest::SET_CONFIGURATION),
                            value, 0U, 0U};
    OnSetupPacket(true, &setup);
  }

  template <typename Hook>
  void SetConfiguration(uint16_t value, Hook&& after_switch)
  {
    ASSERT(RunIrq(
        [this, value, &after_switch]
        {
          SetConfigurationInOwner(value);
          after_switch();
        }));
    CompleteControlStatus();
  }

  void CompleteControlStatus()
  {
    ASSERT(ep0_in.GetState() == Endpoint::State::BUSY);
    CompleteIn(ep0_in);
  }

  void SetConfiguration(uint16_t value)
  {
    SetConfiguration(value, [] {});
  }

  void SetEndpointHalt(FakeEndpoint& endpoint)
  {
    const SetupPacket setup{static_cast<uint8_t>(RequestDirection::OUT) |
                                static_cast<uint8_t>(RequestType::STANDARD) |
                                static_cast<uint8_t>(Recipient::ENDPOINT),
                            static_cast<uint8_t>(StandardRequest::SET_FEATURE), 0U,
                            endpoint.GetAddress(), 0U};
    ASSERT(RunIrq([this, &setup] { OnSetupPacket(true, &setup); }));
    CompleteControlStatus();
  }

  void ClearEndpointHaltInOwner(FakeEndpoint& endpoint)
  {
    const SetupPacket setup{static_cast<uint8_t>(RequestDirection::OUT) |
                                static_cast<uint8_t>(RequestType::STANDARD) |
                                static_cast<uint8_t>(Recipient::ENDPOINT),
                            static_cast<uint8_t>(StandardRequest::CLEAR_FEATURE), 0U,
                            endpoint.GetAddress(), 0U};
    OnSetupPacket(true, &setup);
  }

  void ClearEndpointHalt(FakeEndpoint& endpoint)
  {
    ASSERT(RunIrq([this, &endpoint] { ClearEndpointHaltInOwner(endpoint); }));
    CompleteControlStatus();
  }

  template <typename SetupAction>
  void DispatchSnapshot(uint32_t out_bits, uint32_t in_bits, SetupAction&& setup_action)
  {
    ASSERT(RunIrq(
        [this, out_bits, in_bits, &setup_action]
        {
          // Capture both directions before any endpoint callback can rebind the device.
          const uint32_t captured_out_bits = out_bits;
          const uint32_t captured_in_bits = in_bits;
          const auto endpoint_epochs = BindingEpochs();
          bool endpoint_barrier = false;

          for (uint8_t ep_num = 0U; ep_num < 7U; ++ep_num)
          {
            if (BindingEpochs() != endpoint_epochs)
            {
              endpoint_barrier = true;
              break;
            }

            if ((captured_out_bits & (1UL << ep_num)) == 0U)
            {
              continue;
            }

            if (ep_num == 0U)
            {
              setup_action();
              continue;
            }

            auto* endpoint = EndpointFor(ep_num, Endpoint::Direction::OUT);
            if (endpoint != nullptr)
            {
              (void)endpoint->DispatchStaleOutCompletion(true);
            }
          }

          // A SET_CONFIGURATION callback can be the final captured OUT bit, so check the
          // rebind boundary once more before entering the captured IN direction.
          endpoint_barrier = endpoint_barrier || BindingEpochs() != endpoint_epochs;
          if (!endpoint_barrier)
          {
            const auto in_epochs = BindingEpochs();
            for (uint8_t ep_num = 0U; ep_num < 7U; ++ep_num)
            {
              if (BindingEpochs() != in_epochs)
              {
                break;
              }

              if ((captured_in_bits & (1UL << ep_num)) == 0U)
              {
                continue;
              }

              auto* endpoint = EndpointFor(ep_num, Endpoint::Direction::IN);
              if (endpoint != nullptr)
              {
                (void)endpoint->DispatchStaleInCompletion(true);
              }
            }
          }
        }));
  }

  void CompleteIn(FakeEndpoint& endpoint)
  {
    ASSERT(RunIrq([&endpoint] { endpoint.CompleteInTransfer(true); }));
  }

  template <size_t Size>
  void CompleteOut(FakeEndpoint& endpoint, const std::array<uint8_t, Size>& payload)
  {
    ASSERT(
        RunIrq([&endpoint, &payload] { endpoint.CompleteOutTransfer(payload, true); }));
  }

  void Start(bool) override {}

  void Stop(bool) override {}

  FakeEndpoint ep0_in;
  FakeEndpoint ep0_out;
  FakeEndpoint data_in;
  FakeEndpoint data_out;
  FakeEndpoint comm_in;
  FakeEndpoint data_in_2;
  FakeEndpoint data_out_2;
  FakeEndpoint comm_in_2;

 protected:
  ErrorCode SetAddress(uint8_t, DeviceCore::Context) override { return ErrorCode::OK; }

 private:
  [[nodiscard]] std::array<uint32_t, 8U> BindingEpochs() const
  {
    return {ep0_in.BindingEpoch(),     ep0_out.BindingEpoch(),  data_in.BindingEpoch(),
            data_out.BindingEpoch(),   comm_in.BindingEpoch(),  data_in_2.BindingEpoch(),
            data_out_2.BindingEpoch(), comm_in_2.BindingEpoch()};
  }

  FakeEndpoint* EndpointFor(uint8_t ep_num, Endpoint::Direction direction)
  {
    switch (ep_num)
    {
      case 0U:
        return direction == Endpoint::Direction::IN ? &ep0_in : &ep0_out;
      case 1U:
        return direction == Endpoint::Direction::IN ? &data_in : nullptr;
      case 2U:
        return direction == Endpoint::Direction::OUT ? &data_out : nullptr;
      case 3U:
        return direction == Endpoint::Direction::IN ? &comm_in : nullptr;
      case 4U:
        return direction == Endpoint::Direction::IN ? &data_in_2 : nullptr;
      case 5U:
        return direction == Endpoint::Direction::OUT ? &data_out_2 : nullptr;
      case 6U:
        return direction == Endpoint::Direction::IN ? &comm_in_2 : nullptr;
      default:
        return nullptr;
    }
  }

  void RegisterEndpoints()
  {
    SetEndpoint0(&ep0_in, &ep0_out);
    ASSERT(Put(&data_in) == ErrorCode::OK);
    ASSERT(Put(&data_out) == ErrorCode::OK);
    ASSERT(Put(&comm_in) == ErrorCode::OK);
    ASSERT(Put(&data_in_2) == ErrorCode::OK);
    ASSERT(Put(&data_out_2) == ErrorCode::OK);
    ASSERT(Put(&comm_in_2) == ErrorCode::OK);
  }
};

struct SingleCdcFixture
{
  explicit SingleCdcFixture(size_t rx_buffer_size = 16U, Speed speed = Speed::FULL)
      : cdc(Endpoint::EPNumber::EP1, Endpoint::EPNumber::EP2, Endpoint::EPNumber::EP3,
            rx_buffer_size),
        device(cdc, speed)
  {
  }

  ~SingleCdcFixture()
  {
    if (device.IsInited())
    {
      device.Shutdown();
    }
  }

  TestCDCUart cdc;
  FakeUSBDevice device;
};

struct SyncArmFixture
{
  SyncArmFixture() : cdc(), device(cdc) {}

  ~SyncArmFixture()
  {
    if (device.IsInited())
    {
      device.Shutdown();
    }
  }

  SyncArmCDCBase cdc;
  FakeUSBDevice device;
};

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

struct CompletionProbe
{
  CompletionProbe()
      : callback(LibXR::Callback<ErrorCode>::Create(RecordCompletion, &state)),
        operation(callback)
  {
  }

  CompletionState state;
  LibXR::Callback<ErrorCode> callback;
  LibXR::WriteOperation operation;
};

template <size_t Size>
void Submit(TestCDCUart& cdc, const std::array<uint8_t, Size>& payload,
            CompletionProbe& probe, bool in_isr = false)
{
  ASSERT(cdc.Write(ConstRawData{payload.data(), payload.size()}, probe.operation,
                   in_isr) == ErrorCode::OK);
}

void ExpectCompletedOk(const CompletionProbe& probe)
{
  ASSERT(probe.state.count == 1U);
  ASSERT(probe.state.result == ErrorCode::OK);
}

void BaseLifecycleGateDropsStaleWork()
{
  TestCDCBase cdc;
  FakeUSBDevice device(cdc);
  device.InitializeUnconfigured();

  ASSERT(!cdc.ConfiguredForTest());
  ASSERT(device.data_out.TransferAttempts() == 0U);
  ASSERT(cdc.SendSerialState() == ErrorCode::OK);
  ASSERT(device.comm_in.TransferAttempts() == 0U);

  device.SetConfiguration(1U);
  ASSERT(cdc.ConfiguredForTest());
  ASSERT(device.data_out.TransferAttempts() == 1U);
  ASSERT(device.comm_in.TransferAttempts() == 0U);

  ASSERT(cdc.SendSerialState() == ErrorCode::OK);
  ASSERT(device.comm_in.TransferAttempts() == 1U);
  device.CompleteIn(device.comm_in);
}

void BaseSerialStateBusyUsesCompletionCarrier()
{
  TestCDCBase cdc;
  FakeUSBDevice device(cdc);
  device.Initialize();

  ASSERT(cdc.SendSerialState() == ErrorCode::OK);
  ASSERT(device.comm_in.TransferAttempts() == 1U);
  ASSERT(cdc.SendSerialState() == ErrorCode::OK);
  ASSERT(device.comm_in.TransferAttempts() == 1U);

  device.CompleteIn(device.comm_in);
  ASSERT(device.comm_in.TransferAttempts() == 2U);
  device.CompleteIn(device.comm_in);
}

void BaseFatalGateSuppressesCallbacksAndTransfers()
{
  TestCDCBase cdc;
  FakeUSBDevice device(cdc);
  device.Initialize();

  const std::array<uint8_t, 1U> out_payload{0xE1U};
  ASSERT(device.data_in.Transfer(1U) == ErrorCode::OK);
  ASSERT(cdc.SendSerialState() == ErrorCode::OK);
  ASSERT(cdc.SendSerialState() == ErrorCode::OK);
  const size_t out_attempts = device.data_out.TransferAttempts();
  const size_t comm_attempts = device.comm_in.TransferAttempts();

  cdc.MarkFatalForTest();
  device.CompleteOut(device.data_out, out_payload);
  device.CompleteIn(device.data_in);
  device.CompleteIn(device.comm_in);

  ASSERT(cdc.FatalForTest());
  ASSERT(cdc.OutCompletionCount() == 0U);
  ASSERT(cdc.InCompletionCount() == 0U);
  ASSERT(device.data_out.GetState() == Endpoint::State::IDLE);
  ASSERT(device.data_out.TransferAttempts() == out_attempts);
  ASSERT(device.comm_in.TransferAttempts() == comm_attempts);

  device.SetEndpointHalt(device.data_out);
  device.ClearEndpointHalt(device.data_out);
  ASSERT(device.data_out.TransferAttempts() == out_attempts);

  device.SetConfiguration(1U);
  ASSERT(!cdc.FatalForTest());
  ASSERT(device.data_out.TransferAttempts() == out_attempts + 1U);
  ASSERT(device.comm_in.TransferAttempts() == comm_attempts);

  ASSERT(cdc.SendSerialState() == ErrorCode::OK);
  ASSERT(device.comm_in.TransferAttempts() == comm_attempts + 1U);
  device.CompleteIn(device.comm_in);
}

void SerialStateStartFailureFailStopsGeneration()
{
  SingleCdcFixture fixture;
  fixture.device.Initialize();
  const uint32_t generation = fixture.cdc.GenerationForTest();

  fixture.device.comm_in.FailNextTransfers(1U);
  ASSERT(fixture.cdc.SendSerialState() == ErrorCode::OK);
  ASSERT(fixture.cdc.FatalForTest());
  ASSERT(fixture.cdc.GenerationForTest() == generation);
  ASSERT(fixture.device.comm_in.TransferAttempts() == 1U);

  ASSERT(fixture.cdc.SendSerialState() == ErrorCode::OK);
  ASSERT(fixture.device.comm_in.TransferAttempts() == 1U);

  fixture.device.SetConfiguration(1U);
  ASSERT(!fixture.cdc.FatalForTest());
  ASSERT(fixture.cdc.SendSerialState() == ErrorCode::OK);
  ASSERT(fixture.device.comm_in.TransferAttempts() == 2U);
  fixture.device.CompleteIn(fixture.device.comm_in);
}

void OutRearmStartFailureFailStopsGeneration()
{
  SingleCdcFixture fixture;
  fixture.device.Initialize();
  const uint32_t generation = fixture.cdc.GenerationForTest();
  const size_t attempts_before = fixture.device.data_out.TransferAttempts();

  fixture.device.data_out.FailNextTransfers(1U);
  const std::array<uint8_t, 3U> payload{0xE2U, 0xE3U, 0xE4U};
  fixture.device.CompleteOut(fixture.device.data_out, payload);

  ASSERT(fixture.cdc.FatalForTest());
  ASSERT(fixture.cdc.GenerationForTest() == generation);
  ASSERT(fixture.cdc.QueuedRxBytes() == payload.size());
  ASSERT(fixture.device.data_out.TransferAttempts() == attempts_before + 1U);

  std::array<uint8_t, 3U> received{};
  LibXR::ReadOperation operation;
  ASSERT(fixture.cdc.Read(RawData{received.data(), received.size()}, operation) ==
         ErrorCode::OK);
  ASSERT(received == payload);
  ASSERT(fixture.device.data_out.TransferAttempts() == attempts_before + 1U);

  fixture.device.SetConfiguration(1U);
  ASSERT(!fixture.cdc.FatalForTest());
  ASSERT(fixture.device.data_out.GetState() == Endpoint::State::BUSY);
}

void RequiresExplicitConfigurationBeforeAcceptance()
{
  SingleCdcFixture fixture;
  fixture.device.InitializeUnconfigured();
  const uint32_t init_generation = fixture.cdc.GenerationForTest();

  ASSERT(!fixture.cdc.ConfiguredForTest());
  ASSERT(!fixture.cdc.FatalForTest());
  ASSERT(fixture.device.data_in.GetState() == Endpoint::State::IDLE);
  ASSERT(fixture.device.data_out.GetState() == Endpoint::State::IDLE);

  const std::array<uint8_t, 3U> payload{0x11U, 0x12U, 0x13U};
  CompletionProbe completion;
  Submit(fixture.cdc, payload, completion);
  ASSERT(completion.state.count == 0U);
  ASSERT(fixture.cdc.QueuedBytes() == payload.size());
  ASSERT(fixture.device.data_in.TransferAttempts() == 0U);

  bool observed_configured_idle = false;
  fixture.device.SetConfiguration(
      1U,
      [&]
      {
        observed_configured_idle =
            fixture.cdc.ConfiguredForTest() &&
            fixture.device.data_in.GetState() == Endpoint::State::IDLE &&
            fixture.device.data_out.GetState() == Endpoint::State::IDLE;
      });

  ASSERT(observed_configured_idle);
  ASSERT(fixture.cdc.ConfiguredForTest());
  ASSERT(fixture.cdc.GenerationForTest() > init_generation);
  ExpectCompletedOk(completion);
  ASSERT(fixture.cdc.QueuedBytes() == 0U);
  ASSERT(fixture.device.data_in.InTransfers().size() == 1U);
  ASSERT(fixture.device.data_in.InTransfers()[0] ==
         std::vector<uint8_t>(payload.begin(), payload.end()));
  ASSERT(fixture.device.data_out.GetState() == Endpoint::State::BUSY);
  fixture.device.CompleteIn(fixture.device.data_in);
}

void ActiveAndReadyCompleteEarlyWithoutCrossingRequestBoundaries()
{
  SingleCdcFixture fixture;
  fixture.device.Initialize();

  const std::array<uint8_t, 2U> active{0x21U, 0x22U};
  const std::array<uint8_t, 3U> ready{0x31U, 0x32U, 0x33U};
  CompletionProbe active_completion;
  CompletionProbe ready_completion;

  Submit(fixture.cdc, active, active_completion);
  ExpectCompletedOk(active_completion);
  ASSERT(fixture.device.data_in.TransferAttempts() == 1U);
  ASSERT(fixture.device.data_in.InTransfers().size() == 1U);
  ASSERT(fixture.device.data_in.InTransfers()[0] ==
         std::vector<uint8_t>(active.begin(), active.end()));

  Submit(fixture.cdc, ready, ready_completion);
  ExpectCompletedOk(ready_completion);
  ASSERT(fixture.device.data_in.TransferAttempts() == 1U);
  ASSERT(fixture.device.data_in.InTransfers().size() == 1U);
  ASSERT(fixture.device.data_in.CurrentBufferPrefix(ready.size()) ==
         std::vector<uint8_t>(ready.begin(), ready.end()));

  fixture.device.CompleteIn(fixture.device.data_in);
  ASSERT(fixture.device.data_in.TransferAttempts() == 2U);
  ASSERT(fixture.device.data_in.InTransfers().size() == 2U);
  ASSERT(fixture.device.data_in.InTransfers()[1] ==
         std::vector<uint8_t>(ready.begin(), ready.end()));
  fixture.device.CompleteIn(fixture.device.data_in);
}

void SingleBufferAcceptsOnlyActiveUntilCompletion()
{
  SingleCdcFixture fixture;
  fixture.device.data_in.SetDoubleBufferSupported(false);
  fixture.device.Initialize();
  ASSERT(!fixture.device.data_in.UseDoubleBuffer());

  const std::array<uint8_t, 2U> active{0x34U, 0x35U};
  const std::array<uint8_t, 3U> queued{0x36U, 0x37U, 0x38U};
  CompletionProbe active_completion;
  CompletionProbe queued_completion;

  Submit(fixture.cdc, active, active_completion);
  ExpectCompletedOk(active_completion);
  Submit(fixture.cdc, queued, queued_completion);

  ASSERT(queued_completion.state.count == 0U);
  ASSERT(fixture.cdc.QueuedBytes() == queued.size());
  ASSERT(fixture.device.data_in.TransferAttempts() == 1U);
  ASSERT(fixture.device.data_in.CurrentBufferPrefix(active.size()) ==
         std::vector<uint8_t>(active.begin(), active.end()));

  fixture.device.CompleteIn(fixture.device.data_in);
  ExpectCompletedOk(queued_completion);
  ASSERT(fixture.cdc.QueuedBytes() == 0U);
  ASSERT(fixture.device.data_in.TransferAttempts() == 2U);
  ASSERT(fixture.device.data_in.InTransfers()[1] ==
         std::vector<uint8_t>(queued.begin(), queued.end()));
  fixture.device.CompleteIn(fixture.device.data_in);
}

void ClearDataOutHaltRearmsAndReceives()
{
  SingleCdcFixture fixture;
  fixture.device.Initialize();
  ASSERT(fixture.device.data_out.GetState() == Endpoint::State::BUSY);

  fixture.device.SetEndpointHalt(fixture.device.data_out);
  ASSERT(fixture.device.data_out.GetState() == Endpoint::State::STALLED);

  fixture.device.ClearEndpointHalt(fixture.device.data_out);
  ASSERT(fixture.device.data_out.GetState() == Endpoint::State::BUSY);

  const std::array<uint8_t, 3U> payload{0x39U, 0x3AU, 0x3BU};
  fixture.device.CompleteOut(fixture.device.data_out, payload);

  std::array<uint8_t, 3U> received{};
  LibXR::ReadOperation operation;
  ASSERT(fixture.cdc.Read(RawData{received.data(), received.size()}, operation) ==
         ErrorCode::OK);
  ASSERT(received == payload);
  ASSERT(fixture.device.data_out.GetState() == Endpoint::State::BUSY);
}

void ActiveStartFailureFailStopsAcceptedOperation()
{
  SingleCdcFixture fixture;
  fixture.device.Initialize();
  const uint32_t generation = fixture.cdc.GenerationForTest();

  fixture.device.data_in.FailNextInTransfers(1U);
  const std::array<uint8_t, 3U> accepted{0x41U, 0x42U, 0x43U};
  CompletionProbe accepted_completion;
  Submit(fixture.cdc, accepted, accepted_completion);

  ExpectCompletedOk(accepted_completion);
  ASSERT(fixture.cdc.FatalForTest());
  ASSERT(fixture.cdc.GenerationForTest() == generation);
  ASSERT(fixture.cdc.QueuedBytes() == 0U);
  ASSERT(fixture.device.data_in.FailedInAttempts() == 1U);
  ASSERT(fixture.device.data_in.InTransfers().empty());

  const std::array<uint8_t, 2U> suffix{0x44U, 0x45U};
  CompletionProbe suffix_completion;
  Submit(fixture.cdc, suffix, suffix_completion);
  ASSERT(suffix_completion.state.count == 0U);
  ASSERT(fixture.cdc.QueuedBytes() == suffix.size());
  ASSERT(fixture.cdc.SendSerialState() == ErrorCode::OK);
  ASSERT(fixture.device.data_in.TransferAttempts() == 1U);
  ASSERT(fixture.cdc.QueuedBytes() == suffix.size());
}

void ReadyStartFailureKeepsOnlyUnacceptedSuffixForReconfiguration()
{
  SingleCdcFixture fixture;
  fixture.device.Initialize();
  const uint32_t generation = fixture.cdc.GenerationForTest();

  const std::array<uint8_t, 2U> active{0x51U, 0x52U};
  const std::array<uint8_t, 3U> ready{0x53U, 0x54U, 0x55U};
  const std::array<uint8_t, 2U> suffix{0x56U, 0x57U};
  CompletionProbe active_completion;
  CompletionProbe ready_completion;
  CompletionProbe suffix_completion;
  Submit(fixture.cdc, active, active_completion);
  Submit(fixture.cdc, ready, ready_completion);
  Submit(fixture.cdc, suffix, suffix_completion);

  ExpectCompletedOk(active_completion);
  ExpectCompletedOk(ready_completion);
  ASSERT(suffix_completion.state.count == 0U);
  ASSERT(fixture.cdc.QueuedBytes() == suffix.size());

  fixture.device.data_in.FailNextInTransfers(1U);
  fixture.device.CompleteIn(fixture.device.data_in);

  ASSERT(fixture.cdc.FatalForTest());
  ASSERT(fixture.cdc.GenerationForTest() == generation);
  ASSERT(fixture.device.data_in.FailedInAttempts() == 1U);
  ASSERT(fixture.device.data_in.InTransfers().size() == 1U);
  ASSERT(fixture.cdc.QueuedBytes() == suffix.size());
  ASSERT(suffix_completion.state.count == 0U);
  ASSERT(fixture.cdc.SendSerialState() == ErrorCode::OK);
  ASSERT(fixture.device.data_in.TransferAttempts() == 2U);

  fixture.device.SetConfiguration(1U);
  ASSERT(!fixture.cdc.FatalForTest());
  ASSERT(fixture.cdc.GenerationForTest() > generation);
  ExpectCompletedOk(suffix_completion);
  ASSERT(fixture.cdc.QueuedBytes() == 0U);
  ASSERT(fixture.device.data_in.InTransfers().size() == 2U);
  ASSERT(fixture.device.data_in.InTransfers()[1] ==
         std::vector<uint8_t>(suffix.begin(), suffix.end()));
  fixture.device.CompleteIn(fixture.device.data_in);
}

void UnbindRebindDropsAcceptedSlotsAndKeepsSuffix()
{
  SingleCdcFixture fixture;
  fixture.device.Initialize();

  const std::array<uint8_t, 2U> active{0x61U, 0x62U};
  const std::array<uint8_t, 3U> ready{0x63U, 0x64U, 0x65U};
  const std::array<uint8_t, 1U> suffix{0x66U};
  CompletionProbe active_completion;
  CompletionProbe ready_completion;
  CompletionProbe suffix_completion;
  Submit(fixture.cdc, active, active_completion);
  Submit(fixture.cdc, ready, ready_completion);
  Submit(fixture.cdc, suffix, suffix_completion);

  ExpectCompletedOk(active_completion);
  ExpectCompletedOk(ready_completion);
  ASSERT(suffix_completion.state.count == 0U);
  ASSERT(fixture.cdc.QueuedBytes() == suffix.size());
  ASSERT(fixture.device.data_in.InTransfers().size() == 1U);

  fixture.device.SetConfiguration(0U);
  ASSERT(!fixture.cdc.ConfiguredForTest());
  ASSERT(fixture.device.data_in.GetState() == Endpoint::State::DISABLED);
  ASSERT(fixture.cdc.QueuedBytes() == suffix.size());
  ASSERT(suffix_completion.state.count == 0U);

  fixture.device.SetConfiguration(1U);
  ExpectCompletedOk(suffix_completion);
  ASSERT(fixture.cdc.QueuedBytes() == 0U);
  ASSERT(fixture.device.data_in.InTransfers().size() == 2U);
  ASSERT(fixture.device.data_in.InTransfers()[1] ==
         std::vector<uint8_t>(suffix.begin(), suffix.end()));
  fixture.device.CompleteIn(fixture.device.data_in);
}

void ZlpStartFailureFailStopsWithoutRetry()
{
  SingleCdcFixture fixture;
  fixture.device.Initialize();

  const std::array<uint8_t, FakeEndpoint::BULK_PACKET_SIZE> payload{0x71U, 0x72U, 0x73U,
                                                                    0x74U};
  CompletionProbe completion;
  Submit(fixture.cdc, payload, completion);
  ExpectCompletedOk(completion);
  fixture.device.data_in.FailNextInTransfers(1U);
  fixture.device.CompleteIn(fixture.device.data_in);

  ASSERT(fixture.cdc.FatalForTest());
  ASSERT(fixture.device.data_in.FailedInAttempts() == 1U);
  ASSERT(fixture.device.data_in.TransferAttempts() == 2U);
  ASSERT(fixture.device.data_in.InTransfers().size() == 1U);
  ASSERT(fixture.cdc.SendSerialState() == ErrorCode::OK);
  ASSERT(fixture.device.data_in.TransferAttempts() == 2U);

  const std::array<uint8_t, 1U> suffix{0x75U};
  CompletionProbe suffix_completion;
  Submit(fixture.cdc, suffix, suffix_completion);
  ASSERT(suffix_completion.state.count == 0U);
  ASSERT(fixture.cdc.QueuedBytes() == suffix.size());
}

void ExactPacketAtStableIdleSendsZlp()
{
  SingleCdcFixture fixture;
  fixture.device.Initialize();

  const std::array<uint8_t, FakeEndpoint::BULK_PACKET_SIZE> payload{0x76U, 0x77U, 0x78U,
                                                                    0x79U};
  CompletionProbe completion;
  Submit(fixture.cdc, payload, completion);
  ExpectCompletedOk(completion);

  fixture.device.CompleteIn(fixture.device.data_in);
  ASSERT(fixture.device.data_in.InTransfers().size() == 2U);
  ASSERT(fixture.device.data_in.InTransfers()[1].empty());
  ASSERT(fixture.device.data_in.GetState() == Endpoint::State::BUSY);

  fixture.device.CompleteIn(fixture.device.data_in);
  ASSERT(fixture.device.data_in.InTransfers().size() == 2U);
  ASSERT(fixture.device.data_in.GetState() == Endpoint::State::IDLE);
}

void ReadyDataSuppressesIntermediateZlp()
{
  SingleCdcFixture fixture;
  fixture.device.Initialize();

  const std::array<uint8_t, FakeEndpoint::BULK_PACKET_SIZE> active{0x7AU, 0x7BU, 0x7CU,
                                                                   0x7DU};
  const std::array<uint8_t, 2U> ready{0x7EU, 0x7FU};
  CompletionProbe active_completion;
  CompletionProbe ready_completion;
  Submit(fixture.cdc, active, active_completion);
  Submit(fixture.cdc, ready, ready_completion);
  ExpectCompletedOk(active_completion);
  ExpectCompletedOk(ready_completion);

  fixture.device.CompleteIn(fixture.device.data_in);
  ASSERT(fixture.device.data_in.InTransfers().size() == 2U);
  ASSERT(fixture.device.data_in.InTransfers()[1] ==
         std::vector<uint8_t>(ready.begin(), ready.end()));

  fixture.device.CompleteIn(fixture.device.data_in);
  ASSERT(fixture.device.data_in.InTransfers().size() == 2U);
  ASSERT(fixture.device.data_in.GetState() == Endpoint::State::IDLE);
}

void CompletionAndWriteSnapshotStartsDataBeforeZlp()
{
  SingleCdcFixture fixture;
  fixture.device.Initialize();

  const std::array<uint8_t, FakeEndpoint::BULK_PACKET_SIZE> active{0x86U, 0x87U, 0x88U,
                                                                   0x89U};
  const std::array<uint8_t, 2U> next{0x8AU, 0x8BU};
  CompletionProbe active_completion;
  CompletionProbe next_completion;
  Submit(fixture.cdc, active, active_completion);
  ExpectCompletedOk(active_completion);

  ASSERT(fixture.device.RunIrq(
      [&]
      {
        fixture.device.data_in.CompleteInTransfer(true);
        Submit(fixture.cdc, next, next_completion, true);
        ASSERT(next_completion.state.count == 0U);
        ASSERT(fixture.device.data_in.InTransfers().size() == 1U);
      }));

  ExpectCompletedOk(next_completion);
  ASSERT(fixture.device.data_in.InTransfers().size() == 2U);
  ASSERT(fixture.device.data_in.InTransfers()[1] ==
         std::vector<uint8_t>(next.begin(), next.end()));
  fixture.device.CompleteIn(fixture.device.data_in);
  ASSERT(fixture.device.data_in.InTransfers().size() == 2U);
}

void LifecycleDominatesCompletionInSameOwnerSnapshot()
{
  SingleCdcFixture fixture;
  fixture.device.Initialize();
  const uint32_t generation = fixture.cdc.GenerationForTest();

  const std::array<uint8_t, 2U> active{0x81U, 0x82U};
  const std::array<uint8_t, 2U> ready{0x83U, 0x84U};
  const std::array<uint8_t, 1U> suffix{0x85U};
  CompletionProbe active_completion;
  CompletionProbe ready_completion;
  CompletionProbe suffix_completion;
  Submit(fixture.cdc, active, active_completion);
  Submit(fixture.cdc, ready, ready_completion);
  Submit(fixture.cdc, suffix, suffix_completion);

  ASSERT(fixture.device.RunIrq(
      [&]
      {
        fixture.device.data_in.CompleteInTransfer(true);
        fixture.device.SetConfigurationInOwner(1U);
      }));
  fixture.device.CompleteControlStatus();

  ASSERT(fixture.cdc.GenerationForTest() > generation);
  ExpectCompletedOk(active_completion);
  ExpectCompletedOk(ready_completion);
  ExpectCompletedOk(suffix_completion);
  ASSERT(fixture.device.data_in.InTransfers().size() == 2U);
  ASSERT(fixture.device.data_in.InTransfers()[0] ==
         std::vector<uint8_t>(active.begin(), active.end()));
  ASSERT(fixture.device.data_in.InTransfers()[1] ==
         std::vector<uint8_t>(suffix.begin(), suffix.end()));
  fixture.device.CompleteIn(fixture.device.data_in);
}

void CloseRejectsStaleCompletionAfterRebind()
{
  SingleCdcFixture fixture;
  fixture.device.Initialize();

  const std::array<uint8_t, 2U> old_payload{0x91U, 0x92U};
  CompletionProbe old_completion;
  Submit(fixture.cdc, old_payload, old_completion);
  fixture.device.data_in.LatchInCompletion();
  ASSERT(fixture.device.data_in.HasLatchedCompletion());

  fixture.device.SetConfiguration(1U);
  ASSERT(!fixture.device.data_in.HasLatchedCompletion());

  const std::array<uint8_t, 3U> current{0x93U, 0x94U, 0x95U};
  CompletionProbe current_completion;
  Submit(fixture.cdc, current, current_completion);
  ASSERT(fixture.device.RunIrq(
      [&] { ASSERT(!fixture.device.data_in.DispatchLatchedInCompletion(true)); }));

  ExpectCompletedOk(old_completion);
  ExpectCompletedOk(current_completion);
  ASSERT(fixture.device.data_in.InTransfers().size() == 2U);
  ASSERT(fixture.device.data_in.InTransfers()[1] ==
         std::vector<uint8_t>(current.begin(), current.end()));
  fixture.device.CompleteIn(fixture.device.data_in);
}

void SameSnapshotRebindDropsStaleOutCompletion()
{
  SyncArmFixture fixture;
  fixture.device.Initialize();

  ASSERT(fixture.device.data_out.GetState() == Endpoint::State::BUSY);
  const std::array<uint8_t, 3U> stale_payload{0xA1U, 0xA2U, 0xA3U};
  fixture.device.data_out.InjectStaleOutCompletion(stale_payload);
  const size_t dispatch_attempts = fixture.device.data_out.StaleOutDispatchAttempts();
  const size_t completion_count = fixture.cdc.OutCompletionCount();
  const size_t transfer_attempts = fixture.device.data_out.TransferAttempts();

  fixture.device.DispatchSnapshot(
      (1UL << 0U) | (1UL << 2U), 0U,
      [&]
      {
        fixture.device.SetConfigurationInOwner(1U);
        ASSERT(fixture.device.data_out.GetState() == Endpoint::State::BUSY);
        ASSERT(fixture.device.data_out.TransferAttempts() == transfer_attempts + 1U);
      });

  ASSERT(fixture.device.data_out.StaleOutDispatchAttempts() == dispatch_attempts);
  ASSERT(fixture.device.data_out.HasStaleOutCompletion());
  ASSERT(fixture.cdc.OutCompletionCount() == completion_count);
  ASSERT(fixture.device.data_out.GetState() == Endpoint::State::BUSY);

  fixture.device.CompleteControlStatus();
  const std::array<uint8_t, 2U> current_payload{0xA4U, 0xA5U};
  fixture.device.CompleteOut(fixture.device.data_out, current_payload);
  ASSERT(fixture.cdc.OutCompletionCount() == completion_count + 1U);
}

void SameSnapshotRebindDropsStaleInCompletion()
{
  SyncArmFixture fixture;
  fixture.device.Initialize();

  ASSERT(fixture.device.data_in.Transfer(1U) == ErrorCode::OK);
  fixture.device.data_in.InjectStaleInCompletion();
  const size_t dispatch_attempts = fixture.device.data_in.StaleInDispatchAttempts();
  const size_t completion_count = fixture.cdc.InCompletionCount();
  const size_t transfer_attempts = fixture.device.data_in.TransferAttempts();

  fixture.device.DispatchSnapshot(
      (1UL << 0U), (1UL << 1U),
      [&]
      {
        fixture.device.SetConfigurationInOwner(1U);
        ASSERT(fixture.device.data_in.Transfer(1U) == ErrorCode::OK);
        ASSERT(fixture.device.data_in.GetState() == Endpoint::State::BUSY);
        ASSERT(fixture.device.data_in.TransferAttempts() == transfer_attempts + 1U);
      });

  ASSERT(fixture.device.data_in.StaleInDispatchAttempts() == dispatch_attempts);
  ASSERT(fixture.device.data_in.HasStaleInCompletion());
  ASSERT(fixture.cdc.InCompletionCount() == completion_count);
  ASSERT(fixture.device.data_in.GetState() == Endpoint::State::BUSY);

  fixture.device.CompleteControlStatus();
  fixture.device.CompleteIn(fixture.device.data_in);
  ASSERT(fixture.cdc.InCompletionCount() == completion_count + 1U);
}

void SameSnapshotClearHaltDropsStaleOutCompletion()
{
  SyncArmFixture fixture;
  fixture.device.Initialize();

  ASSERT(fixture.device.data_out.GetState() == Endpoint::State::BUSY);
  const std::array<uint8_t, 3U> stale_payload{0xB1U, 0xB2U, 0xB3U};
  fixture.device.data_out.InjectStaleOutCompletion(stale_payload);
  fixture.device.SetEndpointHalt(fixture.device.data_out);
  ASSERT(fixture.device.data_out.GetState() == Endpoint::State::STALLED);

  const size_t dispatch_attempts = fixture.device.data_out.StaleOutDispatchAttempts();
  const size_t completion_count = fixture.cdc.OutCompletionCount();
  const size_t transfer_attempts = fixture.device.data_out.TransferAttempts();

  fixture.device.DispatchSnapshot(
      (1UL << 0U) | (1UL << 2U), 0U,
      [&]
      {
        fixture.device.ClearEndpointHaltInOwner(fixture.device.data_out);
        ASSERT(fixture.device.data_out.GetState() == Endpoint::State::BUSY);
        ASSERT(fixture.device.data_out.TransferAttempts() == transfer_attempts + 1U);
      });

  ASSERT(fixture.device.data_out.StaleOutDispatchAttempts() == dispatch_attempts);
  ASSERT(fixture.device.data_out.HasStaleOutCompletion());
  ASSERT(fixture.cdc.OutCompletionCount() == completion_count);
  ASSERT(fixture.device.data_out.GetState() == Endpoint::State::BUSY);

  fixture.device.CompleteControlStatus();
  const std::array<uint8_t, 2U> current_payload{0xB4U, 0xB5U};
  fixture.device.CompleteOut(fixture.device.data_out, current_payload);
  ASSERT(fixture.cdc.OutCompletionCount() == completion_count + 1U);
}

void SynchronousCompletionDrainsAtDepthOne()
{
  SingleCdcFixture fixture;
  fixture.device.data_in.SetCompleteInBeforeTransferReturns(true);
  fixture.device.Initialize();

  const std::array<uint8_t, 10U> payload{0xA0U, 0xA1U, 0xA2U, 0xA3U, 0xA4U,
                                         0xA5U, 0xA6U, 0xA7U, 0xA8U, 0xA9U};
  CompletionProbe completion;
  Submit(fixture.cdc, payload, completion);

  ExpectCompletedOk(completion);
  ASSERT(fixture.cdc.QueuedBytes() == 0U);
  ASSERT(fixture.device.data_in.MaxTransferDepth() == 1U);
  ASSERT(fixture.device.data_in.GetState() == Endpoint::State::IDLE);
  ASSERT(fixture.device.data_in.InTransfers().size() == 3U);
  ASSERT((fixture.device.data_in.InTransfers()[0] ==
          std::vector<uint8_t>{0xA0U, 0xA1U, 0xA2U, 0xA3U}));
  ASSERT((fixture.device.data_in.InTransfers()[1] ==
          std::vector<uint8_t>{0xA4U, 0xA5U, 0xA6U, 0xA7U}));
  ASSERT((fixture.device.data_in.InTransfers()[2] == std::vector<uint8_t>{0xA8U, 0xA9U}));
}

struct ReentrantWriteState
{
  TestCDCUart* cdc = nullptr;
  ConstRawData next_payload{};
  LibXR::WriteOperation* next_operation = nullptr;
  size_t completion_count = 0U;
  ErrorCode completion_result = ErrorCode::PENDING;
  ErrorCode nested_submit_result = ErrorCode::PENDING;
};

void SubmitNextWrite(bool in_isr, ReentrantWriteState* state, ErrorCode result)
{
  ++state->completion_count;
  state->completion_result = result;
  state->nested_submit_result =
      state->cdc->Write(state->next_payload, *state->next_operation, in_isr);
}

void CompletionCallbackCanSubmitReadyWrite()
{
  SingleCdcFixture fixture;
  fixture.device.Initialize();

  const std::array<uint8_t, 3U> first{0xB1U, 0xB2U, 0xB3U};
  const std::array<uint8_t, 2U> second{0xB4U, 0xB5U};
  CompletionProbe second_completion;
  ReentrantWriteState first_completion{&fixture.cdc,
                                       ConstRawData{second.data(), second.size()},
                                       &second_completion.operation};
  auto first_callback =
      LibXR::Callback<ErrorCode>::Create(SubmitNextWrite, &first_completion);
  LibXR::WriteOperation first_operation(first_callback);

  ASSERT(fixture.cdc.Write(ConstRawData{first.data(), first.size()}, first_operation) ==
         ErrorCode::OK);
  ASSERT(first_completion.completion_count == 1U);
  ASSERT(first_completion.completion_result == ErrorCode::OK);
  ASSERT(first_completion.nested_submit_result == ErrorCode::OK);
  ExpectCompletedOk(second_completion);
  ASSERT(fixture.device.data_in.InTransfers().size() == 1U);
  ASSERT(fixture.device.data_in.CurrentBufferPrefix(second.size()) ==
         std::vector<uint8_t>(second.begin(), second.end()));
  ASSERT(fixture.device.data_in.MaxTransferDepth() == 1U);

  fixture.device.CompleteIn(fixture.device.data_in);
  ASSERT(fixture.device.data_in.InTransfers().size() == 2U);
  ASSERT(fixture.device.data_in.InTransfers()[1] ==
         std::vector<uint8_t>(second.begin(), second.end()));
  fixture.device.CompleteIn(fixture.device.data_in);
}

void TwoCdcsShareOneDeviceOwner()
{
  TestCDCUart first(Endpoint::EPNumber::EP1, Endpoint::EPNumber::EP2,
                    Endpoint::EPNumber::EP3);
  TestCDCUart second(Endpoint::EPNumber::EP4, Endpoint::EPNumber::EP5,
                     Endpoint::EPNumber::EP6);
  FakeUSBDevice device(first, second);
  device.Initialize();

  const std::array<uint8_t, 3U> first_payload{0xC1U, 0xC2U, 0xC3U};
  const std::array<uint8_t, 2U> second_payload{0xC4U, 0xC5U};
  CompletionProbe first_completion;
  CompletionProbe second_completion;

  ASSERT(device.RunIrq(
      [&]
      {
        Submit(first, first_payload, first_completion, true);
        Submit(second, second_payload, second_completion, true);
        ASSERT(device.data_in.InTransfers().empty());
        ASSERT(device.data_in_2.InTransfers().empty());
      }));

  ExpectCompletedOk(first_completion);
  ExpectCompletedOk(second_completion);
  ASSERT(device.data_in.InTransfers().size() == 1U);
  ASSERT(device.data_in_2.InTransfers().size() == 1U);
  ASSERT(device.data_in.InTransfers()[0] ==
         std::vector<uint8_t>(first_payload.begin(), first_payload.end()));
  ASSERT(device.data_in_2.InTransfers()[0] ==
         std::vector<uint8_t>(second_payload.begin(), second_payload.end()));
  device.CompleteIn(device.data_in);
  device.CompleteIn(device.data_in_2);
  device.Shutdown();
}

void RxBackpressurePublishesPendingDataBeforeRearm()
{
  SingleCdcFixture fixture(6U);
  fixture.device.Initialize();

  const std::array<uint8_t, 4U> first{0xD1U, 0xD2U, 0xD3U, 0xD4U};
  const std::array<uint8_t, 4U> pending{0xD5U, 0xD6U, 0xD7U, 0xD8U};
  fixture.device.CompleteOut(fixture.device.data_out, first);
  fixture.device.CompleteOut(fixture.device.data_out, pending);

  ASSERT(fixture.cdc.QueuedRxBytes() == first.size());
  ASSERT(fixture.device.data_out.GetState() == Endpoint::State::IDLE);
  const size_t attempts_before_read = fixture.device.data_out.TransferAttempts();

  std::array<uint8_t, 4U> first_received{};
  LibXR::ReadOperation first_operation;
  ASSERT(fixture.device.RunIrq(
      [&]
      {
        ASSERT(fixture.cdc.Read(RawData{first_received.data(), first_received.size()},
                                first_operation, true) == ErrorCode::OK);
        ASSERT(fixture.device.data_out.TransferAttempts() == attempts_before_read);
        ASSERT(fixture.device.data_out.GetState() == Endpoint::State::IDLE);
      }));

  ASSERT(first_received == first);
  ASSERT(fixture.cdc.QueuedRxBytes() == pending.size());
  ASSERT(fixture.device.data_out.TransferAttempts() == attempts_before_read + 1U);
  ASSERT(fixture.device.data_out.GetState() == Endpoint::State::BUSY);

  std::array<uint8_t, 4U> pending_received{};
  LibXR::ReadOperation pending_operation;
  ASSERT(fixture.cdc.Read(RawData{pending_received.data(), pending_received.size()},
                          pending_operation) == ErrorCode::OK);
  ASSERT(pending_received == pending);
  ASSERT(fixture.cdc.QueuedRxBytes() == 0U);
  ASSERT(fixture.device.data_out.GetState() == Endpoint::State::BUSY);
}

void HighSpeedFullPacketFitsRxQueueAndRearms()
{
  static constexpr size_t HIGH_SPEED_MPS = 512U;
  SingleCdcFixture fixture(HIGH_SPEED_MPS, Speed::HIGH);
  fixture.device.data_out.SetBulkPacketSize(HIGH_SPEED_MPS);
  fixture.device.Initialize();

  ASSERT(fixture.device.data_out.MaxPacketSize() == HIGH_SPEED_MPS);
  std::array<uint8_t, HIGH_SPEED_MPS> payload{};
  for (size_t i = 0; i < payload.size(); ++i)
  {
    payload[i] = static_cast<uint8_t>(i);
  }

  fixture.device.CompleteOut(fixture.device.data_out, payload);
  ASSERT(fixture.cdc.QueuedRxBytes() == payload.size());
  ASSERT(fixture.device.data_out.GetState() == Endpoint::State::BUSY);

  std::array<uint8_t, HIGH_SPEED_MPS> received{};
  LibXR::ReadOperation operation;
  ASSERT(fixture.cdc.Read(RawData{received.data(), received.size()}, operation) ==
         ErrorCode::OK);
  ASSERT(received == payload);
  ASSERT(fixture.cdc.QueuedRxBytes() == 0U);
  ASSERT(fixture.device.data_out.GetState() == Endpoint::State::BUSY);
}

}  // namespace

void test_cdc_uart_tx()
{
  BaseLifecycleGateDropsStaleWork();
  BaseSerialStateBusyUsesCompletionCarrier();
  BaseFatalGateSuppressesCallbacksAndTransfers();
  SerialStateStartFailureFailStopsGeneration();
  OutRearmStartFailureFailStopsGeneration();
  RequiresExplicitConfigurationBeforeAcceptance();
  ActiveAndReadyCompleteEarlyWithoutCrossingRequestBoundaries();
  SingleBufferAcceptsOnlyActiveUntilCompletion();
  ClearDataOutHaltRearmsAndReceives();
  ActiveStartFailureFailStopsAcceptedOperation();
  ReadyStartFailureKeepsOnlyUnacceptedSuffixForReconfiguration();
  UnbindRebindDropsAcceptedSlotsAndKeepsSuffix();
  ZlpStartFailureFailStopsWithoutRetry();
  ExactPacketAtStableIdleSendsZlp();
  ReadyDataSuppressesIntermediateZlp();
  CompletionAndWriteSnapshotStartsDataBeforeZlp();
  LifecycleDominatesCompletionInSameOwnerSnapshot();
  CloseRejectsStaleCompletionAfterRebind();
  SameSnapshotRebindDropsStaleOutCompletion();
  SameSnapshotRebindDropsStaleInCompletion();
  SameSnapshotClearHaltDropsStaleOutCompletion();
  SynchronousCompletionDrainsAtDepthOne();
  CompletionCallbackCanSubmitReadyWrite();
  TwoCdcsShareOneDeviceOwner();
  RxBackpressurePublishesPendingDataBeforeRearm();
  HighSpeedFullPacketFitsRxQueueAndRearms();
}
