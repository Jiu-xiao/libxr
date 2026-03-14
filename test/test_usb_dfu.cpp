#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "dfu.hpp"
#include "ep_pool.hpp"
#include "libxr_def.hpp"
#include "test.hpp"
#include "usb/core/desc_str.hpp"

namespace
{
using LibXR::ConstRawData;
using LibXR::ErrorCode;
using LibXR::RawData;
using LibXR::USB::DescriptorStrings;
using LibXR::USB::DeviceCore;
using LibXR::USB::DeviceDescriptor;
using LibXR::USB::DFUCapabilities;
using LibXR::USB::DFUClass;
using LibXR::USB::DFURequest;
using LibXR::USB::DFUState;
using LibXR::USB::DFUStatusCode;
using LibXR::USB::Endpoint;
using LibXR::USB::EndpointPool;
using LibXR::USB::ConfigDescriptorItem;
using LibXR::USB::SetupPacket;
using LibXR::USB::Speed;
using LibXR::USB::USBSpec;

constexpr uint16_t kVid = 0x1209u;
constexpr uint16_t kPid = 0x3070u;
constexpr uint16_t kBcd = 0x0001u;
constexpr uint16_t kTransferSize = 16u;
constexpr size_t kMockImageBytes = 128u;

constexpr auto kLangPack = DescriptorStrings::MakeLanguagePack(
    DescriptorStrings::Language::EN_US, "XRobot", "DFU-Test", "HOSTTEST");

class MockEndpoint : public Endpoint
{
 public:
  MockEndpoint(EPNumber number, Direction direction, RawData buffer)
      : Endpoint(number, direction, buffer)
  {
  }

  void Configure(const Config& cfg) override
  {
    GetConfig() = cfg;
    transfer_pending_ = false;
    last_transfer_size_ = 0u;
    SetState(State::IDLE);
  }

  void Close() override
  {
    transfer_pending_ = false;
    last_transfer_size_ = 0u;
    SetState(State::DISABLED);
  }

  ErrorCode Stall() override
  {
    transfer_pending_ = false;
    SetState(State::STALLED);
    return ErrorCode::OK;
  }

  ErrorCode ClearStall() override
  {
    transfer_pending_ = false;
    SetState(State::IDLE);
    return ErrorCode::OK;
  }

  ErrorCode Transfer(size_t size) override
  {
    last_transfer_size_ = size;
    transfer_pending_ = true;
    SetState(State::BUSY);
    return ErrorCode::OK;
  }

  [[nodiscard]] bool TransferPending() const { return transfer_pending_; }
  [[nodiscard]] size_t LastTransferSize() const { return last_transfer_size_; }

  std::vector<uint8_t> CopyBuffer(size_t size) const
  {
    auto buffer = GetBuffer();
    auto* first = reinterpret_cast<uint8_t*>(buffer.addr_);
    return std::vector<uint8_t>(first, first + size);
  }

  void CompleteIn(size_t actual_size)
  {
    ASSERT(TransferPending());
    transfer_pending_ = false;
    OnTransferCompleteCallback(false, actual_size);
  }

  void CompleteOut(ConstRawData data)
  {
    ASSERT(TransferPending());
    auto buffer = GetBuffer();
    ASSERT(buffer.size_ >= data.size_);
    if (data.size_ > 0u)
    {
      std::memcpy(buffer.addr_, data.addr_, data.size_);
    }
    transfer_pending_ = false;
    OnTransferCompleteCallback(false, data.size_);
  }

 private:
  bool transfer_pending_ = false;
  size_t last_transfer_size_ = 0u;
};

class MockDfuBackend
{
 public:
  MockDfuBackend()
  {
    static constexpr std::array<uint8_t, 8> kExistingImage = {
        0x41u, 0x42u, 0x43u, 0x44u, 0x45u, 0x46u, 0x47u, 0x48u};
    std::memcpy(committed_.data(), kExistingImage.data(), kExistingImage.size());
    committed_size_ = kExistingImage.size();
  }

