// ===================
// File: daplink_v2.cpp
// ===================
#include "daplink_v2.hpp"

#include <cstdint>
#include <cstring>

#include "timebase.hpp"

namespace LibXR::USB
{

namespace
{
template <typename E>
static constexpr uint8_t U8(E e)
{
  return static_cast<uint8_t>(e);
}

static inline uint16_t LoadLE16(const uint8_t* p)
{
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

static inline uint32_t LoadLE32(const uint8_t* p)
{
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

static inline void StoreLE16(uint8_t* p, uint16_t v)
{
  p[0] = static_cast<uint8_t>(v & 0xFFu);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
}

static inline void StoreLE32(uint8_t* p, uint32_t v)
{
  p[0] = static_cast<uint8_t>(v & 0xFFu);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
  p[2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
  p[3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
}

static inline void BusyWaitUs(uint32_t us)
{
  const auto start = LibXR::Timebase::GetMicroseconds();
  while ((LibXR::Timebase::GetMicroseconds() - start) < us)
  {
  }
}

}  // namespace

// ============================================================================
// WinUSB (MS OS 2.0) descriptor plumbing
// ============================================================================

void DapLinkV2Class::InitWinUsbDescriptors_()
{
  // --- BOS ---
  winusb_bos_ = {};  // Zero-out the struct
  winusb_bos_.bos.wTotalLength = static_cast<uint16_t>(sizeof(winusb_bos_));
  winusb_bos_.bos.bNumDeviceCaps = 1;
  // --- Initialize the constant fields of the capability descriptor ---
  winusb_bos_.cap.bLength = sizeof(winusb_bos_.cap);
  winusb_bos_.cap.bDescriptorType = 0x10;     // DEVICE_CAPABILITY
  winusb_bos_.cap.bDevCapabilityType = 0x05;  // PLATFORM
  std::memcpy(winusb_bos_.cap.PlatformCapabilityUUID,
              LibXR::USB::WinUsbMsOs20::kMsOs20PlatformCapabilityUuid, 16);
  winusb_bos_.cap.dwWindowsVersion = 0x06030000;
  winusb_bos_.cap.wMSOSDescriptorSetTotalLength =
      static_cast<uint16_t>(sizeof(winusb_msos20_));
  winusb_bos_.cap.bMS_VendorCode = kWinUsbVendorCode;
  winusb_bos_.cap.bAltEnumCode = 0x00;

  // --- MS OS 2.0 descriptor set ---
  // Set Header
  winusb_msos20_.set.wLength = sizeof(winusb_msos20_.set);
  winusb_msos20_.set.wDescriptorType =
      LibXR::USB::WinUsbMsOs20::MS_OS_20_SET_HEADER_DESCRIPTOR;
  winusb_msos20_.set.dwWindowsVersion = 0x06030000;
  winusb_msos20_.set.wTotalLength = static_cast<uint16_t>(sizeof(winusb_msos20_));

  // Configuration Subset Header
  winusb_msos20_.cfg.wLength = sizeof(winusb_msos20_.cfg);
  winusb_msos20_.cfg.wDescriptorType =
      LibXR::USB::WinUsbMsOs20::MS_OS_20_SUBSET_HEADER_CONFIGURATION;
  winusb_msos20_.cfg.bConfigurationValue = 0;  // Assuming config 1
  winusb_msos20_.cfg.wTotalLength =
      static_cast<uint16_t>(sizeof(winusb_msos20_) - offsetof(WinUsbMsOs20DescSet, cfg));

  // Function Subset Header
  winusb_msos20_.func.wLength = sizeof(winusb_msos20_.func);
  winusb_msos20_.func.wDescriptorType =
      LibXR::USB::WinUsbMsOs20::MS_OS_20_SUBSET_HEADER_FUNCTION;
  // bFirstInterface will be patched in UpdateWinUsbInterfaceFields_
  winusb_msos20_.func.wTotalLength =
      static_cast<uint16_t>(sizeof(winusb_msos20_) - offsetof(WinUsbMsOs20DescSet, func));

  // Compatible ID Feature
  winusb_msos20_.compat.wLength = sizeof(winusb_msos20_.compat);
  winusb_msos20_.compat.wDescriptorType =
      LibXR::USB::WinUsbMsOs20::MS_OS_20_FEATURE_COMPATIBLE_ID;
  // "WINUSB" is the default value from the struct definition, no need to copy.

  // Registry Property Feature
  winusb_msos20_.prop.header.wLength = static_cast<uint16_t>(sizeof(winusb_msos20_.prop));
  winusb_msos20_.prop.header.wDescriptorType =
      LibXR::USB::WinUsbMsOs20::MS_OS_20_FEATURE_REG_PROPERTY;
  winusb_msos20_.prop.header.wPropertyDataType = LibXR::USB::WinUsbMsOs20::REG_MULTI_SZ;
  winusb_msos20_.prop.header.wPropertyNameLength =
      LibXR::USB::WinUsbMsOs20::kPropName_DeviceInterfaceGUIDs_Bytes;
  std::memcpy(winusb_msos20_.prop.name,
              LibXR::USB::WinUsbMsOs20::kPropName_DeviceInterfaceGUIDs_Utf16,
              LibXR::USB::WinUsbMsOs20::kPropName_DeviceInterfaceGUIDs_Bytes);

  // --- Correctly build the GUID string here ---
  const char guid_str[] = "{CDB3B5AD-293B-4663-AA36-1AAE46463776}";
  const size_t guid_len = sizeof(guid_str) - 1;  // exclude NUL terminator
  for (size_t i = 0; i < guid_len; ++i)
  {
    winusb_msos20_.prop.data[i * 2] = guid_str[i];
    winusb_msos20_.prop.data[i * 2 + 1] = 0x00;
  }
  // Add the two required NUL terminators for REG_MULTI_SZ
  winusb_msos20_.prop.data[guid_len * 2] = 0x00;
  winusb_msos20_.prop.data[guid_len * 2 + 1] = 0x00;
  winusb_msos20_.prop.data[guid_len * 2 + 2] = 0x00;
  winusb_msos20_.prop.data[guid_len * 2 + 3] = 0x00;

  // Set the final data length: 36 chars * 2 bytes/char + 4 bytes for two NULs
  winusb_msos20_.prop.wPropertyDataLength = static_cast<uint16_t>((guid_len * 2) + 4);
}

void DapLinkV2Class::UpdateWinUsbInterfaceFields_()
{
  // This function should ONLY update fields that change at runtime.
  // All constant data is now initialized in InitWinUsbDescriptors_().

  // Patch the Function Subset to use the correct starting interface number.
  winusb_msos20_.func.bFirstInterface = interface_num_;
}

ConstRawData DapLinkV2Class::GetWinUSB20Descriptor()
{
  return ConstRawData{reinterpret_cast<const uint8_t*>(&winusb_msos20_),
                      sizeof(winusb_msos20_)};
}

ConstRawData DapLinkV2Class::GetWinUSBBOSDescriptor()
{
  return ConstRawData{reinterpret_cast<const uint8_t*>(&winusb_bos_),
                      sizeof(winusb_bos_)};
}

uint8_t DapLinkV2Class::GetWinUSBVendorCode() { return kWinUsbVendorCode; }

// ============================================================================
// Ctor / public APIs
// ============================================================================
DapLinkV2Class::DapLinkV2Class(LibXR::Debug::Swd& swd_link, LibXR::GPIO* nreset_gpio,
                               Endpoint::EPNumber data_in_ep_num,
                               Endpoint::EPNumber data_out_ep_num)
    : swd_(swd_link),
      nreset_gpio_(nreset_gpio),
      data_in_ep_num_(data_in_ep_num),
      data_out_ep_num_(data_out_ep_num)
{
  swj_clock_hz_ = 1'000'000u;
  (void)swd_.SetClockHz(swj_clock_hz_);

  // Init WinUSB descriptor templates (constant parts)
  InitWinUsbDescriptors_();
}

void DapLinkV2Class::SetInfoStrings(const InfoStrings& info) { info_ = info; }

const LibXR::USB::DapLinkV2Def::State& DapLinkV2Class::GetState() const
{
  return dap_state_;
}

bool DapLinkV2Class::IsInited() const { return inited_; }

// ============================================================================
// DeviceClass overrides (aligned to GsUsbClass style)
// ============================================================================
size_t DapLinkV2Class::GetInterfaceNum() { return 1; }

bool DapLinkV2Class::HasIAD() { return false; }

bool DapLinkV2Class::OwnsEndpoint(uint8_t ep_addr) const
{
  if (!inited_)
  {
    return false;
  }
  return (ep_data_in_ && ep_addr == ep_data_in_->GetAddress()) ||
         (ep_data_out_ && ep_addr == ep_data_out_->GetAddress());
}

size_t DapLinkV2Class::GetMaxConfigSize() { return sizeof(desc_block_); }

void DapLinkV2Class::Init(EndpointPool& endpoint_pool, uint8_t start_itf_num)
{
  inited_ = false;
  tx_busy_ = false;

  interface_num_ = start_itf_num;

  // Patch WinUSB function subset to match this interface number
  UpdateWinUsbInterfaceFields_();

  // Allocate endpoints (use pool final capability)
  auto ans = endpoint_pool.Get(ep_data_out_, Endpoint::Direction::OUT, data_out_ep_num_);
  ASSERT(ans == ErrorCode::OK);

  ans = endpoint_pool.Get(ep_data_in_, Endpoint::Direction::IN, data_in_ep_num_);
  ASSERT(ans == ErrorCode::OK);

  // Configure endpoints
  // - Use upper bound; USB core will pick an available max packet size <= this limit.
  // - Keep double_buffer=false to preserve strict request/response sequencing.
  ep_data_out_->Configure(
      {Endpoint::Direction::OUT, Endpoint::Type::BULK, UINT16_MAX, false});
  ep_data_in_->Configure(
      {Endpoint::Direction::IN, Endpoint::Type::BULK, UINT16_MAX, false});

  // Hook callbacks
  ep_data_out_->SetOnTransferCompleteCallback(on_data_out_cb_);
  ep_data_in_->SetOnTransferCompleteCallback(on_data_in_cb_);

  // Descriptor block
  desc_block_.intf = {9,
                      static_cast<uint8_t>(DescriptorType::INTERFACE),
                      interface_num_,
                      0,
                      2,
                      0xFF,  // vendor specific
                      0x00,
                      0x00,
                      0};

  desc_block_.ep_out = {7,
                        static_cast<uint8_t>(DescriptorType::ENDPOINT),
                        static_cast<uint8_t>(ep_data_out_->GetAddress()),
                        static_cast<uint8_t>(Endpoint::Type::BULK),
                        ep_data_out_->MaxPacketSize(),
                        0};

  desc_block_.ep_in = {7,
                       static_cast<uint8_t>(DescriptorType::ENDPOINT),
                       static_cast<uint8_t>(ep_data_in_->GetAddress()),
                       static_cast<uint8_t>(Endpoint::Type::BULK),
                       ep_data_in_->MaxPacketSize(),
                       0};

  SetData(RawData{reinterpret_cast<uint8_t*>(&desc_block_), sizeof(desc_block_)});

  // Runtime defaults
  dap_state_ = {};
  dap_state_.debug_port = LibXR::USB::DapLinkV2Def::DebugPort::DISABLED;
  dap_state_.transfer_abort = false;

  swj_clock_hz_ = 1'000'000u;
  (void)swd_.SetClockHz(swj_clock_hz_);

  inited_ = true;
  ArmOutTransferIfIdle();
}

void DapLinkV2Class::Deinit(EndpointPool& endpoint_pool)
{
  inited_ = false;
  tx_busy_ = false;

  dap_state_.debug_port = LibXR::USB::DapLinkV2Def::DebugPort::DISABLED;
  dap_state_.transfer_abort = false;

  if (ep_data_in_)
  {
    ep_data_in_->Close();
    ep_data_in_->SetActiveLength(0);
    endpoint_pool.Release(ep_data_in_);
    ep_data_in_ = nullptr;
  }

  if (ep_data_out_)
  {
    ep_data_out_->Close();
    ep_data_out_->SetActiveLength(0);
    endpoint_pool.Release(ep_data_out_);
    ep_data_out_ = nullptr;
  }

  swd_.Close();
}

// ============================================================================
// USB callbacks
// ============================================================================
void DapLinkV2Class::OnDataOutCompleteStatic(bool in_isr, DapLinkV2Class* self,
                                             LibXR::ConstRawData& data)
{
  if (self && self->inited_)
  {
    self->OnDataOutComplete(in_isr, data);
  }
}

void DapLinkV2Class::OnDataInCompleteStatic(bool in_isr, DapLinkV2Class* self,
                                            LibXR::ConstRawData& data)
{
  if (self && self->inited_)
  {
    self->OnDataInComplete(in_isr, data);
  }
}

void DapLinkV2Class::OnDataOutComplete(bool in_isr, LibXR::ConstRawData& data)
{
  (void)in_isr;

  if (!inited_ || !ep_data_in_ || !ep_data_out_)
  {
    return;
  }

  // With OUT not double-buffered and only armed when idle, tx_busy_ should rarely
  // be hit; keep guard for safety.
  if (tx_busy_)
  {
    return;
  }

  const auto* req = static_cast<const uint8_t*>(data.addr_);
  const uint16_t req_len = static_cast<uint16_t>(data.size_);

  // Empty packet -> keep receiving
  if (!req || req_len == 0u)
  {
    ArmOutTransferIfIdle();
    return;
  }

  uint16_t out_len = 0;
  (void)ProcessOneCommand(in_isr, req, req_len, tx_buf_, kMaxResp, out_len);

  tx_busy_ = true;
  LibXR::RawData tx(tx_buf_, out_len);

  if (ep_data_in_->TransferMultiBulk(tx) != ErrorCode::OK)
  {
    tx_busy_ = false;
    ArmOutTransferIfIdle();
  }
}

void DapLinkV2Class::OnDataInComplete(bool /*in_isr*/, LibXR::ConstRawData& /*data*/)
{
  tx_busy_ = false;
  ArmOutTransferIfIdle();
}

// ============================================================================
// Arm OUT
// ============================================================================
void DapLinkV2Class::ArmOutTransferIfIdle()
{
  if (!inited_ || tx_busy_ || !ep_data_out_)
  {
    return;
  }

  if (ep_data_out_->GetState() != Endpoint::State::IDLE)
  {
    return;
  }

  LibXR::RawData rx(rx_buf_, kMaxReq);
  (void)ep_data_out_->TransferMultiBulk(rx);
}

// ============================================================================
// Command dispatch
// ============================================================================
ErrorCode DapLinkV2Class::ProcessOneCommand(bool in_isr, const uint8_t* req,
                                            uint16_t req_len, uint8_t* resp,
                                            uint16_t resp_cap, uint16_t& out_len)
{
  out_len = 0;

  if (!req || !resp || req_len < 1u || resp_cap < 2u)
  {
    if (resp && resp_cap >= 2u)
    {
      BuildNotSupportResponse(0xFFu, resp, resp_cap, out_len);
    }
    return ErrorCode::ARG_ERR;
  }

  const uint8_t cmd = req[0];

  switch (cmd)
  {
    case U8(LibXR::USB::DapLinkV2Def::CommandId::Info):
      return Handle_Info(in_isr, req, req_len, resp, resp_cap, out_len);
    case U8(LibXR::USB::DapLinkV2Def::CommandId::HostStatus):
      return Handle_HostStatus(in_isr, req, req_len, resp, resp_cap, out_len);
    case U8(LibXR::USB::DapLinkV2Def::CommandId::Connect):
      return Handle_Connect(in_isr, req, req_len, resp, resp_cap, out_len);
    case U8(LibXR::USB::DapLinkV2Def::CommandId::Disconnect):
      return Handle_Disconnect(in_isr, req, req_len, resp, resp_cap, out_len);
    case U8(LibXR::USB::DapLinkV2Def::CommandId::TransferConfigure):
      return Handle_TransferConfigure(in_isr, req, req_len, resp, resp_cap, out_len);
    case U8(LibXR::USB::DapLinkV2Def::CommandId::Transfer):
      return Handle_Transfer(in_isr, req, req_len, resp, resp_cap, out_len);
    case U8(LibXR::USB::DapLinkV2Def::CommandId::TransferBlock):
      return Handle_TransferBlock(in_isr, req, req_len, resp, resp_cap, out_len);
    case U8(LibXR::USB::DapLinkV2Def::CommandId::TransferAbort):
      return Handle_TransferAbort(in_isr, req, req_len, resp, resp_cap, out_len);
    case U8(LibXR::USB::DapLinkV2Def::CommandId::WriteABORT):
      return Handle_WriteABORT(in_isr, req, req_len, resp, resp_cap, out_len);
    case U8(LibXR::USB::DapLinkV2Def::CommandId::Delay):
      return Handle_Delay(in_isr, req, req_len, resp, resp_cap, out_len);
    case U8(LibXR::USB::DapLinkV2Def::CommandId::ResetTarget):
      return Handle_ResetTarget(in_isr, req, req_len, resp, resp_cap, out_len);

    case U8(LibXR::USB::DapLinkV2Def::CommandId::SWJ_Pins):
      return Handle_SWJ_Pins(in_isr, req, req_len, resp, resp_cap, out_len);
    case U8(LibXR::USB::DapLinkV2Def::CommandId::SWJ_Clock):
      return Handle_SWJ_Clock(in_isr, req, req_len, resp, resp_cap, out_len);
    case U8(LibXR::USB::DapLinkV2Def::CommandId::SWJ_Sequence):
      return Handle_SWJ_Sequence(in_isr, req, req_len, resp, resp_cap, out_len);
    case U8(LibXR::USB::DapLinkV2Def::CommandId::SWD_Configure):
      return Handle_SWD_Configure(in_isr, req, req_len, resp, resp_cap, out_len);
    case U8(LibXR::USB::DapLinkV2Def::CommandId::SWD_Sequence):
      return Handle_SWD_Sequence(in_isr, req, req_len, resp, resp_cap, out_len);

    case U8(LibXR::USB::DapLinkV2Def::CommandId::QueueCommands):
      return Handle_QueueCommands(in_isr, req, req_len, resp, resp_cap, out_len);
    case U8(LibXR::USB::DapLinkV2Def::CommandId::ExecuteCommands):
      return Handle_ExecuteCommands(in_isr, req, req_len, resp, resp_cap, out_len);

    default:
      BuildNotSupportResponse(cmd, resp, resp_cap, out_len);
      return ErrorCode::NOT_SUPPORT;
  }
}

void DapLinkV2Class::BuildNotSupportResponse(uint8_t cmd, uint8_t* resp,
                                             uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0;
    return;
  }
  resp[0] = cmd;
  resp[1] = U8(LibXR::USB::DapLinkV2Def::Status::Error);
  out_len = 2u;
}

// ============================================================================
// DAP_Info
// ============================================================================
ErrorCode DapLinkV2Class::Handle_Info(bool /*in_isr*/, const uint8_t* req,
                                      uint16_t req_len, uint8_t* resp, uint16_t resp_cap,
                                      uint16_t& out_len)
{
  if (req_len < 2u)
  {
    resp[0] = U8(LibXR::USB::DapLinkV2Def::CommandId::Info);
    resp[1] = 0u;
    out_len = 2u;
    return ErrorCode::ARG_ERR;
  }

  const uint8_t info_id = req[1];

  resp[0] = U8(LibXR::USB::DapLinkV2Def::CommandId::Info);

  switch (info_id)
  {
    case U8(LibXR::USB::DapLinkV2Def::InfoId::Vendor):
      return BuildInfoStringResponse(resp[0], info_.vendor, resp, resp_cap, out_len);
    case U8(LibXR::USB::DapLinkV2Def::InfoId::Product):
      return BuildInfoStringResponse(resp[0], info_.product, resp, resp_cap, out_len);
    case U8(LibXR::USB::DapLinkV2Def::InfoId::SerialNumber):
      return BuildInfoStringResponse(resp[0], info_.serial, resp, resp_cap, out_len);
    case U8(LibXR::USB::DapLinkV2Def::InfoId::FirmwareVersion):
      return BuildInfoStringResponse(resp[0], info_.firmware_ver, resp, resp_cap,
                                     out_len);

    case U8(LibXR::USB::DapLinkV2Def::InfoId::DeviceVendor):
      return BuildInfoStringResponse(resp[0], info_.device_vendor, resp, resp_cap,
                                     out_len);
    case U8(LibXR::USB::DapLinkV2Def::InfoId::DeviceName):
      return BuildInfoStringResponse(resp[0], info_.device_name, resp, resp_cap, out_len);
    case U8(LibXR::USB::DapLinkV2Def::InfoId::BoardVendor):
      return BuildInfoStringResponse(resp[0], info_.board_vendor, resp, resp_cap,
                                     out_len);
    case U8(LibXR::USB::DapLinkV2Def::InfoId::BoardName):
      return BuildInfoStringResponse(resp[0], info_.board_name, resp, resp_cap, out_len);
    case U8(LibXR::USB::DapLinkV2Def::InfoId::ProductFirmwareVersion):
      return BuildInfoStringResponse(resp[0], info_.product_fw_ver, resp, resp_cap,
                                     out_len);

    case U8(LibXR::USB::DapLinkV2Def::InfoId::Capabilities):
      return BuildInfoU8Response(resp[0], LibXR::USB::DapLinkV2Def::DAP_CAP_SWD, resp,
                                 resp_cap, out_len);
    case U8(LibXR::USB::DapLinkV2Def::InfoId::PacketCount):
      return BuildInfoU8Response(resp[0], 1, resp, resp_cap, out_len);
    case U8(LibXR::USB::DapLinkV2Def::InfoId::PacketSize):
      return BuildInfoU16Response(resp[0], ep_data_in_ ? ep_data_in_->MaxPacketSize() : 0,
                                  resp, resp_cap, out_len);
    case U8(LibXR::USB::DapLinkV2Def::InfoId::TimestampClock):
      return BuildInfoU32Response(resp[0], 1000000U, resp, resp_cap, out_len);

    default:
      resp[1] = 0u;
      out_len = 2u;
      return ErrorCode::OK;
  }
}

ErrorCode DapLinkV2Class::BuildInfoStringResponse(uint8_t cmd, const char* str,
                                                  uint8_t* resp, uint16_t resp_cap,
                                                  uint16_t& out_len)
{
  resp[0] = cmd;
  resp[1] = 0u;

  if (!str)
  {
    out_len = 2u;
    return ErrorCode::OK;
  }

  const size_t n = std::strlen(str);
  const size_t max_payload = (resp_cap >= 2u) ? (resp_cap - 2u) : 0u;
  const size_t copy_n = (n > max_payload) ? max_payload : n;

  resp[1] = static_cast<uint8_t>(copy_n);
  if (copy_n != 0u)
  {
    std::memcpy(&resp[2], str, copy_n);
  }

  out_len = static_cast<uint16_t>(2u + copy_n);
  return ErrorCode::OK;
}

ErrorCode DapLinkV2Class::BuildInfoU8Response(uint8_t cmd, uint8_t val, uint8_t* resp,
                                              uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 3u)
  {
    out_len = 0;
    return ErrorCode::NOT_FOUND;
  }
  resp[0] = cmd;
  resp[1] = 1u;
  resp[2] = val;
  out_len = 3u;
  return ErrorCode::OK;
}

ErrorCode DapLinkV2Class::BuildInfoU16Response(uint8_t cmd, uint16_t val, uint8_t* resp,
                                               uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 4u)
  {
    out_len = 0;
    return ErrorCode::NOT_FOUND;
  }
  resp[0] = cmd;
  resp[1] = 2u;
  StoreLE16(&resp[2], val);
  out_len = 4u;
  return ErrorCode::OK;
}

ErrorCode DapLinkV2Class::BuildInfoU32Response(uint8_t cmd, uint32_t val, uint8_t* resp,
                                               uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 6u)
  {
    out_len = 0;
    return ErrorCode::NOT_FOUND;
  }
  resp[0] = cmd;
  resp[1] = 4u;
  StoreLE32(&resp[2], val);
  out_len = 6u;
  return ErrorCode::OK;
}

// ============================================================================
// Simple control handlers
// ============================================================================
ErrorCode DapLinkV2Class::Handle_HostStatus(bool /*in_isr*/, const uint8_t* /*req*/,
                                            uint16_t /*req_len*/, uint8_t* resp,
                                            uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0;
    return ErrorCode::NOT_FOUND;
  }
  resp[0] = U8(LibXR::USB::DapLinkV2Def::CommandId::HostStatus);
  resp[1] = U8(LibXR::USB::DapLinkV2Def::Status::OK);
  out_len = 2u;
  return ErrorCode::OK;
}