  DFUCapabilities GetDfuCapabilities() const
  {
    DFUCapabilities caps = {};
    caps.can_download = true;
    caps.can_upload = true;
    caps.manifestation_tolerant = false;
    caps.will_detach = false;
    caps.detach_timeout_ms = 0u;
    caps.transfer_size = kTransferSize;
    return caps;
  }

  ErrorCode DfuSetAlternate(uint8_t alt)
  {
    return (alt == 0u) ? ErrorCode::OK : ErrorCode::NOT_SUPPORT;
  }

  void DfuAbort(uint8_t alt)
  {
    ASSERT(alt == 0u);
    ++abort_count_;
    ResetDownload();
    ResetUpload();
  }

  void DfuClearStatus(uint8_t alt)
  {
    ASSERT(alt == 0u);
    ResetDownload();
    ResetUpload();
    ++clear_status_count_;
  }

  DFUStatusCode DfuDownload(uint8_t alt, uint16_t block_num, ConstRawData data,
                            uint32_t& poll_timeout_ms)
  {
    poll_timeout_ms = 0u;
    if (alt != 0u)
    {
      return DFUStatusCode::ERR_TARGET;
    }
    if (data.size_ == 0u || data.size_ > kTransferSize)
    {
      return DFUStatusCode::ERR_USBR;
    }

    if ((block_num == 0u) && download_started_ &&
        ((staged_size_ != 0u) || (next_download_block_num_ != 0u)))
    {
      ResetDownload();
    }

    if (!download_started_)
    {
      if (block_num != 0u)
      {
        return DFUStatusCode::ERR_ADDRESS;
      }
      ResetDownload();
      ResetUpload();
      download_started_ = true;
    }
    else if (block_num != next_download_block_num_)
    {
      return DFUStatusCode::ERR_ADDRESS;
    }

    const size_t offset = staged_size_;
    if ((offset + data.size_) > staged_.size())
    {
      return DFUStatusCode::ERR_ADDRESS;
    }

    std::memcpy(staged_.data() + offset, data.addr_, data.size_);
    staged_size_ = offset + data.size_;
    next_download_block_num_ = static_cast<uint16_t>(block_num + 1u);
    return DFUStatusCode::OK;
  }

  size_t DfuUpload(uint8_t alt, uint16_t block_num, RawData data, DFUStatusCode& status,
                   uint32_t& poll_timeout_ms)
  {
    poll_timeout_ms = 0u;
    status = DFUStatusCode::OK;
    if (alt != 0u)
    {
      status = DFUStatusCode::ERR_TARGET;
      return 0u;
    }

    if (data.size_ == 0u)
    {
      status = DFUStatusCode::ERR_USBR;
      return 0u;
    }

    if (block_num == 0u)
    {
      ResetUpload();
      upload_started_ = true;
    }
    else if (!upload_started_ || (block_num != next_upload_block_num_))
    {
      status = DFUStatusCode::ERR_ADDRESS;
      return 0u;
    }

    const size_t offset = upload_offset_;
    if (offset >= committed_size_)
    {
      return 0u;
    }

    size_t copy_size = committed_size_ - offset;
    if (copy_size > data.size_)
    {
      copy_size = data.size_;
    }
    std::memcpy(data.addr_, committed_.data() + offset, copy_size);
    upload_offset_ = offset + copy_size;
    next_upload_block_num_ = static_cast<uint16_t>(block_num + 1u);
    return copy_size;
  }

  DFUStatusCode DfuManifest(uint8_t alt, uint32_t& poll_timeout_ms)
  {
    if (alt != 0u)
    {
      return DFUStatusCode::ERR_TARGET;
    }
    if (!download_started_ || staged_size_ == 0u)
    {
      return DFUStatusCode::ERR_NOTDONE;
    }

    std::memcpy(committed_.data(), staged_.data(), staged_size_);
    committed_size_ = staged_size_;
    ResetDownload();
    ResetUpload();
    ++manifest_count_;
    poll_timeout_ms = 25u;
    return DFUStatusCode::OK;
  }

  DFUStatusCode DfuGetDownloadStatus(uint8_t alt, bool& busy, uint32_t& poll_timeout_ms)
  {
    poll_timeout_ms = 0u;
    busy = false;
    return (alt == 0u) ? DFUStatusCode::OK : DFUStatusCode::ERR_TARGET;
  }