ErrorCode DapLinkV2Class::Handle_Connect(bool /*in_isr*/, const uint8_t* req,
                                         uint16_t req_len, uint8_t* resp,
                                         uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0;
    return ErrorCode::NOT_FOUND;
  }

  resp[0] = U8(LibXR::USB::DapLinkV2Def::CommandId::Connect);

  uint8_t port = 0u;
  if (req_len >= 2u)
  {
    port = req[1];
  }

  // SWD-only
  if (port == 0u || port == U8(LibXR::USB::DapLinkV2Def::Port::SWD))
  {
    (void)swd_.EnterSwd();
    (void)swd_.SetClockHz(swj_clock_hz_);

    dap_state_.debug_port = LibXR::USB::DapLinkV2Def::DebugPort::SWD;
    dap_state_.transfer_abort = false;

    resp[1] = U8(LibXR::USB::DapLinkV2Def::Port::SWD);
  }
  else
  {
    resp[1] = U8(LibXR::USB::DapLinkV2Def::Port::Disabled);
  }

  out_len = 2u;
  return ErrorCode::OK;
}

ErrorCode DapLinkV2Class::Handle_Disconnect(bool /*in_isr*/, const uint8_t* /*req*/,
                                            uint16_t /*req_len*/, uint8_t* resp,
                                            uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0;
    return ErrorCode::NOT_FOUND;
  }

  swd_.Close();
  dap_state_.debug_port = LibXR::USB::DapLinkV2Def::DebugPort::DISABLED;
  dap_state_.transfer_abort = false;

  resp[0] = U8(LibXR::USB::DapLinkV2Def::CommandId::Disconnect);
  resp[1] = U8(LibXR::USB::DapLinkV2Def::Status::OK);
  out_len = 2u;
  return ErrorCode::OK;
}