  DFUStatusCode DfuGetManifestStatus(uint8_t alt, bool& busy, uint32_t& poll_timeout_ms)
  {
    poll_timeout_ms = 0u;
    busy = false;
    return (alt == 0u) ? DFUStatusCode::OK : DFUStatusCode::ERR_TARGET;
  }

  void DfuCommitManifestWaitReset(uint8_t alt) { ASSERT(alt == 0u); }

  [[nodiscard]] size_t AbortCount() const { return abort_count_; }
  [[nodiscard]] size_t ClearStatusCount() const { return clear_status_count_; }
  [[nodiscard]] size_t ManifestCount() const { return manifest_count_; }
  [[nodiscard]] size_t CommittedSize() const { return committed_size_; }

  std::vector<uint8_t> CommittedBytes() const
  {
    return std::vector<uint8_t>(committed_.begin(), committed_.begin() + committed_size_);
  }

 private:
  void ResetDownload()
  {
    staged_size_ = 0u;
    next_download_block_num_ = 0u;
    download_started_ = false;
  }

  void ResetUpload()
  {
    upload_started_ = false;
    upload_offset_ = 0u;
    next_upload_block_num_ = 0u;
  }

  std::array<uint8_t, kMockImageBytes> staged_ = {};
  std::array<uint8_t, kMockImageBytes> committed_ = {};
  size_t staged_size_ = 0u;
  size_t upload_offset_ = 0u;
  size_t committed_size_ = 0u;
  uint16_t next_download_block_num_ = 0u;
  uint16_t next_upload_block_num_ = 0u;
  size_t abort_count_ = 0u;
  size_t clear_status_count_ = 0u;
  size_t manifest_count_ = 0u;
  bool download_started_ = false;
  bool upload_started_ = false;
};

class MockDeviceCore : public DeviceCore
{
 public:
  MockDeviceCore(
      EndpointPool& endpoint_pool,
      const std::initializer_list<const DescriptorStrings::LanguagePack*>& lang_list,
      const std::initializer_list<const std::initializer_list<LibXR::USB::ConfigDescriptorItem*>>
          configs)
      : DeviceCore(endpoint_pool, USBSpec::USB_2_1, Speed::HIGH,
                   DeviceDescriptor::PacketSize0::SIZE_64, kVid, kPid, kBcd, lang_list,
                   configs)
  {
  }

  ErrorCode SetAddress(uint8_t address, Context) override
  {
    address_ = address;
    return ErrorCode::OK;
  }

  void Start(bool) override {}
  void Stop(bool) override {}

  [[nodiscard]] uint8_t Address() const { return address_; }

 private:
  uint8_t address_ = 0u;
};

class DfuHarness
{
 public:
  DfuHarness()
      : ep0_in_(Endpoint::EPNumber::EP0, Endpoint::Direction::IN,
                {ep0_in_buffer_.data(), ep0_in_buffer_.size()}),
        ep0_out_(Endpoint::EPNumber::EP0, Endpoint::Direction::OUT,
                 {ep0_out_buffer_.data(), ep0_out_buffer_.size()}),
        pool_(2u),
        dfu_(backend_, "Mock DFU"),
        device_(pool_, {&kLangPack}, {{&dfu_}})
  {
    pool_.SetEndpoint0(&ep0_in_, &ep0_out_);
    device_.Init(false);
    device_.Start(false);
  }

  ~DfuHarness()
  {
    device_.Stop(false);
    device_.Deinit(false);
  }

  std::vector<uint8_t> ControlIn(const SetupPacket& setup)
  {
    device_.OnSetupPacket(false, &setup);
    ASSERT(!ep0_in_.IsStalled());
    ASSERT(ep0_in_.TransferPending());

    const size_t transfer_size = ep0_in_.LastTransferSize();
    auto payload = ep0_in_.CopyBuffer(transfer_size);
    ep0_in_.CompleteIn(transfer_size);
    if (ep0_out_.TransferPending())
    {
      ep0_out_.CompleteOut({nullptr, 0});
    }
    return payload;
  }

  void ControlOut(const SetupPacket& setup, ConstRawData payload = {nullptr, 0})
  {
    device_.OnSetupPacket(false, &setup);
    ASSERT(!ep0_out_.IsStalled());

    if (setup.wLength > 0u)
    {
      ASSERT(ep0_out_.TransferPending());
      ASSERT(payload.size_ == setup.wLength);
      ep0_out_.CompleteOut(payload);
    }

    if (ep0_in_.TransferPending())
    {
      ep0_in_.CompleteIn(ep0_in_.LastTransferSize());
    }
  }

  MockDfuBackend& Backend() { return backend_; }

 private:
  std::array<uint8_t, 64> ep0_in_buffer_ = {};
  std::array<uint8_t, 64> ep0_out_buffer_ = {};
  MockEndpoint ep0_in_;
  MockEndpoint ep0_out_;
  EndpointPool pool_;
  MockDfuBackend backend_;
  DFUClass<MockDfuBackend, kTransferSize> dfu_;
  MockDeviceCore device_;
};

SetupPacket MakeSetup(uint8_t bm_request_type, uint8_t b_request, uint16_t w_value,
                      uint16_t w_index, uint16_t w_length)
{
  return SetupPacket{bm_request_type, b_request, w_value, w_index, w_length};
}

std::string DecodeUsbString(const std::vector<uint8_t>& descriptor)
{
  ASSERT(descriptor.size() >= 2u);
  ASSERT(descriptor[1] == 0x03u);

  std::string text;
  for (size_t i = 2; i + 1 < descriptor.size(); i += 2)
  {
    ASSERT(descriptor[i + 1] == 0u);
    text.push_back(static_cast<char>(descriptor[i]));
  }
  return text;
}

class MockInterfaceStringClass : public LibXR::USB::DeviceClass
{
 protected:
  void BindEndpoints(EndpointPool&, uint8_t start_itf_num, bool) override
  {
    desc_[0] = {9,
                static_cast<uint8_t>(LibXR::USB::DescriptorType::INTERFACE),
                start_itf_num,
                0,
                0,
                0xFFu,
                0x00u,
                0x00u,
                GetInterfaceStringIndex(0u)};

    desc_[1] = {9,
                static_cast<uint8_t>(LibXR::USB::DescriptorType::INTERFACE),
                static_cast<uint8_t>(start_itf_num + 1u),
                0,
                0,
                0xFFu,
                0x00u,
                0x01u,
                GetInterfaceStringIndex(1u)};

    SetData(RawData{reinterpret_cast<uint8_t*>(desc_.data()), sizeof(desc_)});
  }

  void UnbindEndpoints(EndpointPool&, bool) override {}

  size_t GetInterfaceCount() override { return desc_.size(); }
  bool HasIAD() override { return false; }
  size_t GetMaxConfigSize() override { return sizeof(desc_); }

 public:
  const char* GetInterfaceString(size_t local_interface_index) const override
  {
    switch (local_interface_index)
    {
      case 0u:
        return "Mock Control";
      case 1u:
        return "Mock Data";
      default:
        return nullptr;
    }
  }

 private:
  std::array<ConfigDescriptorItem::InterfaceDescriptor, 2> desc_ = {};
};

class InterfaceStringHarness
{
 public:
  InterfaceStringHarness()
      : ep0_in_(Endpoint::EPNumber::EP0, Endpoint::Direction::IN,
                {ep0_in_buffer_.data(), ep0_in_buffer_.size()}),
        ep0_out_(Endpoint::EPNumber::EP0, Endpoint::Direction::OUT,
                 {ep0_out_buffer_.data(), ep0_out_buffer_.size()}),
        pool_(2u),
        device_(pool_, {&kLangPack}, {{&string_class_}})
  {
    pool_.SetEndpoint0(&ep0_in_, &ep0_out_);
    device_.Init(false);
    device_.Start(false);
  }

  ~InterfaceStringHarness()
  {
    device_.Stop(false);
    device_.Deinit(false);
  }