ErrorCode DapLinkV2Class::Handle_TransferConfigure(bool /*in_isr*/, const uint8_t* req,
                                                   uint16_t req_len, uint8_t* resp,
                                                   uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0;
    return ErrorCode::NOT_FOUND;
  }

  resp[0] = U8(LibXR::USB::DapLinkV2Def::CommandId::TransferConfigure);

  // Req: [0]=0x04 [1]=idle_cycles [2..3]=wait_retry [4..5]=match_retry
  if (req_len < 6u)
  {
    resp[1] = U8(LibXR::USB::DapLinkV2Def::Status::Error);
    out_len = 2u;
    return ErrorCode::ARG_ERR;
  }

  const uint8_t idle = req[1];
  const uint16_t wait_retry = LoadLE16(&req[2]);
  const uint16_t match_retry = LoadLE16(&req[4]);

  dap_state_.transfer_cfg.idle_cycles = idle;
  dap_state_.transfer_cfg.retry_count = wait_retry;
  dap_state_.transfer_cfg.match_retry = match_retry;

  // map to SWD transaction policy
  LibXR::Debug::Swd::TransferPolicy pol = swd_.GetTransferPolicy();
  pol.idle_cycles = idle;
  pol.wait_retry = wait_retry;
  swd_.SetTransferPolicy(pol);

  resp[1] = U8(LibXR::USB::DapLinkV2Def::Status::OK);
  out_len = 2u;
  return ErrorCode::OK;
}