  std::vector<uint8_t> ControlIn(const SetupPacket& setup)
  {
    device_.OnSetupPacket(false, &setup);
    ASSERT(!ep0_in_.IsStalled());
    ASSERT(ep0_in_.TransferPending());

    const size_t transfer_size = ep0_in_.LastTransferSize();
    auto payload = ep0_in_.CopyBuffer(transfer_size);
    ep0_in_.CompleteIn(transfer_size);
    if (ep0_out_.TransferPending())
    {
      ep0_out_.CompleteOut({nullptr, 0});
    }
    return payload;
  }

  void ControlOut(const SetupPacket& setup)
  {
    device_.OnSetupPacket(false, &setup);
    ASSERT(!ep0_out_.IsStalled());

    if (ep0_in_.TransferPending())
    {
      ep0_in_.CompleteIn(ep0_in_.LastTransferSize());
    }
  }

 private:
  std::array<uint8_t, 64> ep0_in_buffer_ = {};
  std::array<uint8_t, 64> ep0_out_buffer_ = {};
  MockEndpoint ep0_in_;
  MockEndpoint ep0_out_;
  EndpointPool pool_;
  MockInterfaceStringClass string_class_;
  MockDeviceCore device_;
};
}  // namespace

void test_usb_dfu()
{
  DfuHarness harness;

  auto device_descriptor = harness.ControlIn(
      MakeSetup(0x80u, 0x06u, 0x0100u, 0x0000u, DeviceDescriptor::DEVICE_DESC_LENGTH));
  ASSERT(device_descriptor.size() == DeviceDescriptor::DEVICE_DESC_LENGTH);
  ASSERT(device_descriptor[4] ==
         static_cast<uint8_t>(DeviceDescriptor::ClassID::APPLICATION_SPECIFIC));
  ASSERT(device_descriptor[5] == 0x01u);
  ASSERT(device_descriptor[6] == 0x02u);

  harness.ControlOut(MakeSetup(0x00u, 0x09u, 0x0001u, 0x0000u, 0x0000u));

  auto config_descriptor = harness.ControlIn(MakeSetup(0x80u, 0x06u, 0x0200u, 0x0000u, 64u));
  ASSERT(config_descriptor.size() == 27u);
  ASSERT(config_descriptor[9] == 9u);
  ASSERT(config_descriptor[10] == 0x04u);
  ASSERT(config_descriptor[14] == 0xFEu);
  ASSERT(config_descriptor[15] == 0x01u);
  ASSERT(config_descriptor[16] == 0x02u);
  ASSERT(config_descriptor[17] == 4u);
  ASSERT(config_descriptor[18] == 9u);
  ASSERT(config_descriptor[19] == 0x21u);
  ASSERT((config_descriptor[20] & 0x01u) != 0u);
  ASSERT((config_descriptor[20] & 0x02u) != 0u);
  ASSERT((config_descriptor[20] & 0x04u) == 0u);
  ASSERT(config_descriptor[23] == static_cast<uint8_t>(kTransferSize & 0xFFu));
  ASSERT(config_descriptor[24] == static_cast<uint8_t>((kTransferSize >> 8) & 0xFFu));

  auto func_descriptor = harness.ControlIn(MakeSetup(0x81u, 0x06u, 0x2100u, 0x0000u, 9u));
  ASSERT(func_descriptor.size() == 9u);
  ASSERT(func_descriptor[1] == 0x21u);
  ASSERT(func_descriptor[2] == 0x03u);

  auto dfu_string = harness.ControlIn(MakeSetup(0x80u, 0x06u, 0x0304u, 0x0409u, 64u));
  ASSERT(DecodeUsbString(dfu_string) == "Mock DFU");

  auto state_idle =
      harness.ControlIn(MakeSetup(0xA1u, static_cast<uint8_t>(DFURequest::GETSTATE), 0u, 0u, 1u));
  ASSERT(state_idle.size() == 1u);
  ASSERT(state_idle[0] == static_cast<uint8_t>(DFUState::DFU_IDLE));

  static constexpr std::array<uint8_t, 8> kExistingImage = {
      0x41u, 0x42u, 0x43u, 0x44u, 0x45u, 0x46u, 0x47u, 0x48u};
  auto upload_existing_0 =
      harness.ControlIn(MakeSetup(0xA1u, static_cast<uint8_t>(DFURequest::UPLOAD), 0u, 0u, 4u));
  ASSERT(upload_existing_0.size() == 4u);
  ASSERT(std::memcmp(upload_existing_0.data(), kExistingImage.data(), upload_existing_0.size()) ==
         0);

  auto upload_existing_1 =
      harness.ControlIn(MakeSetup(0xA1u, static_cast<uint8_t>(DFURequest::UPLOAD), 1u, 0u, 4u));
  ASSERT(upload_existing_1.size() == 4u);
  ASSERT(std::memcmp(upload_existing_1.data(), kExistingImage.data() + 4u,
                     upload_existing_1.size()) == 0);

  auto upload_eof =
      harness.ControlIn(MakeSetup(0xA1u, static_cast<uint8_t>(DFURequest::UPLOAD), 2u, 0u, 4u));
  ASSERT(upload_eof.empty());

  harness.ControlOut(
      MakeSetup(0x21u, static_cast<uint8_t>(DFURequest::DNLOAD), 0u, 0u, 0u));
  auto status_err =
      harness.ControlIn(MakeSetup(0xA1u, static_cast<uint8_t>(DFURequest::GETSTATUS), 0u, 0u, 6u));
  ASSERT(status_err.size() == 6u);
  ASSERT(status_err[0] == static_cast<uint8_t>(DFUStatusCode::ERR_NOTDONE));
  ASSERT(status_err[4] == static_cast<uint8_t>(DFUState::DFU_ERROR));

  harness.ControlOut(
      MakeSetup(0x21u, static_cast<uint8_t>(DFURequest::CLRSTATUS), 0u, 0u, 0u));
  ASSERT(harness.Backend().ClearStatusCount() == 1u);

  auto state_after_clear =
      harness.ControlIn(MakeSetup(0xA1u, static_cast<uint8_t>(DFURequest::GETSTATE), 0u, 0u, 1u));
  ASSERT(state_after_clear.size() == 1u);
  ASSERT(state_after_clear[0] == static_cast<uint8_t>(DFUState::DFU_IDLE));

  static constexpr std::array<uint8_t, 18> kDownloadImage = {
      0x10u, 0x20u, 0x30u, 0x40u, 0x50u, 0x60u, 0x70u, 0x80u, 0x90u,
      0xA0u, 0xB0u, 0xC0u, 0xD0u, 0xE0u, 0xF0u, 0x01u, 0x02u, 0x03u};

  harness.ControlOut(
      MakeSetup(0x21u, static_cast<uint8_t>(DFURequest::DNLOAD), 0u, 0u, 4u),
      {kDownloadImage.data(), 4u});
  auto status_dnload =
      harness.ControlIn(MakeSetup(0xA1u, static_cast<uint8_t>(DFURequest::GETSTATUS), 0u, 0u, 6u));
  ASSERT(status_dnload.size() == 6u);
  ASSERT(status_dnload[0] == static_cast<uint8_t>(DFUStatusCode::OK));
  ASSERT(status_dnload[4] == static_cast<uint8_t>(DFUState::DFU_DNLOAD_IDLE));

  harness.ControlOut(
      MakeSetup(0x21u, static_cast<uint8_t>(DFURequest::ABORT), 0u, 0u, 0u));
  ASSERT(harness.Backend().AbortCount() >= 1u);

  auto state_after_abort =
      harness.ControlIn(MakeSetup(0xA1u, static_cast<uint8_t>(DFURequest::GETSTATE), 0u, 0u, 1u));
  ASSERT(state_after_abort.size() == 1u);
  ASSERT(state_after_abort[0] == static_cast<uint8_t>(DFUState::DFU_IDLE));

  for (uint16_t block = 0u; block < 5u; ++block)
  {
    const size_t offset = static_cast<size_t>(block) * 4u;
    size_t chunk_size = kDownloadImage.size() - offset;
    if (chunk_size > 4u)
    {
      chunk_size = 4u;
    }
    harness.ControlOut(MakeSetup(0x21u, static_cast<uint8_t>(DFURequest::DNLOAD), block, 0u,
                                 static_cast<uint16_t>(chunk_size)),
                       {kDownloadImage.data() + offset, chunk_size});
    auto chunk_status = harness.ControlIn(
        MakeSetup(0xA1u, static_cast<uint8_t>(DFURequest::GETSTATUS), 0u, 0u, 6u));
    ASSERT(chunk_status.size() == 6u);
    ASSERT(chunk_status[0] == static_cast<uint8_t>(DFUStatusCode::OK));
    ASSERT(chunk_status[4] == static_cast<uint8_t>(DFUState::DFU_DNLOAD_IDLE));
  }

  harness.ControlOut(
      MakeSetup(0x21u, static_cast<uint8_t>(DFURequest::DNLOAD), 5u, 0u, 0u));
  auto status_manifest =
      harness.ControlIn(MakeSetup(0xA1u, static_cast<uint8_t>(DFURequest::GETSTATUS), 0u, 0u, 6u));
  ASSERT(status_manifest.size() == 6u);
  ASSERT(status_manifest[0] == static_cast<uint8_t>(DFUStatusCode::OK));
  ASSERT(status_manifest[1] == 25u);
  ASSERT(status_manifest[2] == 0u);
  ASSERT(status_manifest[3] == 0u);
  ASSERT(status_manifest[4] == static_cast<uint8_t>(DFUState::DFU_MANIFEST));

  auto status_manifest_done =
      harness.ControlIn(MakeSetup(0xA1u, static_cast<uint8_t>(DFURequest::GETSTATUS), 0u, 0u, 6u));
  ASSERT(status_manifest_done.size() == 6u);
  ASSERT(status_manifest_done[0] == static_cast<uint8_t>(DFUStatusCode::OK));
  ASSERT(status_manifest_done[1] == 0u);
  ASSERT(status_manifest_done[2] == 0u);
  ASSERT(status_manifest_done[3] == 0u);
  ASSERT(status_manifest_done[4] == static_cast<uint8_t>(DFUState::DFU_MANIFEST_WAIT_RESET));
  ASSERT(harness.Backend().ManifestCount() == 1u);
  ASSERT(harness.Backend().CommittedSize() == kDownloadImage.size());

  const auto committed = harness.Backend().CommittedBytes();
  ASSERT(committed.size() == kDownloadImage.size());
  ASSERT(std::memcmp(committed.data(), kDownloadImage.data(), kDownloadImage.size()) == 0);
}