ErrorCode DapLinkV2Class::Handle_TransferAbort(bool /*in_isr*/, const uint8_t* /*req*/,
                                               uint16_t /*req_len*/, uint8_t* resp,
                                               uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0;
    return ErrorCode::NOT_FOUND;
  }

  SetTransferAbortFlag(true);

  resp[0] = U8(LibXR::USB::DapLinkV2Def::CommandId::TransferAbort);
  resp[1] = U8(LibXR::USB::DapLinkV2Def::Status::OK);
  out_len = 2u;
  return ErrorCode::OK;
}

ErrorCode DapLinkV2Class::Handle_WriteABORT(bool /*in_isr*/, const uint8_t* req,
                                            uint16_t req_len, uint8_t* resp,
                                            uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0;
    return ErrorCode::NOT_FOUND;
  }

  resp[0] = U8(LibXR::USB::DapLinkV2Def::CommandId::WriteABORT);

  if (req_len < 5u)
  {
    resp[1] = U8(LibXR::USB::DapLinkV2Def::Status::Error);
    out_len = 2u;
    return ErrorCode::ARG_ERR;
  }

  const uint32_t flags = LoadLE32(&req[1]);
  LibXR::Debug::Swd::Ack ack = LibXR::Debug::Swd::Ack::PROTOCOL;

  const ErrorCode ec = swd_.WriteAbortTxn(flags, ack);
  resp[1] = (ec == ErrorCode::OK && ack == LibXR::Debug::Swd::Ack::OK)
                ? U8(LibXR::USB::DapLinkV2Def::Status::OK)
                : U8(LibXR::USB::DapLinkV2Def::Status::Error);

  out_len = 2u;
  return ErrorCode::OK;
}

ErrorCode DapLinkV2Class::Handle_Delay(bool /*in_isr*/, const uint8_t* req,
                                       uint16_t req_len, uint8_t* resp, uint16_t resp_cap,
                                       uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0;
    return ErrorCode::NOT_FOUND;
  }

  resp[0] = U8(LibXR::USB::DapLinkV2Def::CommandId::Delay);

  if (req_len < 3u)
  {
    resp[1] = U8(LibXR::USB::DapLinkV2Def::Status::Error);
    out_len = 2u;
    return ErrorCode::ARG_ERR;
  }

  const uint16_t us = LoadLE16(&req[1]);
  BusyWaitUs(us);

  resp[1] = U8(LibXR::USB::DapLinkV2Def::Status::OK);
  out_len = 2u;
  return ErrorCode::OK;
}

ErrorCode DapLinkV2Class::Handle_ResetTarget(bool in_isr, const uint8_t* /*req*/,
                                             uint16_t /*req_len*/, uint8_t* resp,
                                             uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0;
    return ErrorCode::NOT_FOUND;
  }

  resp[0] = U8(LibXR::USB::DapLinkV2Def::CommandId::ResetTarget);

  if (nreset_gpio_ == nullptr)
  {
    resp[1] = 0u;  // not available
    out_len = 2u;
    return ErrorCode::OK;
  }

  DriveReset(false);
  DelayUsIfAllowed(in_isr, 1000u);
  DriveReset(true);
  DelayUsIfAllowed(in_isr, 1000u);

  resp[1] = 1u;
  out_len = 2u;
  return ErrorCode::OK;
}