void test_usb_interface_strings()
{
  InterfaceStringHarness harness;

  harness.ControlOut(MakeSetup(0x00u, 0x09u, 0x0001u, 0x0000u, 0x0000u));

  auto config_descriptor = harness.ControlIn(MakeSetup(0x80u, 0x06u, 0x0200u, 0x0000u, 64u));
  ASSERT(config_descriptor.size() == sizeof(ConfigDescriptorItem::Header) +
                                         sizeof(ConfigDescriptorItem::InterfaceDescriptor) * 2u);

  constexpr size_t kInterfaceStringOffset = 8u;
  const size_t first_i_interface =
      sizeof(ConfigDescriptorItem::Header) + kInterfaceStringOffset;
  const size_t second_i_interface =
      sizeof(ConfigDescriptorItem::Header) + sizeof(ConfigDescriptorItem::InterfaceDescriptor) +
      kInterfaceStringOffset;

  ASSERT(config_descriptor[first_i_interface] == 4u);
  ASSERT(config_descriptor[second_i_interface] == 5u);

  auto control_string =
      harness.ControlIn(MakeSetup(0x80u, 0x06u, 0x0304u, 0x0409u, 64u));
  ASSERT(DecodeUsbString(control_string) == "Mock Control");

  auto data_string =
      harness.ControlIn(MakeSetup(0x80u, 0x06u, 0x0305u, 0x0409u, 64u));
  ASSERT(DecodeUsbString(data_string) == "Mock Data");
}