// ============================================================================
// SWJ / SWD handlers
// ============================================================================
ErrorCode DapLinkV2Class::Handle_SWJ_Pins(bool /*in_isr*/, const uint8_t* req,
                                          uint16_t req_len, uint8_t* resp,
                                          uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0;
    return ErrorCode::NOT_FOUND;
  }

  resp[0] = U8(LibXR::USB::DapLinkV2Def::CommandId::SWJ_Pins);

  // Req: [0]=0x10 [1]=PinOut [2]=PinSelect [3..6]=PinWait(us)
  if (req_len < 7u)
  {
    resp[1] = 0u;
    out_len = 2u;
    return ErrorCode::ARG_ERR;
  }

  const uint8_t pin_out = req[1];
  const uint8_t pin_sel = req[2];
  const uint32_t wait_us = LoadLE32(&req[3]);

  // Only nRESET
  if ((pin_sel & LibXR::USB::DapLinkV2Def::DAP_SWJ_nRESET) != 0u &&
      nreset_gpio_ != nullptr)
  {
    const bool level_high = ((pin_out & LibXR::USB::DapLinkV2Def::DAP_SWJ_nRESET) != 0u);
    DriveReset(level_high);
  }

  BusyWaitUs(wait_us);

  uint8_t pin_in = 0u;
  if (nreset_gpio_ != nullptr)
  {
    if (nreset_gpio_->Read())
    {
      pin_in |= LibXR::USB::DapLinkV2Def::DAP_SWJ_nRESET;
    }
  }
  else
  {
    if (last_nreset_level_high_)
    {
      pin_in |= LibXR::USB::DapLinkV2Def::DAP_SWJ_nRESET;
    }
  }

  resp[1] = pin_in;
  out_len = 2u;
  return ErrorCode::OK;
}

ErrorCode DapLinkV2Class::Handle_SWJ_Clock(bool /*in_isr*/, const uint8_t* req,
                                           uint16_t req_len, uint8_t* resp,
                                           uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0;
    return ErrorCode::NOT_FOUND;
  }

  resp[0] = U8(LibXR::USB::DapLinkV2Def::CommandId::SWJ_Clock);

  if (req_len < 5u)
  {
    resp[1] = U8(LibXR::USB::DapLinkV2Def::Status::Error);
    out_len = 2u;
    return ErrorCode::ARG_ERR;
  }

  const uint32_t hz = LoadLE32(&req[1]);
  swj_clock_hz_ = hz;
  (void)swd_.SetClockHz(hz);

  resp[1] = U8(LibXR::USB::DapLinkV2Def::Status::OK);
  out_len = 2u;
  return ErrorCode::OK;
}

static bool BitsAllOnes(const uint8_t* data, uint32_t bit_count)
{
  const uint32_t byte_count = (bit_count + 7u) / 8u;
  for (uint32_t i = 0; i < byte_count; ++i)
  {
    const uint8_t b = data[i];
    if (i + 1u < byte_count)
    {
      if (b != 0xFFu)
      {
        return false;
      }
    }
    else
    {
      const uint32_t rem = bit_count & 7u;
      if (rem == 0u)
      {
        if (b != 0xFFu)
        {
          return false;
        }
      }
      else
      {
        const uint8_t mask = static_cast<uint8_t>((1u << rem) - 1u);
        if ((b & mask) != mask)
        {
          return false;
        }
      }
    }
  }
  return true;
}

ErrorCode DapLinkV2Class::Handle_SWJ_Sequence(bool /*in_isr*/, const uint8_t* req,
                                              uint16_t req_len, uint8_t* resp,
                                              uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0;
    return ErrorCode::NOT_FOUND;
  }

  resp[0] = U8(LibXR::USB::DapLinkV2Def::CommandId::SWJ_Sequence);

  // Req: [0]=0x12 [1]=bit_count [2..]=data
  if (req_len < 2u)
  {
    resp[1] = U8(LibXR::USB::DapLinkV2Def::Status::Error);
    out_len = 2u;
    return ErrorCode::ARG_ERR;
  }

  const uint32_t bit_count = req[1];
  const uint32_t bytes = (bit_count + 7u) / 8u;

  if (2u + bytes > req_len)
  {
    resp[1] = U8(LibXR::USB::DapLinkV2Def::Status::Error);
    out_len = 2u;
    return ErrorCode::ARG_ERR;
  }

  const uint8_t* data = &req[2];

  // Map: line reset (>=50 ones)
  if (bit_count >= 50u && BitsAllOnes(data, bit_count))
  {
    (void)swd_.LineReset();
  }
  else
  {
    // Map: detect 0x9E 0xE7 (common LSB-first representation of 0xE79E)
    for (uint32_t i = 0; i + 1u < bytes; ++i)
    {
      if (data[i] == 0x9Eu && data[i + 1u] == 0xE7u)
      {
        (void)swd_.EnterSwd();
        break;
      }
    }
  }

  resp[1] = U8(LibXR::USB::DapLinkV2Def::Status::OK);
  out_len = 2u;
  return ErrorCode::OK;
}

ErrorCode DapLinkV2Class::Handle_SWD_Configure(bool /*in_isr*/, const uint8_t* /*req*/,
                                               uint16_t /*req_len*/, uint8_t* resp,
                                               uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0;
    return ErrorCode::NOT_FOUND;
  }

  resp[0] = U8(LibXR::USB::DapLinkV2Def::CommandId::SWD_Configure);

  // Best-effort parse (optional). Keep compatibility by returning OK.
  resp[1] = U8(LibXR::USB::DapLinkV2Def::Status::OK);
  out_len = 2u;
  return ErrorCode::OK;
}

ErrorCode DapLinkV2Class::Handle_SWD_Sequence(bool /*in_isr*/, const uint8_t* /*req*/,
                                              uint16_t /*req_len*/, uint8_t* resp,
                                              uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0;
    return ErrorCode::NOT_FOUND;
  }
  resp[0] = U8(LibXR::USB::DapLinkV2Def::CommandId::SWD_Sequence);
  resp[1] = U8(LibXR::USB::DapLinkV2Def::Status::Error);
  out_len = 2u;
  return ErrorCode::NOT_SUPPORT;
}

ErrorCode DapLinkV2Class::Handle_QueueCommands(bool /*in_isr*/, const uint8_t* /*req*/,
                                               uint16_t /*req_len*/, uint8_t* resp,
                                               uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0;
    return ErrorCode::NOT_FOUND;
  }
  resp[0] = U8(LibXR::USB::DapLinkV2Def::CommandId::QueueCommands);
  resp[1] = U8(LibXR::USB::DapLinkV2Def::Status::Error);
  out_len = 2u;
  return ErrorCode::NOT_SUPPORT;
}

ErrorCode DapLinkV2Class::Handle_ExecuteCommands(bool /*in_isr*/, const uint8_t* /*req*/,
                                                 uint16_t /*req_len*/, uint8_t* resp,
                                                 uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0;
    return ErrorCode::NOT_FOUND;
  }
  resp[0] = U8(LibXR::USB::DapLinkV2Def::CommandId::ExecuteCommands);
  resp[1] = U8(LibXR::USB::DapLinkV2Def::Status::Error);
  out_len = 2u;
  return ErrorCode::NOT_SUPPORT;
}

// ============================================================================
// Transfer helpers
// ============================================================================
uint8_t DapLinkV2Class::MapAckToDapResp(LibXR::Debug::Swd::Ack ack) const
{
  switch (ack)
  {
    case LibXR::Debug::Swd::Ack::OK:
      return LibXR::USB::DapLinkV2Def::DAP_TRANSFER_OK;
    case LibXR::Debug::Swd::Ack::WAIT:
      return LibXR::USB::DapLinkV2Def::DAP_TRANSFER_WAIT;
    case LibXR::Debug::Swd::Ack::FAULT:
      return LibXR::USB::DapLinkV2Def::DAP_TRANSFER_FAULT;
    default:
      return 0u;  // PROTOCOL/NO_ACK -> 0
  }
}

void DapLinkV2Class::SetTransferAbortFlag(bool on) { dap_state_.transfer_abort = on; }

// ============================================================================
// DAP_Transfer / DAP_TransferBlock
// ============================================================================
ErrorCode DapLinkV2Class::Handle_Transfer(bool /*in_isr*/, const uint8_t* req,
                                          uint16_t req_len, uint8_t* resp,
                                          uint16_t resp_cap, uint16_t& out_len)
{
  // Req:  [0]=0x05 [1]=index [2]=count [3..]= (req, [data])...
  // Resp: [0]=0x05 [1]=done  [2]=resp  [3..]= (ts?, data?)...
  if (resp_cap < 3u)
  {
    out_len = 0;
    return ErrorCode::NOT_FOUND;
  }

  resp[0] = U8(LibXR::USB::DapLinkV2Def::CommandId::Transfer);
  resp[1] = 0u;
  resp[2] = 0u;

  if (req_len < 3u)
  {
    resp[2] = LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
    out_len = 3u;
    return ErrorCode::ARG_ERR;
  }

  // one-shot consume abort flag
  if (dap_state_.transfer_abort)
  {
    dap_state_.transfer_abort = false;
    resp[2] = LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
    out_len = 3u;
    return ErrorCode::OK;
  }

  const uint8_t count = req[2];
  uint16_t req_off = 3u;
  uint16_t resp_off = 3u;

  uint8_t done = 0u;
  uint8_t xresp = 0u;

  for (uint32_t i = 0; i < count; ++i)
  {
    if (req_off >= req_len)
    {
      xresp |= LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
      break;
    }

    const uint8_t dap_rq = req[req_off++];

    if ((dap_rq & (LibXR::USB::DapLinkV2Def::DAP_TRANSFER_MATCH_VALUE |
                   LibXR::USB::DapLinkV2Def::DAP_TRANSFER_MATCH_MASK)) != 0u)
    {
      xresp |= LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
      break;
    }

    const bool ap = LibXR::USB::DapLinkV2Def::ReqIsAp(dap_rq);
    const bool rnw = LibXR::USB::DapLinkV2Def::ReqIsRead(dap_rq);
    const uint8_t addr2b = LibXR::USB::DapLinkV2Def::ReqAddr2b(dap_rq);
    const bool ts = LibXR::USB::DapLinkV2Def::ReqNeedTimestamp(dap_rq);

    LibXR::Debug::Swd::Ack ack = LibXR::Debug::Swd::Ack::PROTOCOL;
    ErrorCode ec = ErrorCode::OK;

    if (!rnw)
    {
      if (req_off + 4u > req_len)
      {
        xresp |= LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
        break;
      }

      const uint32_t wdata = LoadLE32(&req[req_off]);
      req_off = static_cast<uint16_t>(req_off + 4u);

      if (ap)
      {
        ec = swd_.ApWriteTxn(addr2b, wdata, ack);
      }
      else
      {
        ec = swd_.DpWriteTxn(static_cast<LibXR::Debug::Swd::DpWriteReg>(addr2b), wdata,
                             ack);
      }

      xresp = MapAckToDapResp(ack);
      done = static_cast<uint8_t>(i + 1u);

      if (ts)
      {
        if (resp_off + 4u > resp_cap)
        {
          xresp |= LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
          break;
        }
        StoreLE32(&resp[resp_off],
                  static_cast<uint32_t>(LibXR::Timebase::GetMicroseconds()));
        resp_off = static_cast<uint16_t>(resp_off + 4u);
      }

      if (ec != ErrorCode::OK)
      {
        xresp |= LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
        break;
      }
    }
    else
    {
      uint32_t rdata = 0u;

      if (ap)
      {
        ec = swd_.ApReadTxn(addr2b, rdata, ack);
      }
      else
      {
        ec =
            swd_.DpReadTxn(static_cast<LibXR::Debug::Swd::DpReadReg>(addr2b), rdata, ack);
      }

      xresp = MapAckToDapResp(ack);
      done = static_cast<uint8_t>(i + 1u);

      const uint16_t need = static_cast<uint16_t>((ts ? 4u : 0u) + 4u);
      if (resp_off + need > resp_cap)
      {
        xresp |= LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
        break;
      }

      if (ts)
      {
        StoreLE32(&resp[resp_off],
                  static_cast<uint32_t>(LibXR::Timebase::GetMicroseconds()));
        resp_off = static_cast<uint16_t>(resp_off + 4u);
      }

      StoreLE32(&resp[resp_off], rdata);
      resp_off = static_cast<uint16_t>(resp_off + 4u);

      if (ec != ErrorCode::OK)
      {
        xresp |= LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
        break;
      }
    }
  }

  resp[1] = done;
  resp[2] = xresp;
  out_len = resp_off;

  return ErrorCode::OK;
}

ErrorCode DapLinkV2Class::Handle_TransferBlock(bool /*in_isr*/, const uint8_t* req,
                                               uint16_t req_len, uint8_t* resp,
                                               uint16_t resp_cap, uint16_t& out_len)
{
  // Req:  [0]=0x06 [1]=index [2..3]=count [4]=request [5..]=data(write)
  // Resp: [0]=0x06 [1..2]=done [3]=resp [4..]=data(read)
  if (resp_cap < 4u)
  {
    out_len = 0;
    return ErrorCode::NOT_FOUND;
  }

  resp[0] = U8(LibXR::USB::DapLinkV2Def::CommandId::TransferBlock);
  resp[1] = 0u;
  resp[2] = 0u;
  resp[3] = 0u;

  if (req_len < 5u)
  {
    resp[3] = LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
    out_len = 4u;
    return ErrorCode::ARG_ERR;
  }

  if (dap_state_.transfer_abort)
  {
    dap_state_.transfer_abort = false;
    resp[3] = LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
    out_len = 4u;
    return ErrorCode::OK;
  }

  const uint16_t count = LoadLE16(&req[2]);
  const uint8_t dap_rq = req[4];

  if ((dap_rq & (LibXR::USB::DapLinkV2Def::DAP_TRANSFER_MATCH_VALUE |
                 LibXR::USB::DapLinkV2Def::DAP_TRANSFER_MATCH_MASK)) != 0u)
  {
    resp[3] = LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
    out_len = 4u;
    return ErrorCode::NOT_SUPPORT;
  }

  const bool ap = LibXR::USB::DapLinkV2Def::ReqIsAp(dap_rq);
  const bool rnw = LibXR::USB::DapLinkV2Def::ReqIsRead(dap_rq);
  const uint8_t addr2b = LibXR::USB::DapLinkV2Def::ReqAddr2b(dap_rq);

  uint16_t done = 0u;
  uint8_t xresp = 0u;

  uint16_t req_off = 5u;
  uint16_t resp_off = 4u;

  for (uint32_t i = 0; i < count; ++i)
  {
    LibXR::Debug::Swd::Ack ack = LibXR::Debug::Swd::Ack::PROTOCOL;
    ErrorCode ec = ErrorCode::OK;

    if (!rnw)
    {
      if (req_off + 4u > req_len)
      {
        xresp |= LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
        break;
      }

      const uint32_t wdata = LoadLE32(&req[req_off]);
      req_off = static_cast<uint16_t>(req_off + 4u);

      if (ap)
      {
        ec = swd_.ApWriteTxn(addr2b, wdata, ack);
      }
      else
      {
        ec = swd_.DpWriteTxn(static_cast<LibXR::Debug::Swd::DpWriteReg>(addr2b), wdata,
                             ack);
      }

      xresp = MapAckToDapResp(ack);
      done = static_cast<uint16_t>(i + 1u);

      if (ec != ErrorCode::OK)
      {
        xresp |= LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
        break;
      }
    }
    else
    {
      uint32_t rdata = 0u;

      if (ap)
      {
        ec = swd_.ApReadTxn(addr2b, rdata, ack);
      }
      else
      {
        ec =
            swd_.DpReadTxn(static_cast<LibXR::Debug::Swd::DpReadReg>(addr2b), rdata, ack);
      }

      xresp = MapAckToDapResp(ack);
      done = static_cast<uint16_t>(i + 1u);

      if (resp_off + 4u > resp_cap)
      {
        xresp |= LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
        break;
      }

      StoreLE32(&resp[resp_off], rdata);
      resp_off = static_cast<uint16_t>(resp_off + 4u);

      if (ec != ErrorCode::OK)
      {
        xresp |= LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
        break;
      }
    }
  }

  StoreLE16(&resp[1], done);
  resp[3] = xresp;
  out_len = resp_off;

  return ErrorCode::OK;
}

// ============================================================================
// Reset helpers
// ============================================================================
void DapLinkV2Class::DriveReset(bool release)
{
  last_nreset_level_high_ = release;

  if (nreset_gpio_ != nullptr)
  {
    (void)nreset_gpio_->Write(release);
  }
}

void DapLinkV2Class::DelayUsIfAllowed(bool /*in_isr*/, uint32_t us) { BusyWaitUs(us); }

}  // namespace LibXR::USB
