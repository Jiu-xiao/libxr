#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "daplink_v1_def.hpp"
#include "debug/jtag_dp.hpp"
#include "debug/swd.hpp"
#include "gpio.hpp"
#include "hid.hpp"
#include "libxr_def.hpp"
#include "libxr_type.hpp"
#include "timebase.hpp"

namespace LibXR::USB
{

static constexpr uint8_t DAPLINK_V1_REPORT_DESC[] = {
    0x06, 0x00, 0xFF,  // Usage Page (Vendor Defined 0xFF00)
    0x09, 0x01,        // Usage (0x01)
    0xA1, 0x01,        // Collection (Application)
    0x15, 0x00,        // Logical Minimum (0)
    0x26, 0xFF, 0x00,  // Logical Maximum (255)
    0x75, 0x08,        // Report Size (8)
    0x95, 0x40,        // Report Count (64)
    0x09, 0x01,        // Usage (0x01)
    0x81, 0x02,        // Input (Data, Variable, Absolute)
    0x95, 0x40,        // Report Count (64)
    0x09, 0x01,        // Usage (0x01)
    0x91, 0x02,        // Output (Data, Variable, Absolute)
    0x95, 0x40,        // Report Count (64)
    0x09, 0x01,        // Usage (0x01)
    0xB1, 0x02,        // Feature (Data, Variable, Absolute)
    0xC0               // End Collection
};

template <typename SwdPort>
class DapLinkV1Class
    : public HID<sizeof(DAPLINK_V1_REPORT_DESC), DapLinkV1Def::MAX_REQUEST_SIZE,
                 DapLinkV1Def::MAX_RESPONSE_SIZE>
{
 public:
  struct InfoStrings
  {
    const char* vendor = nullptr;
    const char* product = nullptr;
    const char* serial = nullptr;
    const char* firmware_ver = nullptr;

    const char* device_vendor = nullptr;
    const char* device_name = nullptr;
    const char* board_vendor = nullptr;
    const char* board_name = nullptr;
    const char* product_fw_ver = nullptr;
  };

  explicit DapLinkV1Class(SwdPort& swd_link, LibXR::GPIO* nreset_gpio = nullptr,
                          Endpoint::EPNumber in_ep_num = Endpoint::EPNumber::EP_AUTO,
                          Endpoint::EPNumber out_ep_num = Endpoint::EPNumber::EP_AUTO);

  ~DapLinkV1Class() override = default;

  DapLinkV1Class(const DapLinkV1Class&) = delete;
  DapLinkV1Class& operator=(const DapLinkV1Class&) = delete;

  void SetInfoStrings(const InfoStrings& info);
  void SetJtag(LibXR::Debug::Jtag* jtag);
  const LibXR::USB::DapLinkV1Def::State& GetState() const;
  bool IsInited() const;

 protected:
  ErrorCode WriteDeviceDescriptor(DeviceDescriptor& header) override;
  ConstRawData GetReportDesc() override;

  void BindEndpoints(EndpointPool& endpoint_pool, uint8_t start_itf_num) override;
  void UnbindEndpoints(EndpointPool& endpoint_pool) override;

  void OnDataOutComplete(bool in_isr, LibXR::ConstRawData& data) override;
  void OnDataInComplete(bool in_isr, LibXR::ConstRawData& data) override;

 private:
  static void OnDataOutCompleteStatic(bool in_isr, DapLinkV1Class* self,
                                      LibXR::ConstRawData& data);
  static void OnDataInCompleteStatic(bool in_isr, DapLinkV1Class* self,
                                     LibXR::ConstRawData& data);

  void ArmOutTransferIfIdle();

  ErrorCode ProcessOneCommand(bool in_isr, const uint8_t* req, uint16_t req_len,
                              uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);
  void BuildNotSupportResponse(uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

  ErrorCode HandleInfo(bool in_isr, const uint8_t* req, uint16_t req_len, uint8_t* resp,
                       uint16_t resp_cap, uint16_t& out_len);
  ErrorCode HandleHostStatus(bool in_isr, const uint8_t* req, uint16_t req_len,
                             uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);
  ErrorCode HandleConnect(bool in_isr, const uint8_t* req, uint16_t req_len,
                          uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);
  ErrorCode HandleDisconnect(bool in_isr, const uint8_t* req, uint16_t req_len,
                             uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);
  ErrorCode HandleTransferConfigure(bool in_isr, const uint8_t* req, uint16_t req_len,
                                    uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);
  ErrorCode HandleTransfer(bool in_isr, const uint8_t* req, uint16_t req_len,
                           uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);
  ErrorCode HandleTransferBlock(bool in_isr, const uint8_t* req, uint16_t req_len,
                                uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);
  ErrorCode HandleTransferAbort(bool in_isr, const uint8_t* req, uint16_t req_len,
                                uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);
  ErrorCode HandleWriteABORT(bool in_isr, const uint8_t* req, uint16_t req_len,
                             uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);
  ErrorCode HandleDelay(bool in_isr, const uint8_t* req, uint16_t req_len, uint8_t* resp,
                        uint16_t resp_cap, uint16_t& out_len);
  ErrorCode HandleResetTarget(bool in_isr, const uint8_t* req, uint16_t req_len,
                              uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);
  ErrorCode HandleSWJPins(bool in_isr, const uint8_t* req, uint16_t req_len,
                          uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);
  ErrorCode HandleSWJClock(bool in_isr, const uint8_t* req, uint16_t req_len,
                           uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);
  ErrorCode HandleSWJSequence(bool in_isr, const uint8_t* req, uint16_t req_len,
                              uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);
  ErrorCode HandleSWDConfigure(bool in_isr, const uint8_t* req, uint16_t req_len,
                               uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);
  ErrorCode HandleSWDSequence(bool in_isr, const uint8_t* req, uint16_t req_len,
                              uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);
  ErrorCode HandleJTAGSequence(bool in_isr, const uint8_t* req, uint16_t req_len,
                               uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);
  ErrorCode HandleJTAGConfigure(bool in_isr, const uint8_t* req, uint16_t req_len,
                                uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);
  ErrorCode HandleJTAGIdCode(bool in_isr, const uint8_t* req, uint16_t req_len,
                             uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);
  ErrorCode HandleQueueCommands(bool in_isr, const uint8_t* req, uint16_t req_len,
                                uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);
  ErrorCode HandleExecuteCommands(bool in_isr, const uint8_t* req, uint16_t req_len,
                                  uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

  ErrorCode BuildInfoStringResponse(uint8_t cmd, const char* str, uint8_t* resp,
                                    uint16_t resp_cap, uint16_t& out_len);
  ErrorCode BuildInfoU8Response(uint8_t cmd, uint8_t val, uint8_t* resp,
                                uint16_t resp_cap, uint16_t& out_len);
  ErrorCode BuildInfoU16Response(uint8_t cmd, uint16_t val, uint8_t* resp,
                                 uint16_t resp_cap, uint16_t& out_len);
  ErrorCode BuildInfoU32Response(uint8_t cmd, uint32_t val, uint8_t* resp,
                                 uint16_t resp_cap, uint16_t& out_len);

  uint8_t MapAckToDapResp(LibXR::Debug::SwdProtocol::Ack ack) const;
  uint8_t MapAckToDapResp(LibXR::Debug::JtagProtocol::Ack ack) const;
  ErrorCode HandleJtagTransfer(bool in_isr, const uint8_t* req, uint16_t req_len,
                               uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);
  ErrorCode HandleJtagTransferBlock(bool in_isr, const uint8_t* req, uint16_t req_len,
                                    uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);
  ErrorCode HandleJtagWriteAbort(bool in_isr, const uint8_t* req, uint16_t req_len,
                                 uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);
  void SetTransferAbortFlag(bool on);

 void DriveReset(bool release);
  void DelayUsIfAllowed(bool in_isr, uint32_t us);

 private:
  template <typename E>
  static constexpr uint8_t to_u8(E e)
  {
    return static_cast<uint8_t>(e);
  }

  static constexpr uint8_t DAP_OK = 0x00u;
  static constexpr uint8_t DAP_ERROR = 0xFFu;

  static inline ErrorCode build_unknown_cmd_response(uint8_t* resp, uint16_t cap,
                                                     uint16_t& out_len)
  {
    if (!resp || cap < 1u)
    {
      out_len = 0u;
      return ErrorCode::NOT_FOUND;
    }
    resp[0] = 0xFFu;
    out_len = 1u;
    return ErrorCode::OK;
  }

  static inline ErrorCode build_cmd_status_response(uint8_t cmd, uint8_t status,
                                                    uint8_t* resp, uint16_t cap,
                                                    uint16_t& out_len)
  {
    if (!resp || cap < 2u)
    {
      out_len = 0u;
      return ErrorCode::NOT_FOUND;
    }
    resp[0] = cmd;
    resp[1] = status;
    out_len = 2u;
    return ErrorCode::OK;
  }

  static constexpr uint16_t MAX_REQ = DapLinkV1Def::MAX_REQUEST_SIZE;
  static constexpr uint16_t MAX_RESP = DapLinkV1Def::MAX_RESPONSE_SIZE;
  static constexpr uint8_t JTAG_MAX_DEVICES = 16u;

  SwdPort& swd_;
  LibXR::Debug::Jtag* jtag_ = nullptr;
  LibXR::Debug::JtagProtocol::ChainConfig jtag_chain_{};
  uint8_t jtag_ir_len_[JTAG_MAX_DEVICES] = {};
  uint16_t jtag_ir_before_[JTAG_MAX_DEVICES] = {};
  uint16_t jtag_ir_after_[JTAG_MAX_DEVICES] = {};
  LibXR::GPIO* nreset_gpio_ = nullptr;

  uint8_t swj_shadow_ = static_cast<uint8_t>(DapLinkV1Def::DAP_SWJ_SWDIO_TMS |
                                             DapLinkV1Def::DAP_SWJ_NRESET);
  bool last_nreset_level_high_ = true;

  LibXR::USB::DapLinkV1Def::State dap_state_{};
  InfoStrings info_{"XRobot", "DAPLinkV1", "00000001", "1.0.0", "XRUSB",
                    "XRDAP",  "XRobot",    "DAP_DEMO", "0.1.0"};

  uint32_t swj_clock_hz_ = 1000000u;
  uint32_t match_mask_ = 0xFFFFFFFFu;

  Endpoint* ep_in_ = nullptr;   // Cached HID IN endpoint
  Endpoint* ep_out_ = nullptr;  // Cached HID OUT endpoint

  LibXR::Callback<LibXR::ConstRawData&> on_data_out_cb_ =
      LibXR::Callback<LibXR::ConstRawData&>::Create(OnDataOutCompleteStatic, this);
  LibXR::Callback<LibXR::ConstRawData&> on_data_in_cb_ =
      LibXR::Callback<LibXR::ConstRawData&>::Create(OnDataInCompleteStatic, this);

  bool inited_ = false;
};

template <typename SwdPort>
DapLinkV1Class<SwdPort>::DapLinkV1Class(SwdPort& swd_link, LibXR::GPIO* nreset_gpio,
                               Endpoint::EPNumber in_ep_num,
                               Endpoint::EPNumber out_ep_num)
    : HID(true, 1, 1, in_ep_num, out_ep_num), swd_(swd_link), nreset_gpio_(nreset_gpio)
{
  jtag_chain_.ir_length = jtag_ir_len_;
  jtag_chain_.ir_before = jtag_ir_before_;
  jtag_chain_.ir_after = jtag_ir_after_;
  jtag_chain_.count = 0u;
  jtag_chain_.index = 0u;
  (void)swd_.SetClockHz(swj_clock_hz_);
}

template <typename SwdPort>
void DapLinkV1Class<SwdPort>::SetInfoStrings(const InfoStrings& info) { info_ = info; }

template <typename SwdPort>
void DapLinkV1Class<SwdPort>::SetJtag(LibXR::Debug::Jtag* jtag)
{
  jtag_ = jtag;
}

template <typename SwdPort>
const LibXR::USB::DapLinkV1Def::State& DapLinkV1Class<SwdPort>::GetState() const
{
  return dap_state_;
}

template <typename SwdPort>
bool DapLinkV1Class<SwdPort>::IsInited() const { return inited_; }

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::WriteDeviceDescriptor(DeviceDescriptor& header)
{
  header.data_.bDeviceClass = DeviceDescriptor::ClassID::HID;
  header.data_.bDeviceSubClass = 0;
  header.data_.bDeviceProtocol = 0;
  return ErrorCode::OK;
}

template <typename SwdPort>
ConstRawData DapLinkV1Class<SwdPort>::GetReportDesc()
{
  return ConstRawData{DAPLINK_V1_REPORT_DESC, sizeof(DAPLINK_V1_REPORT_DESC)};
}

template <typename SwdPort>
void DapLinkV1Class<SwdPort>::BindEndpoints(EndpointPool& endpoint_pool, uint8_t start_itf_num)
{
  HID::BindEndpoints(endpoint_pool, start_itf_num);

  ep_in_ = GetInEndpoint();
  ep_out_ = GetOutEndpoint();

  if (ep_in_ != nullptr)
  {
    // Double buffer splits RawData into two halves; ensure EP IN buffer >= 2 * MAX_RESP.
    ep_in_->Configure(
        {Endpoint::Direction::IN, Endpoint::Type::INTERRUPT, MAX_RESP, true});
    ep_in_->SetOnTransferCompleteCallback(on_data_in_cb_);
  }

  if (ep_out_ != nullptr)
  {
    // Double buffer splits RawData into two halves; ensure EP OUT buffer >= 2 * MAX_REQ.
    ep_out_->Configure(
        {Endpoint::Direction::OUT, Endpoint::Type::INTERRUPT, MAX_REQ, true});
    ep_out_->SetOnTransferCompleteCallback(on_data_out_cb_);
  }

  inited_ = true;
  match_mask_ = 0xFFFFFFFFu;

  dap_state_ = {};
  dap_state_.debug_port = LibXR::USB::DapLinkV1Def::DebugPort::DISABLED;
  dap_state_.transfer_abort = false;

  swj_clock_hz_ = 1000000u;
  (void)swd_.SetClockHz(swj_clock_hz_);
  if (jtag_ != nullptr)
  {
    (void)jtag_->SetClockHz(swj_clock_hz_);
  }

  jtag_chain_.count = 0u;
  jtag_chain_.index = 0u;
  jtag_chain_.ir_before_bits_len = 0u;
  jtag_chain_.ir_after_bits_len = 0u;
  jtag_chain_.dr_before_bits_len = 0u;
  jtag_chain_.dr_after_bits_len = 0u;
  Memory::FastSet(jtag_ir_len_, 0, sizeof(jtag_ir_len_));
  Memory::FastSet(jtag_ir_before_, 0, sizeof(jtag_ir_before_));
  Memory::FastSet(jtag_ir_after_, 0, sizeof(jtag_ir_after_));

  last_nreset_level_high_ = true;
  swj_shadow_ = static_cast<uint8_t>(DapLinkV1Def::DAP_SWJ_SWDIO_TMS |
                                     DapLinkV1Def::DAP_SWJ_NRESET);

  ArmOutTransferIfIdle();
}

template <typename SwdPort>
void DapLinkV1Class<SwdPort>::UnbindEndpoints(EndpointPool& endpoint_pool)
{
  inited_ = false;

  dap_state_.debug_port = LibXR::USB::DapLinkV1Def::DebugPort::DISABLED;
  dap_state_.transfer_abort = false;

  HID::UnbindEndpoints(endpoint_pool);

  ep_in_ = nullptr;
  ep_out_ = nullptr;

  swd_.Close();
  if (jtag_ != nullptr)
  {
    jtag_->Close();
  }

  last_nreset_level_high_ = true;
  swj_shadow_ = static_cast<uint8_t>(DapLinkV1Def::DAP_SWJ_SWDIO_TMS |
                                     DapLinkV1Def::DAP_SWJ_NRESET);
}

template <typename SwdPort>
void DapLinkV1Class<SwdPort>::OnDataOutCompleteStatic(bool in_isr,
                                                      DapLinkV1Class* self,
                                                      LibXR::ConstRawData& data)
{
  if (self && self->inited_)
  {
    self->OnDataOutComplete(in_isr, data);
  }
}

template <typename SwdPort>
void DapLinkV1Class<SwdPort>::OnDataInCompleteStatic(bool in_isr,
                                                     DapLinkV1Class* self,
                                                     LibXR::ConstRawData& data)
{
  if (self && self->inited_)
  {
    self->OnDataInComplete(in_isr, data);
  }
}

template <typename SwdPort>
void DapLinkV1Class<SwdPort>::OnDataOutComplete(bool in_isr, LibXR::ConstRawData& data)
{
  if (!inited_ || !ep_in_ || !ep_out_)
  {
    return;
  }

  const auto* REQ = static_cast<const uint8_t*>(data.addr_);
  const uint16_t REQ_LEN = static_cast<uint16_t>(data.size_);

  auto tx_buff = ep_in_->GetBuffer();
  const uint16_t RESP_CAP = static_cast<uint16_t>(tx_buff.size_);
  const uint16_t TX_LEN = (RESP_CAP < MAX_RESP) ? RESP_CAP : MAX_RESP;

  if (ep_in_->GetState() == Endpoint::State::IDLE)
  {
    ArmOutTransferIfIdle();
  }

  Memory::FastSet(tx_buff.addr_, 0, RESP_CAP);
  uint16_t out_len = 0u;
  auto ans = ProcessOneCommand(in_isr, REQ, REQ_LEN,
                               reinterpret_cast<uint8_t*>(tx_buff.addr_), RESP_CAP,
                               out_len);
  UNUSED(ans);
  UNUSED(out_len);

  if (ep_in_->GetState() == Endpoint::State::IDLE)
  {
    ep_in_->Transfer(TX_LEN);
    ep_in_->SetActiveLength(0);
  }
  else
  {
    ep_in_->SetActiveLength(TX_LEN);
  }
}

template <typename SwdPort>
void DapLinkV1Class<SwdPort>::OnDataInComplete(bool in_isr, LibXR::ConstRawData& data)
{
  UNUSED(in_isr);
  UNUSED(data);

  auto act_len = ep_in_->GetActiveLength();
  if (act_len > 0)
  {
    ep_in_->Transfer(act_len);
    ep_in_->SetActiveLength(0);
  }

  ArmOutTransferIfIdle();
}

template <typename SwdPort>
void DapLinkV1Class<SwdPort>::ArmOutTransferIfIdle()
{
  if (!inited_ || !ep_out_)
  {
    return;
  }

  if (ep_out_->GetState() != Endpoint::State::IDLE)
  {
    return;
  }

  (void)ep_out_->Transfer(ep_out_->MaxTransferSize());
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::ProcessOneCommand(bool in_isr, const uint8_t* req,
                                            uint16_t req_len, uint8_t* resp,
                                            uint16_t resp_cap, uint16_t& out_len)
{
  out_len = 0u;

  if (!req || !resp || req_len < 1u || resp_cap < 1u)
  {
    if (resp && resp_cap >= 1u)
    {
      BuildNotSupportResponse(resp, resp_cap, out_len);
    }
    return ErrorCode::ARG_ERR;
  }

  const uint8_t CMD = req[0];

  switch (CMD)
  {
    case to_u8(LibXR::USB::DapLinkV1Def::CommandId::INFO):
      return HandleInfo(in_isr, req, req_len, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV1Def::CommandId::HOST_STATUS):
      return HandleHostStatus(in_isr, req, req_len, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV1Def::CommandId::CONNECT):
      return HandleConnect(in_isr, req, req_len, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV1Def::CommandId::DISCONNECT):
      return HandleDisconnect(in_isr, req, req_len, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV1Def::CommandId::TRANSFER_CONFIGURE):
      return HandleTransferConfigure(in_isr, req, req_len, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV1Def::CommandId::TRANSFER):
      return HandleTransfer(in_isr, req, req_len, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV1Def::CommandId::TRANSFER_BLOCK):
      return HandleTransferBlock(in_isr, req, req_len, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV1Def::CommandId::TRANSFER_ABORT):
      return HandleTransferAbort(in_isr, req, req_len, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV1Def::CommandId::WRITE_ABORT):
      return HandleWriteABORT(in_isr, req, req_len, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV1Def::CommandId::DELAY):
      return HandleDelay(in_isr, req, req_len, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV1Def::CommandId::RESET_TARGET):
      return HandleResetTarget(in_isr, req, req_len, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV1Def::CommandId::SWJ_PINS):
      return HandleSWJPins(in_isr, req, req_len, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV1Def::CommandId::SWJ_CLOCK):
      return HandleSWJClock(in_isr, req, req_len, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV1Def::CommandId::SWJ_SEQUENCE):
      return HandleSWJSequence(in_isr, req, req_len, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV1Def::CommandId::SWD_CONFIGURE):
      return HandleSWDConfigure(in_isr, req, req_len, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV1Def::CommandId::SWD_SEQUENCE):
      return HandleSWDSequence(in_isr, req, req_len, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV1Def::CommandId::JTAG_SEQUENCE):
      return HandleJTAGSequence(in_isr, req, req_len, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV1Def::CommandId::JTAG_CONFIGURE):
      return HandleJTAGConfigure(in_isr, req, req_len, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV1Def::CommandId::JTAG_IDCODE):
      return HandleJTAGIdCode(in_isr, req, req_len, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV1Def::CommandId::QUEUE_COMMANDS):
      return HandleQueueCommands(in_isr, req, req_len, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV1Def::CommandId::EXECUTE_COMMANDS):
      return HandleExecuteCommands(in_isr, req, req_len, resp, resp_cap, out_len);
    default:
      (void)build_unknown_cmd_response(resp, resp_cap, out_len);
      return ErrorCode::NOT_SUPPORT;
  }
}

template <typename SwdPort>
void DapLinkV1Class<SwdPort>::BuildNotSupportResponse(uint8_t* resp, uint16_t resp_cap,
                                             uint16_t& out_len)
{
  if (!resp || resp_cap < 1u)
  {
    out_len = 0u;
    return;
  }
  resp[0] = 0xFFu;
  out_len = 1u;
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::HandleInfo(bool /*in_isr*/, const uint8_t* req,
                                     uint16_t req_len, uint8_t* resp, uint16_t resp_cap,
                                     uint16_t& out_len)
{
  if (req_len < 2u)
  {
    resp[0] = to_u8(LibXR::USB::DapLinkV1Def::CommandId::INFO);
    resp[1] = 0u;
    out_len = 2u;
    return ErrorCode::ARG_ERR;
  }

  const uint8_t INFO_ID = req[1];

  resp[0] = to_u8(LibXR::USB::DapLinkV1Def::CommandId::INFO);

  switch (INFO_ID)
  {
    case to_u8(LibXR::USB::DapLinkV1Def::InfoId::VENDOR):
      return BuildInfoStringResponse(resp[0], info_.vendor, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV1Def::InfoId::PRODUCT):
      return BuildInfoStringResponse(resp[0], info_.product, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV1Def::InfoId::SERIAL_NUMBER):
      return BuildInfoStringResponse(resp[0], info_.serial, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV1Def::InfoId::FIRMWARE_VERSION):
      return BuildInfoStringResponse(resp[0], info_.firmware_ver, resp, resp_cap,
                                     out_len);
    case to_u8(LibXR::USB::DapLinkV1Def::InfoId::DEVICE_VENDOR):
      return BuildInfoStringResponse(resp[0], info_.device_vendor, resp, resp_cap,
                                     out_len);
    case to_u8(LibXR::USB::DapLinkV1Def::InfoId::DEVICE_NAME):
      return BuildInfoStringResponse(resp[0], info_.device_name, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV1Def::InfoId::BOARD_VENDOR):
      return BuildInfoStringResponse(resp[0], info_.board_vendor, resp, resp_cap,
                                     out_len);
    case to_u8(LibXR::USB::DapLinkV1Def::InfoId::BOARD_NAME):
      return BuildInfoStringResponse(resp[0], info_.board_name, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV1Def::InfoId::PRODUCT_FIRMWARE_VERSION):
      return BuildInfoStringResponse(resp[0], info_.product_fw_ver, resp, resp_cap,
                                     out_len);
    case to_u8(LibXR::USB::DapLinkV1Def::InfoId::CAPABILITIES):
    {
      uint8_t caps = LibXR::USB::DapLinkV1Def::DAP_CAP_SWD;
      if (jtag_ != nullptr)
      {
        caps = static_cast<uint8_t>(caps | LibXR::USB::DapLinkV1Def::DAP_CAP_JTAG);
      }
      return BuildInfoU8Response(resp[0], caps, resp, resp_cap, out_len);
    }
    case to_u8(LibXR::USB::DapLinkV1Def::InfoId::PACKET_COUNT):
      return BuildInfoU8Response(resp[0], 1, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV1Def::InfoId::PACKET_SIZE):
      return BuildInfoU16Response(resp[0], MAX_RESP, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV1Def::InfoId::TIMESTAMP_CLOCK):
      return BuildInfoU32Response(resp[0], 1000000U, resp, resp_cap, out_len);
    default:
      resp[1] = 0u;
      out_len = 2u;
      return ErrorCode::OK;
  }
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::BuildInfoStringResponse(uint8_t cmd, const char* str,
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

  const size_t N_WITH_NUL = std::strlen(str) + 1;
  const size_t MAX_PAYLOAD = (resp_cap >= 2u) ? (resp_cap - 2u) : 0u;
  if (MAX_PAYLOAD == 0u)
  {
    out_len = 2u;
    return ErrorCode::OK;
  }

  const size_t COPY_N = (N_WITH_NUL > MAX_PAYLOAD) ? MAX_PAYLOAD : N_WITH_NUL;
  Memory::FastCopy(&resp[2], str, COPY_N);
  resp[1] = static_cast<uint8_t>(COPY_N);
  out_len = static_cast<uint16_t>(COPY_N + 2u);
  return ErrorCode::OK;
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::BuildInfoU8Response(uint8_t cmd, uint8_t val, uint8_t* resp,
                                              uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 3u)
  {
    out_len = 0u;
    return ErrorCode::NOT_FOUND;
  }
  resp[0] = cmd;
  resp[1] = 1u;
  resp[2] = val;
  out_len = 3u;
  return ErrorCode::OK;
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::BuildInfoU16Response(uint8_t cmd, uint16_t val, uint8_t* resp,
                                               uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 4u)
  {
    out_len = 0u;
    return ErrorCode::NOT_FOUND;
  }
  resp[0] = cmd;
  resp[1] = 2u;
  Memory::FastCopy(&resp[2], &val, sizeof(val));
  out_len = 4u;
  return ErrorCode::OK;
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::BuildInfoU32Response(uint8_t cmd, uint32_t val, uint8_t* resp,
                                               uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 6u)
  {
    out_len = 0u;
    return ErrorCode::NOT_FOUND;
  }
  resp[0] = cmd;
  resp[1] = 4u;
  Memory::FastCopy(&resp[2], &val, sizeof(val));
  out_len = 6u;
  return ErrorCode::OK;
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::HandleHostStatus(bool /*in_isr*/, const uint8_t* /*req*/,
                                           uint16_t /*req_len*/, uint8_t* resp,
                                           uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0u;
    return ErrorCode::NOT_FOUND;
  }
  resp[0] = to_u8(LibXR::USB::DapLinkV1Def::CommandId::HOST_STATUS);
  resp[1] = to_u8(LibXR::USB::DapLinkV1Def::Status::OK);
  out_len = 2u;
  return ErrorCode::OK;
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::HandleConnect(bool /*in_isr*/, const uint8_t* req,
                                        uint16_t req_len, uint8_t* resp,
                                        uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0u;
    return ErrorCode::NOT_FOUND;
  }

  resp[0] = to_u8(LibXR::USB::DapLinkV1Def::CommandId::CONNECT);

  uint8_t port = 0u;
  if (req_len >= 2u)
  {
    port = req[1];
  }

  if (port == 0u)
  {
    port = to_u8(LibXR::USB::DapLinkV1Def::Port::SWD);
  }

  if (port == to_u8(LibXR::USB::DapLinkV1Def::Port::SWD))
  {
    (void)swd_.EnterSwd();
    (void)swd_.SetClockHz(swj_clock_hz_);

    dap_state_.debug_port = LibXR::USB::DapLinkV1Def::DebugPort::SWD;
    dap_state_.transfer_abort = false;

    resp[1] = to_u8(LibXR::USB::DapLinkV1Def::Port::SWD);
  }
  else if (port == to_u8(LibXR::USB::DapLinkV1Def::Port::JTAG))
  {
    if (jtag_ == nullptr)
    {
      resp[1] = to_u8(LibXR::USB::DapLinkV1Def::Port::DISABLED);
    }
    else
    {
      (void)jtag_->SetClockHz(swj_clock_hz_);
      dap_state_.debug_port = LibXR::USB::DapLinkV1Def::DebugPort::JTAG;
      dap_state_.transfer_abort = false;
      resp[1] = to_u8(LibXR::USB::DapLinkV1Def::Port::JTAG);
    }
  }
  else
  {
    resp[1] = to_u8(LibXR::USB::DapLinkV1Def::Port::DISABLED);
  }

  out_len = 2u;
  return ErrorCode::OK;
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::HandleDisconnect(bool /*in_isr*/, const uint8_t* /*req*/,
                                           uint16_t /*req_len*/, uint8_t* resp,
                                           uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0u;
    return ErrorCode::NOT_FOUND;
  }

  swd_.Close();
  dap_state_.debug_port = LibXR::USB::DapLinkV1Def::DebugPort::DISABLED;
  dap_state_.transfer_abort = false;

  resp[0] = to_u8(LibXR::USB::DapLinkV1Def::CommandId::DISCONNECT);
  resp[1] = to_u8(LibXR::USB::DapLinkV1Def::Status::OK);
  out_len = 2u;
  return ErrorCode::OK;
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::HandleTransferConfigure(bool /*in_isr*/, const uint8_t* req,
                                                  uint16_t req_len, uint8_t* resp,
                                                  uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0u;
    return ErrorCode::NOT_FOUND;
  }

  resp[0] = to_u8(LibXR::USB::DapLinkV1Def::CommandId::TRANSFER_CONFIGURE);

  if (req_len < 6u)
  {
    resp[1] = to_u8(LibXR::USB::DapLinkV1Def::Status::ERROR);
    out_len = 2u;
    return ErrorCode::ARG_ERR;
  }

  const uint8_t IDLE = req[1];

  uint16_t wait_retry = 0u;
  uint16_t match_retry = 0u;
  Memory::FastCopy(&wait_retry, &req[2], sizeof(wait_retry));
  Memory::FastCopy(&match_retry, &req[4], sizeof(match_retry));

  dap_state_.transfer_cfg.idle_cycles = IDLE;
  dap_state_.transfer_cfg.retry_count = wait_retry;
  dap_state_.transfer_cfg.match_retry = match_retry;

  LibXR::Debug::Swd::TransferPolicy pol = swd_.GetTransferPolicy();
  pol.idle_cycles = IDLE;
  pol.wait_retry = wait_retry;
  swd_.SetTransferPolicy(pol);

  resp[1] = to_u8(LibXR::USB::DapLinkV1Def::Status::OK);
  out_len = 2u;
  return ErrorCode::OK;
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::HandleTransferAbort(bool /*in_isr*/, const uint8_t* /*req*/,
                                              uint16_t /*req_len*/, uint8_t* resp,
                                              uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0u;
    return ErrorCode::NOT_FOUND;
  }

  SetTransferAbortFlag(true);

  resp[0] = to_u8(LibXR::USB::DapLinkV1Def::CommandId::TRANSFER_ABORT);
  resp[1] = to_u8(LibXR::USB::DapLinkV1Def::Status::OK);
  out_len = 2u;
  return ErrorCode::OK;
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::HandleWriteABORT(bool /*in_isr*/, const uint8_t* req,
                                           uint16_t req_len, uint8_t* resp,
                                           uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0u;
    return ErrorCode::NOT_FOUND;
  }

  if (dap_state_.debug_port == LibXR::USB::DapLinkV1Def::DebugPort::JTAG)
  {
    return HandleJtagWriteAbort(false, req, req_len, resp, resp_cap, out_len);
  }

  resp[0] = to_u8(LibXR::USB::DapLinkV1Def::CommandId::WRITE_ABORT);

  if (req_len < 6u)
  {
    resp[1] = to_u8(LibXR::USB::DapLinkV1Def::Status::ERROR);
    out_len = 2u;
    return ErrorCode::ARG_ERR;
  }

  uint32_t flags = 0u;
  Memory::FastCopy(&flags, &req[2], sizeof(flags));

  LibXR::Debug::SwdProtocol::Ack ack = LibXR::Debug::SwdProtocol::Ack::PROTOCOL;
  const ErrorCode EC = swd_.WriteAbortTxn(flags, ack);
  resp[1] = (EC == ErrorCode::OK && ack == LibXR::Debug::SwdProtocol::Ack::OK)
                ? to_u8(LibXR::USB::DapLinkV1Def::Status::OK)
                : to_u8(LibXR::USB::DapLinkV1Def::Status::ERROR);

  out_len = 2u;
  return ErrorCode::OK;
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::HandleDelay(bool /*in_isr*/, const uint8_t* req,
                                      uint16_t req_len, uint8_t* resp, uint16_t resp_cap,
                                      uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0u;
    return ErrorCode::NOT_FOUND;
  }

  resp[0] = to_u8(LibXR::USB::DapLinkV1Def::CommandId::DELAY);

  if (req_len < 3u)
  {
    resp[1] = to_u8(LibXR::USB::DapLinkV1Def::Status::ERROR);
    out_len = 2u;
    return ErrorCode::ARG_ERR;
  }

  uint16_t us = 0u;
  Memory::FastCopy(&us, &req[1], sizeof(us));

  LibXR::Timebase::DelayMicroseconds(us);

  resp[1] = to_u8(LibXR::USB::DapLinkV1Def::Status::OK);
  out_len = 2u;
  return ErrorCode::OK;
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::HandleResetTarget(bool in_isr, const uint8_t* /*req*/,
                                            uint16_t /*req_len*/, uint8_t* resp,
                                            uint16_t resp_cap, uint16_t& out_len)
{
  if (!resp || resp_cap < 3u)
  {
    out_len = 0u;
    return ErrorCode::NOT_FOUND;
  }

  resp[0] = to_u8(LibXR::USB::DapLinkV1Def::CommandId::RESET_TARGET);

  uint8_t execute = 0u;
  if (nreset_gpio_ != nullptr)
  {
    DriveReset(false);
    DelayUsIfAllowed(in_isr, 1000u);
    DriveReset(true);
    DelayUsIfAllowed(in_isr, 1000u);
    execute = 1u;
  }

  resp[1] = DAP_OK;
  resp[2] = execute;
  out_len = 3u;
  return ErrorCode::OK;
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::HandleSWJPins(bool /*in_isr*/, const uint8_t* req,
                                        uint16_t req_len, uint8_t* resp,
                                        uint16_t resp_cap, uint16_t& out_len)
{
  if (!resp || resp_cap < 2u)
  {
    out_len = 0u;
    return ErrorCode::NOT_FOUND;
  }

  resp[0] = to_u8(LibXR::USB::DapLinkV1Def::CommandId::SWJ_PINS);

  if (!req || req_len < 7u)
  {
    resp[1] = 0u;
    out_len = 2u;
    return ErrorCode::ARG_ERR;
  }

  const uint8_t PIN_OUT = req[1];
  const uint8_t PIN_SEL = req[2];

  uint32_t wait_us = 0u;
  Memory::FastCopy(&wait_us, &req[3], sizeof(wait_us));

  swj_shadow_ = static_cast<uint8_t>((swj_shadow_ & static_cast<uint8_t>(~PIN_SEL)) |
                                     (PIN_OUT & PIN_SEL));

  if ((PIN_SEL & LibXR::USB::DapLinkV1Def::DAP_SWJ_NRESET) != 0u)
  {
    const bool LEVEL_HIGH =
        ((PIN_OUT & LibXR::USB::DapLinkV1Def::DAP_SWJ_NRESET) != 0u);
    DriveReset(LEVEL_HIGH);
  }

  auto read_pins = [&]() -> uint8_t
  {
    uint8_t pin_in = swj_shadow_;

    if (nreset_gpio_ != nullptr)
    {
      if (nreset_gpio_->Read())
      {
        pin_in |= LibXR::USB::DapLinkV1Def::DAP_SWJ_NRESET;
      }
      else
      {
        pin_in = static_cast<uint8_t>(
            pin_in & static_cast<uint8_t>(~LibXR::USB::DapLinkV1Def::DAP_SWJ_NRESET));
      }
    }

    return pin_in;
  };

  uint8_t pin_in = 0u;
  if (wait_us == 0u || PIN_SEL == 0u)
  {
    pin_in = read_pins();
  }
  else
  {
    const uint64_t START = LibXR::Timebase::GetMicroseconds();
    const uint8_t EXPECT = static_cast<uint8_t>(PIN_OUT & PIN_SEL);

    do
    {
      pin_in = read_pins();
      if ((pin_in & PIN_SEL) == EXPECT)
      {
        break;
      }
    } while ((static_cast<uint64_t>(LibXR::Timebase::GetMicroseconds()) - START) <
             wait_us);
  }

  resp[1] = pin_in;
  out_len = 2u;
  return ErrorCode::OK;
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::HandleSWJClock(bool /*in_isr*/, const uint8_t* req,
                                         uint16_t req_len, uint8_t* resp,
                                         uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0u;
    return ErrorCode::NOT_FOUND;
  }

  resp[0] = to_u8(LibXR::USB::DapLinkV1Def::CommandId::SWJ_CLOCK);

  if (req_len < 5u)
  {
    resp[1] = to_u8(LibXR::USB::DapLinkV1Def::Status::ERROR);
    out_len = 2u;
    return ErrorCode::ARG_ERR;
  }

  uint32_t hz = 0u;
  Memory::FastCopy(&hz, &req[1], sizeof(hz));

  swj_clock_hz_ = hz;
  (void)swd_.SetClockHz(hz);
  if (jtag_ != nullptr)
  {
    (void)jtag_->SetClockHz(hz);
  }

  resp[1] = to_u8(LibXR::USB::DapLinkV1Def::Status::OK);
  out_len = 2u;
  return ErrorCode::OK;
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::HandleSWJSequence(bool /*in_isr*/, const uint8_t* req,
                                            uint16_t req_len, uint8_t* resp,
                                            uint16_t resp_cap, uint16_t& out_len)
{
  if (!resp || resp_cap < 2u)
  {
    out_len = 0u;
    return ErrorCode::NOT_FOUND;
  }

  resp[0] = to_u8(LibXR::USB::DapLinkV1Def::CommandId::SWJ_SEQUENCE);
  resp[1] = to_u8(LibXR::USB::DapLinkV1Def::Status::OK);
  out_len = 2u;

  if (!req || req_len < 2u)
  {
    resp[1] = to_u8(LibXR::USB::DapLinkV1Def::Status::ERROR);
    return ErrorCode::ARG_ERR;
  }

  uint32_t bit_count = req[1];
  if (bit_count == 0u)
  {
    bit_count = 256u;
  }

  const uint32_t BYTE_COUNT = (bit_count + 7u) / 8u;
  if (req_len < (2u + BYTE_COUNT))
  {
    resp[1] = to_u8(LibXR::USB::DapLinkV1Def::Status::ERROR);
    return ErrorCode::ARG_ERR;
  }

  const uint8_t* DATA = &req[2];
  const ErrorCode EC = swd_.SeqWriteBits(bit_count, DATA);
  if (EC != ErrorCode::OK)
  {
    resp[1] = to_u8(LibXR::USB::DapLinkV1Def::Status::ERROR);
    return ErrorCode::OK;
  }

  swj_shadow_ = static_cast<uint8_t>(
      swj_shadow_ & static_cast<uint8_t>(~LibXR::USB::DapLinkV1Def::DAP_SWJ_SWCLK_TCK));

  bool last_swdio = false;
  if (bit_count != 0u)
  {
    const uint32_t LAST_I = bit_count - 1u;
    last_swdio = (((DATA[LAST_I / 8u] >> (LAST_I & 7u)) & 0x01u) != 0u);
  }

  if (last_swdio)
  {
    swj_shadow_ |= LibXR::USB::DapLinkV1Def::DAP_SWJ_SWDIO_TMS;
  }
  else
  {
    swj_shadow_ = static_cast<uint8_t>(
        swj_shadow_ & static_cast<uint8_t>(~LibXR::USB::DapLinkV1Def::DAP_SWJ_SWDIO_TMS));
  }

  return ErrorCode::OK;
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::HandleSWDConfigure(bool /*in_isr*/, const uint8_t* /*req*/,
                                             uint16_t /*req_len*/, uint8_t* resp,
                                             uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0u;
    return ErrorCode::NOT_FOUND;
  }

  resp[0] = to_u8(LibXR::USB::DapLinkV1Def::CommandId::SWD_CONFIGURE);
  resp[1] = to_u8(LibXR::USB::DapLinkV1Def::Status::OK);
  out_len = 2u;
  return ErrorCode::OK;
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::HandleSWDSequence(bool /*in_isr*/, const uint8_t* req,
                                            uint16_t req_len, uint8_t* resp,
                                            uint16_t resp_cap, uint16_t& out_len)
{
  if (!req || !resp || resp_cap < 2u)
  {
    out_len = 0u;
    return ErrorCode::ARG_ERR;
  }

  resp[0] = to_u8(LibXR::USB::DapLinkV1Def::CommandId::SWD_SEQUENCE);
  resp[1] = DAP_OK;
  out_len = 2u;

  if (req_len < 2u)
  {
    resp[1] = DAP_ERROR;
    out_len = 2u;
    return ErrorCode::ARG_ERR;
  }

  const uint8_t SEQ_CNT = req[1];
  uint16_t req_off = 2u;
  uint16_t resp_off = 2u;

  for (uint32_t s = 0; s < SEQ_CNT; ++s)
  {
    if (req_off >= req_len)
    {
      resp[1] = DAP_ERROR;
      out_len = 2u;
      return ErrorCode::ARG_ERR;
    }

    const uint8_t INFO = req[req_off++];
    uint32_t cycles = static_cast<uint32_t>(INFO & 0x3Fu);
    if (cycles == 0u)
    {
      cycles = 64u;
    }

    const bool MODE_IN = ((INFO & 0x80u) != 0u);
    const uint16_t BYTES = static_cast<uint16_t>((cycles + 7u) / 8u);

    if (!MODE_IN)
    {
      if (req_off + BYTES > req_len)
      {
        resp[1] = DAP_ERROR;
        out_len = 2u;
        return ErrorCode::ARG_ERR;
      }

      const uint8_t* DATA = &req[req_off];
      req_off = static_cast<uint16_t>(req_off + BYTES);

      const ErrorCode EC = swd_.SeqWriteBits(cycles, DATA);
      if (EC != ErrorCode::OK)
      {
        resp[1] = DAP_ERROR;
        out_len = 2u;
        return ErrorCode::OK;
      }
    }
    else
    {
      if (resp_off + BYTES > resp_cap)
      {
        resp[1] = DAP_ERROR;
        out_len = 2u;
        return ErrorCode::NOT_FOUND;
      }

      Memory::FastSet(&resp[resp_off], 0, BYTES);
      const ErrorCode EC = swd_.SeqReadBits(cycles, &resp[resp_off]);
      if (EC != ErrorCode::OK)
      {
        resp[1] = DAP_ERROR;
        out_len = 2u;
        return ErrorCode::OK;
      }

      resp_off = static_cast<uint16_t>(resp_off + BYTES);
    }
  }

  out_len = resp_off;
  return ErrorCode::OK;
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::HandleJTAGSequence(bool /*in_isr*/, const uint8_t* req,
                                             uint16_t req_len, uint8_t* resp,
                                             uint16_t resp_cap, uint16_t& out_len)
{
  if (!req || !resp || resp_cap < 2u)
  {
    out_len = 0u;
    return ErrorCode::ARG_ERR;
  }

  resp[0] = to_u8(LibXR::USB::DapLinkV1Def::CommandId::JTAG_SEQUENCE);
  resp[1] = DAP_OK;
  out_len = 2u;

  if (jtag_ == nullptr)
  {
    resp[1] = DAP_ERROR;
    return ErrorCode::OK;
  }

  if (req_len < 2u)
  {
    resp[1] = DAP_ERROR;
    return ErrorCode::ARG_ERR;
  }

  const uint8_t SEQ_CNT = req[1];
  uint16_t req_off = 2u;
  uint16_t resp_off = 2u;

  for (uint32_t s = 0; s < SEQ_CNT; ++s)
  {
    if (req_off >= req_len)
    {
      resp[1] = DAP_ERROR;
      out_len = 2u;
      return ErrorCode::ARG_ERR;
    }

    const uint8_t INFO = req[req_off++];
    uint32_t cycles = static_cast<uint32_t>(INFO & 0x3Fu);
    if (cycles == 0u)
    {
      cycles = 64u;
    }

    const bool TMS = ((INFO & LibXR::Debug::JtagProtocol::JTAG_SEQUENCE_TMS) != 0u);
    const bool CAPTURE =
        ((INFO & LibXR::Debug::JtagProtocol::JTAG_SEQUENCE_TDO) != 0u);
    const uint16_t BYTES = static_cast<uint16_t>((cycles + 7u) / 8u);

    if (req_off + BYTES > req_len)
    {
      resp[1] = DAP_ERROR;
      out_len = 2u;
      return ErrorCode::ARG_ERR;
    }

    if (CAPTURE && (resp_off + BYTES > resp_cap))
    {
      resp[1] = DAP_ERROR;
      out_len = 2u;
      return ErrorCode::NOT_FOUND;
    }

    const uint8_t* DATA = &req[req_off];
    uint8_t* OUT = CAPTURE ? &resp[resp_off] : nullptr;
    if (CAPTURE)
    {
      Memory::FastSet(OUT, 0, BYTES);
    }

    const ErrorCode EC = jtag_->Sequence(cycles, TMS, DATA, OUT);
    if (EC != ErrorCode::OK)
    {
      resp[1] = DAP_ERROR;
      out_len = 2u;
      return ErrorCode::OK;
    }

    req_off = static_cast<uint16_t>(req_off + BYTES);
    if (CAPTURE)
    {
      resp_off = static_cast<uint16_t>(resp_off + BYTES);
    }
  }

  out_len = resp_off;
  return ErrorCode::OK;
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::HandleJTAGConfigure(bool /*in_isr*/, const uint8_t* req,
                                              uint16_t req_len, uint8_t* resp,
                                              uint16_t resp_cap, uint16_t& out_len)
{
  if (!req || !resp || resp_cap < 2u)
  {
    out_len = 0u;
    return ErrorCode::ARG_ERR;
  }

  resp[0] = to_u8(LibXR::USB::DapLinkV1Def::CommandId::JTAG_CONFIGURE);
  resp[1] = DAP_OK;
  out_len = 2u;

  if (jtag_ == nullptr)
  {
    resp[1] = DAP_ERROR;
    return ErrorCode::OK;
  }

  if (req_len < 2u)
  {
    resp[1] = DAP_ERROR;
    return ErrorCode::ARG_ERR;
  }

  const uint8_t COUNT = req[1];
  if (COUNT > JTAG_MAX_DEVICES || req_len < static_cast<uint16_t>(2u + COUNT))
  {
    resp[1] = DAP_ERROR;
    return ErrorCode::ARG_ERR;
  }

  Memory::FastSet(jtag_ir_len_, 0, sizeof(jtag_ir_len_));
  Memory::FastSet(jtag_ir_before_, 0, sizeof(jtag_ir_before_));
  Memory::FastSet(jtag_ir_after_, 0, sizeof(jtag_ir_after_));

  uint32_t bits = 0u;
  for (uint32_t i = 0; i < COUNT; ++i)
  {
    const uint8_t LEN = req[static_cast<uint16_t>(2u + i)];
    jtag_ir_len_[i] = LEN;
    jtag_ir_before_[i] = static_cast<uint16_t>(bits);
    bits += LEN;
  }
  for (uint32_t i = 0; i < COUNT; ++i)
  {
    bits -= jtag_ir_len_[i];
    jtag_ir_after_[i] = static_cast<uint16_t>(bits);
  }

  jtag_chain_.count = COUNT;
  if (jtag_chain_.index >= jtag_chain_.count)
  {
    jtag_chain_.index = 0u;
  }
  jtag_chain_.ir_before_bits_len = 0u;
  jtag_chain_.ir_after_bits_len = 0u;
  jtag_chain_.dr_before_bits_len = 0u;
  jtag_chain_.dr_after_bits_len = 0u;

  return ErrorCode::OK;
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::HandleJTAGIdCode(bool /*in_isr*/, const uint8_t* req,
                                           uint16_t req_len, uint8_t* resp,
                                           uint16_t resp_cap, uint16_t& out_len)
{
  if (!req || !resp || resp_cap < 2u)
  {
    out_len = 0u;
    return ErrorCode::ARG_ERR;
  }

  if (resp_cap < 6u)
  {
    out_len = 0u;
    return ErrorCode::NOT_FOUND;
  }

  resp[0] = to_u8(LibXR::USB::DapLinkV1Def::CommandId::JTAG_IDCODE);
  resp[1] = DAP_ERROR;
  out_len = 2u;

  if (jtag_ == nullptr || dap_state_.debug_port != LibXR::USB::DapLinkV1Def::DebugPort::JTAG)
  {
    return ErrorCode::OK;
  }

  if (req_len < 2u)
  {
    return ErrorCode::ARG_ERR;
  }

  const uint8_t INDEX = req[1];
  if (INDEX >= jtag_chain_.count)
  {
    return ErrorCode::OK;
  }

  jtag_chain_.index = INDEX;
  LibXR::Debug::JtagProtocol::UpdateChainCache(jtag_chain_);

  LibXR::Debug::JtagDp dp(*jtag_, &jtag_chain_);
  uint32_t idcode = 0u;
  const ErrorCode EC = dp.ReadIdCode(idcode);
  if (EC != ErrorCode::OK)
  {
    return EC;
  }

  resp[1] = DAP_OK;
  resp[2] = static_cast<uint8_t>(idcode & 0xFFu);
  resp[3] = static_cast<uint8_t>((idcode >> 8) & 0xFFu);
  resp[4] = static_cast<uint8_t>((idcode >> 16) & 0xFFu);
  resp[5] = static_cast<uint8_t>((idcode >> 24) & 0xFFu);
  out_len = 6u;
  return ErrorCode::OK;
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::HandleQueueCommands(bool /*in_isr*/, const uint8_t* /*req*/,
                                              uint16_t /*req_len*/, uint8_t* resp,
                                              uint16_t resp_cap, uint16_t& out_len)
{
  return build_cmd_status_response(
      to_u8(LibXR::USB::DapLinkV1Def::CommandId::QUEUE_COMMANDS), DAP_ERROR, resp,
      resp_cap, out_len);
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::HandleExecuteCommands(bool /*in_isr*/, const uint8_t* /*req*/,
                                                uint16_t /*req_len*/, uint8_t* resp,
                                                uint16_t resp_cap, uint16_t& out_len)
{
  return build_cmd_status_response(
      to_u8(LibXR::USB::DapLinkV1Def::CommandId::EXECUTE_COMMANDS), DAP_ERROR, resp,
      resp_cap, out_len);
}

template <typename SwdPort>
uint8_t DapLinkV1Class<SwdPort>::MapAckToDapResp(LibXR::Debug::SwdProtocol::Ack ack) const
{
  switch (ack)
  {
    case LibXR::Debug::SwdProtocol::Ack::OK:
      return 1u;
    case LibXR::Debug::SwdProtocol::Ack::WAIT:
      return 2u;
    case LibXR::Debug::SwdProtocol::Ack::FAULT:
      return 4u;
    case LibXR::Debug::SwdProtocol::Ack::NO_ACK:
    case LibXR::Debug::SwdProtocol::Ack::PROTOCOL:
    default:
      return 7u;
  }
}

template <typename SwdPort>
uint8_t DapLinkV1Class<SwdPort>::MapAckToDapResp(
    LibXR::Debug::JtagProtocol::Ack ack) const
{
  switch (ack)
  {
    case LibXR::Debug::JtagProtocol::Ack::OK:
      return 1u;
    case LibXR::Debug::JtagProtocol::Ack::WAIT:
      return 2u;
    case LibXR::Debug::JtagProtocol::Ack::FAULT:
      return 4u;
    case LibXR::Debug::JtagProtocol::Ack::NO_ACK:
    case LibXR::Debug::JtagProtocol::Ack::PROTOCOL:
    default:
      return 7u;
  }
}

template <typename SwdPort>
void DapLinkV1Class<SwdPort>::SetTransferAbortFlag(bool on) { dap_state_.transfer_abort = on; }

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::HandleTransfer(bool /*in_isr*/, const uint8_t* req,
                                         uint16_t req_len, uint8_t* resp,
                                         uint16_t resp_cap, uint16_t& out_len)
{
  out_len = 0u;
  if (!req || !resp || resp_cap < 3u)
  {
    return ErrorCode::ARG_ERR;
  }

  if (dap_state_.debug_port == LibXR::USB::DapLinkV1Def::DebugPort::JTAG)
  {
    return HandleJtagTransfer(false, req, req_len, resp, resp_cap, out_len);
  }

  resp[0] = to_u8(LibXR::USB::DapLinkV1Def::CommandId::TRANSFER);
  resp[1] = 0u;
  resp[2] = 0u;
  uint16_t resp_off = 3u;

  if (req_len < 3u)
  {
    resp[2] = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
    out_len = 3u;
    return ErrorCode::ARG_ERR;
  }

  if (dap_state_.transfer_abort)
  {
    dap_state_.transfer_abort = false;
    resp[2] = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
    out_len = 3u;
    return ErrorCode::OK;
  }

  const uint8_t COUNT = req[2];
  uint16_t req_off = 3u;

  auto ack_to_dap = [&](LibXR::Debug::SwdProtocol::Ack ack) -> uint8_t
  {
    switch (ack)
    {
      case LibXR::Debug::SwdProtocol::Ack::OK:
        return LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK;
      case LibXR::Debug::SwdProtocol::Ack::WAIT:
        return LibXR::USB::DapLinkV1Def::DAP_TRANSFER_WAIT;
      case LibXR::Debug::SwdProtocol::Ack::FAULT:
        return LibXR::USB::DapLinkV1Def::DAP_TRANSFER_FAULT;
      default:
        return 0x07u;
    }
  };

  auto push_u32 = [&](uint32_t VALUE) -> bool
  {
    if (resp_off + 4u > resp_cap)
    {
      return false;
    }
    Memory::FastCopy(&resp[resp_off], &VALUE, sizeof(VALUE));
    resp_off = static_cast<uint16_t>(resp_off + 4u);
    return true;
  };

  auto push_timestamp = [&]() -> bool
  {
    const uint32_t T = static_cast<uint32_t>(LibXR::Timebase::GetMicroseconds());
    return push_u32(T);
  };

  auto ensure_space = [&](uint16_t bytes) -> bool
  { return (resp_off + bytes) <= resp_cap; };

  auto bytes_for_read = [&](bool need_ts) -> uint16_t
  { return static_cast<uint16_t>(need_ts ? 8u : 4u); };

  uint8_t response_count = 0u;
  uint8_t response_value = 0u;
  bool check_write = false;

  struct PendingApRead
  {
    bool valid = false;
    bool need_ts = false;
  } pending;

  auto emit_read_with_ts = [&](bool need_ts, uint32_t data) -> bool
  {
    if (need_ts)
    {
      if (!push_timestamp())
      {
        return false;
      }
    }
    if (!push_u32(data))
    {
      return false;
    }
    response_count++;
    return true;
  };

  auto complete_pending_by_rdbuff = [&]() -> bool
  {
    if (!pending.valid)
    {
      return true;
    }

    if (!ensure_space(bytes_for_read(pending.need_ts)))
    {
      response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
      return false;
    }

    uint32_t rdata = 0u;
    LibXR::Debug::SwdProtocol::Ack ack = LibXR::Debug::SwdProtocol::Ack::PROTOCOL;
    const ErrorCode EC = swd_.DpReadRdbuffTxn(rdata, ack);

    const uint8_t V = ack_to_dap(ack);
    if (V != LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK)
    {
      response_value = V;
      return false;
    }
    if (EC != ErrorCode::OK)
    {
      response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
      return false;
    }

    if (!emit_read_with_ts(pending.need_ts, rdata))
    {
      response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
      return false;
    }

    pending.valid = false;
    pending.need_ts = false;
    check_write = false;
    response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK;
    return true;
  };

  auto flush_pending_if_any = [&]() -> bool
  { return pending.valid ? complete_pending_by_rdbuff() : true; };

  for (uint32_t i = 0; i < COUNT; ++i)
  {
    if (req_off >= req_len)
    {
      response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
      break;
    }

    const uint8_t RQ = req[req_off++];

    const bool AP = LibXR::USB::DapLinkV1Def::req_is_ap(RQ);
    const bool RNW = LibXR::USB::DapLinkV1Def::req_is_read(RQ);
    const uint8_t ADDR2B = LibXR::USB::DapLinkV1Def::req_addr2b(RQ);

    const bool TS = LibXR::USB::DapLinkV1Def::req_need_timestamp(RQ);
    const bool MATCH_VALUE =
        ((RQ & LibXR::USB::DapLinkV1Def::DAP_TRANSFER_MATCH_VALUE) != 0u);
    const bool MATCH_MASK =
        ((RQ & LibXR::USB::DapLinkV1Def::DAP_TRANSFER_MATCH_MASK) != 0u);

    if (TS && (MATCH_VALUE || MATCH_MASK))
    {
      response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
      break;
    }

    LibXR::Debug::SwdProtocol::Ack ack = LibXR::Debug::SwdProtocol::Ack::PROTOCOL;
    ErrorCode EC = ErrorCode::OK;

    if (!RNW)
    {
      if (!flush_pending_if_any())
      {
        break;
      }

      if (req_off + 4u > req_len)
      {
        response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
        break;
      }

      uint32_t wdata = 0u;
      Memory::FastCopy(&wdata, &req[req_off], sizeof(wdata));
      req_off = static_cast<uint16_t>(req_off + 4u);

      if (MATCH_MASK)
      {
        match_mask_ = wdata;
        response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK;
        response_count++;
        continue;
      }

      if (AP)
      {
        EC = swd_.ApWriteTxn(ADDR2B, wdata, ack);
      }
      else
      {
        EC = swd_.DpWriteTxn(static_cast<LibXR::Debug::SwdProtocol::DpWriteReg>(ADDR2B),
                             wdata, ack);
      }

      response_value = ack_to_dap(ack);
      if (response_value != LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK)
      {
        break;
      }
      if (EC != ErrorCode::OK)
      {
        response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
        break;
      }

      if (TS)
      {
        if (!push_timestamp())
        {
          response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
          break;
        }
      }

      response_count++;
      check_write = true;
    }
    else
    {
      if (MATCH_VALUE)
      {
        if (!flush_pending_if_any())
        {
          break;
        }

        if (req_off + 4u > req_len)
        {
          response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
          break;
        }

        uint32_t match_val = 0u;
        Memory::FastCopy(&match_val, &req[req_off], sizeof(match_val));
        req_off = static_cast<uint16_t>(req_off + 4u);

        uint32_t rdata = 0u;
        uint32_t retry = dap_state_.transfer_cfg.match_retry;
        bool matched = false;

        while (true)
        {
          if (AP)
          {
            EC = swd_.ApReadTxn(ADDR2B, rdata, ack);
            if (EC == ErrorCode::OK && ack == LibXR::Debug::SwdProtocol::Ack::OK)
            {
              check_write = false;
            }
          }
          else
          {
            EC = swd_.DpReadTxn(static_cast<LibXR::Debug::SwdProtocol::DpReadReg>(ADDR2B),
                                rdata, ack);
          }

          response_value = ack_to_dap(ack);
          if (response_value != LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK)
          {
            break;
          }
          if (EC != ErrorCode::OK)
          {
            response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
            break;
          }

          if ((rdata & match_mask_) == (match_val & match_mask_))
          {
            matched = true;
            break;
          }

          if (retry == 0u)
          {
            break;
          }
          --retry;
        }

        if (response_value != LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK)
        {
          break;
        }

        if (!matched)
        {
          response_value =
              static_cast<uint8_t>(LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK |
                                   LibXR::USB::DapLinkV1Def::DAP_TRANSFER_MISMATCH);
          break;
        }

        response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK;
        response_count++;
        continue;
      }

      if (!AP)
      {
        if (!flush_pending_if_any())
        {
          break;
        }

        uint32_t rdata = 0u;
        EC = swd_.DpReadTxn(static_cast<LibXR::Debug::SwdProtocol::DpReadReg>(ADDR2B),
                            rdata, ack);

        response_value = ack_to_dap(ack);
        if (response_value != LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK)
        {
          break;
        }
        if (EC != ErrorCode::OK)
        {
          response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
          break;
        }

        if (!ensure_space(bytes_for_read(TS)))
        {
          response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
          break;
        }

        if (!emit_read_with_ts(TS, rdata))
        {
          response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
          break;
        }

        response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK;
        continue;
      }

      if (!pending.valid)
      {
        uint32_t dummy_posted = 0u;
        EC = swd_.ApReadPostedTxn(ADDR2B, dummy_posted, ack);

        response_value = ack_to_dap(ack);
        if (response_value != LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK)
        {
          break;
        }
        if (EC != ErrorCode::OK)
        {
          response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
          break;
        }

        pending.valid = true;
        pending.need_ts = TS;
        response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK;
      }
      else
      {
        if (!ensure_space(bytes_for_read(pending.need_ts)))
        {
          response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
          break;
        }

        uint32_t posted_prev = 0u;
        EC = swd_.ApReadPostedTxn(ADDR2B, posted_prev, ack);

        const uint8_t CUR_V = ack_to_dap(ack);
        if (CUR_V != LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK || EC != ErrorCode::OK)
        {
          const uint8_t PRIOR_FAIL =
              (CUR_V != LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK)
                  ? CUR_V
                  : LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;

          if (!complete_pending_by_rdbuff())
          {
            break;
          }

          response_value = PRIOR_FAIL;
          break;
        }

        if (!emit_read_with_ts(pending.need_ts, posted_prev))
        {
          response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
          break;
        }

        pending.valid = true;
        pending.need_ts = TS;
        response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK;
      }
    }

    if (dap_state_.transfer_abort)
    {
      dap_state_.transfer_abort = false;
      break;
    }
  }

  if (pending.valid)
  {
    const uint8_t PRIOR_FAIL = response_value;

    if (!complete_pending_by_rdbuff())
    {
    }
    else
    {
      if (PRIOR_FAIL != 0u && PRIOR_FAIL != LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK)
      {
        response_value = PRIOR_FAIL;
      }
    }
  }

  if (response_value == LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK && check_write)
  {
    uint32_t dummy = 0u;
    LibXR::Debug::SwdProtocol::Ack ack = LibXR::Debug::SwdProtocol::Ack::PROTOCOL;
    const ErrorCode EC = swd_.DpReadRdbuffTxn(dummy, ack);
    const uint8_t V = ack_to_dap(ack);

    if (V != LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK)
    {
      response_value = V;
    }
    else if (EC != ErrorCode::OK)
    {
      response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
    }
  }

  resp[1] = response_count;
  resp[2] = response_value;
  out_len = resp_off;
  return ErrorCode::OK;
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::HandleTransferBlock(bool /*in_isr*/, const uint8_t* req,
                                              uint16_t req_len, uint8_t* resp,
                                              uint16_t resp_cap, uint16_t& out_len)
{
  if (dap_state_.debug_port == LibXR::USB::DapLinkV1Def::DebugPort::JTAG)
  {
    return HandleJtagTransferBlock(false, req, req_len, resp, resp_cap, out_len);
  }

  if (!resp || resp_cap < 4u)
  {
    out_len = 0u;
    return ErrorCode::NOT_FOUND;
  }

  resp[0] = to_u8(LibXR::USB::DapLinkV1Def::CommandId::TRANSFER_BLOCK);
  resp[1] = 0u;
  resp[2] = 0u;
  resp[3] = 0u;
  out_len = 4u;

  if (!req || req_len < 5u)
  {
    resp[3] = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
    return ErrorCode::ARG_ERR;
  }

  if (dap_state_.transfer_abort)
  {
    dap_state_.transfer_abort = false;
    resp[3] = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
    return ErrorCode::OK;
  }

  uint16_t count = 0u;
  Memory::FastCopy(&count, &req[2], sizeof(count));

  const uint8_t DAP_RQ = req[4];

  if ((DAP_RQ & (LibXR::USB::DapLinkV1Def::DAP_TRANSFER_MATCH_VALUE |
                 LibXR::USB::DapLinkV1Def::DAP_TRANSFER_MATCH_MASK)) != 0u)
  {
    resp[3] = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
    return ErrorCode::NOT_SUPPORT;
  }
  if (LibXR::USB::DapLinkV1Def::req_need_timestamp(DAP_RQ))
  {
    resp[3] = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
    return ErrorCode::NOT_SUPPORT;
  }

  if (count == 0u)
  {
    const uint16_t DONE0 = 0u;
    Memory::FastCopy(&resp[1], &DONE0, sizeof(DONE0));
    resp[3] = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK;
    out_len = 4u;
    return ErrorCode::OK;
  }

  const bool AP = LibXR::USB::DapLinkV1Def::req_is_ap(DAP_RQ);
  const bool RNW = LibXR::USB::DapLinkV1Def::req_is_read(DAP_RQ);
  const uint8_t ADDR2B = LibXR::USB::DapLinkV1Def::req_addr2b(DAP_RQ);

  uint16_t done = 0u;
  uint8_t xresp = 0u;

  uint16_t req_off = 5u;
  uint16_t resp_off = 4u;

  if (!RNW)
  {
    for (uint32_t i = 0; i < count; ++i)
    {
      LibXR::Debug::SwdProtocol::Ack ack = LibXR::Debug::SwdProtocol::Ack::PROTOCOL;
      ErrorCode EC = ErrorCode::OK;

      if (req_off + 4u > req_len)
      {
        xresp |= LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
        break;
      }

      uint32_t wdata = 0u;
      Memory::FastCopy(&wdata, &req[req_off], sizeof(wdata));
      req_off = static_cast<uint16_t>(req_off + 4u);

      if (AP)
      {
        EC = swd_.ApWriteTxn(ADDR2B, wdata, ack);
      }
      else
      {
        EC = swd_.DpWriteTxn(static_cast<LibXR::Debug::SwdProtocol::DpWriteReg>(ADDR2B),
                             wdata, ack);
      }

      xresp = MapAckToDapResp(ack);
      if (xresp == 0u)
      {
        xresp |= LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
        break;
      }

      if (ack != LibXR::Debug::SwdProtocol::Ack::OK)
      {
        break;
      }

      if (EC != ErrorCode::OK)
      {
        xresp |= LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
        break;
      }

      done = static_cast<uint16_t>(i + 1u);
    }

    Memory::FastCopy(&resp[1], &done, sizeof(done));
    resp[3] = xresp;
    out_len = resp_off;
    return ErrorCode::OK;
  }

  if (!AP)
  {
    for (uint32_t i = 0; i < count; ++i)
    {
      LibXR::Debug::SwdProtocol::Ack ack = LibXR::Debug::SwdProtocol::Ack::PROTOCOL;
      ErrorCode EC = ErrorCode::OK;
      uint32_t rdata = 0u;

      if (resp_off + 4u > resp_cap)
      {
        xresp |= LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
        break;
      }

      EC = swd_.DpReadTxn(static_cast<LibXR::Debug::SwdProtocol::DpReadReg>(ADDR2B),
                          rdata, ack);

      xresp = MapAckToDapResp(ack);
      if (xresp == 0u)
      {
        xresp |= LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
        break;
      }

      if (ack != LibXR::Debug::SwdProtocol::Ack::OK)
      {
        break;
      }

      if (EC != ErrorCode::OK)
      {
        xresp |= LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
        break;
      }

      Memory::FastCopy(&resp[resp_off], &rdata, sizeof(rdata));
      resp_off = static_cast<uint16_t>(resp_off + 4u);
      done = static_cast<uint16_t>(i + 1u);
    }

    Memory::FastCopy(&resp[1], &done, sizeof(done));
    resp[3] = xresp;
    out_len = resp_off;
    return ErrorCode::OK;
  }

  {
    LibXR::Debug::SwdProtocol::Ack ack = LibXR::Debug::SwdProtocol::Ack::PROTOCOL;
    ErrorCode EC = ErrorCode::OK;

    uint32_t dummy_posted = 0u;
    EC = swd_.ApReadPostedTxn(ADDR2B, dummy_posted, ack);
    xresp = MapAckToDapResp(ack);

    if (xresp == 0u)
    {
      xresp |= LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
      goto out_ap_read;  // NOLINT
    }
    if (ack != LibXR::Debug::SwdProtocol::Ack::OK)
    {
      goto out_ap_read;  // NOLINT
    }
    if (EC != ErrorCode::OK)
    {
      xresp |= LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
      goto out_ap_read;  // NOLINT
    }

    for (uint32_t i = 1; i < count; ++i)
    {
      if (resp_off + 4u > resp_cap)
      {
        xresp |= LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
        goto out_ap_read;  // NOLINT
      }

      uint32_t posted_prev = 0u;
      EC = swd_.ApReadPostedTxn(ADDR2B, posted_prev, ack);
      const uint8_t CUR = MapAckToDapResp(ack);

      if (CUR == 0u)
      {
        xresp = CUR | LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
        goto out_ap_read;  // NOLINT
      }

      if (ack != LibXR::Debug::SwdProtocol::Ack::OK || EC != ErrorCode::OK)
      {
        if (resp_off + 4u <= resp_cap)
        {
          uint32_t last = 0u;
          LibXR::Debug::SwdProtocol::Ack ack2 = LibXR::Debug::SwdProtocol::Ack::PROTOCOL;
          const ErrorCode EC2 = swd_.DpReadRdbuffTxn(last, ack2);
          const uint8_t V2 = MapAckToDapResp(ack2);

          if (V2 == 1u && EC2 == ErrorCode::OK)
          {
            Memory::FastCopy(&resp[resp_off], &last, sizeof(last));
            resp_off = static_cast<uint16_t>(resp_off + 4u);
            done = static_cast<uint16_t>(i);
          }
          else
          {
            xresp = V2;
            if (EC2 != ErrorCode::OK)
            {
              xresp |= LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
            }
            goto out_ap_read;  // NOLINT
          }
        }

        xresp = CUR;
        if (EC != ErrorCode::OK)
        {
          xresp |= LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
        }
        goto out_ap_read;  // NOLINT
      }

      Memory::FastCopy(&resp[resp_off], &posted_prev, sizeof(posted_prev));
      resp_off = static_cast<uint16_t>(resp_off + 4u);
      done = static_cast<uint16_t>(i);
      xresp = CUR;
    }

    if (resp_off + 4u > resp_cap)
    {
      xresp |= LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
      goto out_ap_read;  // NOLINT
    }

    {
      uint32_t last = 0u;
      LibXR::Debug::SwdProtocol::Ack ack2 = LibXR::Debug::SwdProtocol::Ack::PROTOCOL;
      const ErrorCode EC2 = swd_.DpReadRdbuffTxn(last, ack2);
      const uint8_t V2 = MapAckToDapResp(ack2);

      xresp = V2;
      if (V2 != 1u)
      {
        goto out_ap_read;  // NOLINT
      }
      if (EC2 != ErrorCode::OK)
      {
        xresp |= LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
        goto out_ap_read;  // NOLINT
      }

      Memory::FastCopy(&resp[resp_off], &last, sizeof(last));
      resp_off = static_cast<uint16_t>(resp_off + 4u);
      done = count;
    }

  out_ap_read:
    Memory::FastCopy(&resp[1], &done, sizeof(done));
    resp[3] = xresp;
    out_len = resp_off;
    return ErrorCode::OK;
  }
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::HandleJtagTransfer(bool /*in_isr*/, const uint8_t* req,
                                             uint16_t req_len, uint8_t* resp,
                                             uint16_t resp_cap, uint16_t& out_len)
{
  out_len = 0u;
  if (!req || !resp || resp_cap < 3u)
  {
    return ErrorCode::ARG_ERR;
  }

  resp[0] = to_u8(LibXR::USB::DapLinkV1Def::CommandId::TRANSFER);
  resp[1] = 0u;
  resp[2] = 0u;
  uint16_t resp_off = 3u;

  if (req_len < 3u || jtag_ == nullptr || jtag_chain_.count == 0u)
  {
    resp[2] = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
    out_len = 3u;
    return ErrorCode::OK;
  }

  if (dap_state_.transfer_abort)
  {
    dap_state_.transfer_abort = false;
    resp[2] = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
    out_len = 3u;
    return ErrorCode::OK;
  }

  const uint8_t TAP_INDEX = req[1];
  if (TAP_INDEX >= jtag_chain_.count)
  {
    resp[2] = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
    out_len = 3u;
    return ErrorCode::OK;
  }

  jtag_chain_.index = TAP_INDEX;
  LibXR::Debug::JtagProtocol::UpdateChainCache(jtag_chain_);

  LibXR::Debug::JtagDp dp(*jtag_, &jtag_chain_);

  const uint8_t COUNT = req[2];
  uint16_t req_off = 3u;

  auto push_u32 = [&](uint32_t VALUE) -> bool
  {
    if (resp_off + 4u > resp_cap)
    {
      return false;
    }
    Memory::FastCopy(&resp[resp_off], &VALUE, sizeof(VALUE));
    resp_off = static_cast<uint16_t>(resp_off + 4u);
    return true;
  };

  auto push_timestamp = [&]() -> bool
  {
    const uint32_t T = static_cast<uint32_t>(LibXR::Timebase::GetMicroseconds());
    return push_u32(T);
  };

  auto ensure_space = [&](uint16_t bytes) -> bool
  { return (resp_off + bytes) <= resp_cap; };

  auto bytes_for_read = [&](bool need_ts) -> uint16_t
  { return static_cast<uint16_t>(need_ts ? 8u : 4u); };

  auto idle_after_attempt = [&]() {
    if (dap_state_.transfer_cfg.idle_cycles != 0u)
    {
      jtag_->IdleClocks(dap_state_.transfer_cfg.idle_cycles);
    }
  };

  auto dp_read_retry = [&](uint8_t addr2b, uint32_t& val,
                           LibXR::Debug::JtagProtocol::Ack& ack) -> ErrorCode
  {
    uint32_t retry = dap_state_.transfer_cfg.retry_count;
    while (true)
    {
      const ErrorCode EC = dp.DpReadTxn(addr2b, val, ack);
      idle_after_attempt();
      if (ack != LibXR::Debug::JtagProtocol::Ack::WAIT || retry == 0u ||
          dap_state_.transfer_abort)
      {
        return EC;
      }
      --retry;
    }
  };

  auto dp_write_retry = [&](uint8_t addr2b, uint32_t val,
                            LibXR::Debug::JtagProtocol::Ack& ack) -> ErrorCode
  {
    uint32_t retry = dap_state_.transfer_cfg.retry_count;
    while (true)
    {
      const ErrorCode EC = dp.DpWriteTxn(addr2b, val, ack);
      idle_after_attempt();
      if (ack != LibXR::Debug::JtagProtocol::Ack::WAIT || retry == 0u ||
          dap_state_.transfer_abort)
      {
        return EC;
      }
      --retry;
    }
  };

  auto ap_read_retry = [&](uint8_t addr2b, uint32_t& val,
                           LibXR::Debug::JtagProtocol::Ack& ack) -> ErrorCode
  {
    uint32_t retry = dap_state_.transfer_cfg.retry_count;
    while (true)
    {
      const ErrorCode EC = dp.ApReadTxn(addr2b, val, ack);
      idle_after_attempt();
      if (ack != LibXR::Debug::JtagProtocol::Ack::WAIT || retry == 0u ||
          dap_state_.transfer_abort)
      {
        return EC;
      }
      --retry;
    }
  };

  auto ap_write_retry = [&](uint8_t addr2b, uint32_t val,
                            LibXR::Debug::JtagProtocol::Ack& ack) -> ErrorCode
  {
    uint32_t retry = dap_state_.transfer_cfg.retry_count;
    while (true)
    {
      const ErrorCode EC = dp.ApWriteTxn(addr2b, val, ack);
      idle_after_attempt();
      if (ack != LibXR::Debug::JtagProtocol::Ack::WAIT || retry == 0u ||
          dap_state_.transfer_abort)
      {
        return EC;
      }
      --retry;
    }
  };

  uint8_t response_count = 0u;
  uint8_t response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK;
  bool check_write = false;

  auto emit_read_with_ts = [&](bool need_ts, uint32_t data) -> bool
  {
    if (need_ts)
    {
      if (!push_timestamp())
      {
        return false;
      }
    }
    if (!push_u32(data))
    {
      return false;
    }
    response_count++;
    return true;
  };

  for (uint32_t i = 0; i < COUNT; ++i)
  {
    if (req_off >= req_len)
    {
      response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
      break;
    }

    const uint8_t RQ = req[req_off++];
    const bool AP = LibXR::USB::DapLinkV1Def::req_is_ap(RQ);
    const bool RNW = LibXR::USB::DapLinkV1Def::req_is_read(RQ);
    const uint8_t ADDR2B = LibXR::USB::DapLinkV1Def::req_addr2b(RQ);

    const bool TS = LibXR::USB::DapLinkV1Def::req_need_timestamp(RQ);
    const bool MATCH_VALUE =
        ((RQ & LibXR::USB::DapLinkV1Def::DAP_TRANSFER_MATCH_VALUE) != 0u);
    const bool MATCH_MASK =
        ((RQ & LibXR::USB::DapLinkV1Def::DAP_TRANSFER_MATCH_MASK) != 0u);

    if (TS && (MATCH_VALUE || MATCH_MASK))
    {
      response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
      break;
    }

    LibXR::Debug::JtagProtocol::Ack ack = LibXR::Debug::JtagProtocol::Ack::PROTOCOL;
    ErrorCode EC = ErrorCode::OK;

    if (!RNW)
    {
      if (req_off + 4u > req_len)
      {
        response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
        break;
      }

      uint32_t wdata = 0u;
      Memory::FastCopy(&wdata, &req[req_off], sizeof(wdata));
      req_off = static_cast<uint16_t>(req_off + 4u);

      if (MATCH_MASK)
      {
        match_mask_ = wdata;
        response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK;
        response_count++;
        continue;
      }

      if (AP)
      {
        EC = ap_write_retry(ADDR2B, wdata, ack);
      }
      else
      {
        EC = dp_write_retry(ADDR2B, wdata, ack);
      }

      response_value = MapAckToDapResp(ack);
      if (response_value != LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK)
      {
        break;
      }
      if (EC != ErrorCode::OK)
      {
        response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
        break;
      }

      if (TS)
      {
        if (!push_timestamp())
        {
          response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
          break;
        }
      }

      response_count++;
      check_write = true;
    }
    else
    {
      if (MATCH_VALUE)
      {
        if (req_off + 4u > req_len)
        {
          response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
          break;
        }

        uint32_t match_val = 0u;
        Memory::FastCopy(&match_val, &req[req_off], sizeof(match_val));
        req_off = static_cast<uint16_t>(req_off + 4u);

        uint32_t rdata = 0u;
        uint32_t retry = dap_state_.transfer_cfg.match_retry;
        bool matched = false;

        while (true)
        {
          if (AP)
          {
            EC = ap_read_retry(ADDR2B, rdata, ack);
          }
          else
          {
            EC = dp_read_retry(ADDR2B, rdata, ack);
          }

          response_value = MapAckToDapResp(ack);
          if (response_value != LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK)
          {
            break;
          }
          if (EC != ErrorCode::OK)
          {
            response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
            break;
          }

          if ((rdata & match_mask_) == (match_val & match_mask_))
          {
            matched = true;
            break;
          }

          if (retry == 0u)
          {
            break;
          }
          --retry;
        }

        if (response_value != LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK)
        {
          break;
        }

        if (!matched)
        {
          response_value =
              static_cast<uint8_t>(LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK |
                                   LibXR::USB::DapLinkV1Def::DAP_TRANSFER_MISMATCH);
          break;
        }

        response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK;
        response_count++;
        continue;
      }

      uint32_t rdata = 0u;
      if (AP)
      {
        EC = ap_read_retry(ADDR2B, rdata, ack);
      }
      else
      {
        EC = dp_read_retry(ADDR2B, rdata, ack);
      }

      response_value = MapAckToDapResp(ack);
      if (response_value != LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK)
      {
        break;
      }
      if (EC != ErrorCode::OK)
      {
        response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
        break;
      }

      if (!ensure_space(bytes_for_read(TS)))
      {
        response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
        break;
      }

      if (!emit_read_with_ts(TS, rdata))
      {
        response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
        break;
      }

      response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK;
    }

    if (dap_state_.transfer_abort)
    {
      dap_state_.transfer_abort = false;
      break;
    }
  }

  if (response_value == LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK && check_write)
  {
    uint32_t dummy = 0u;
    LibXR::Debug::JtagProtocol::Ack ack = LibXR::Debug::JtagProtocol::Ack::PROTOCOL;
    const ErrorCode EC = dp_read_retry(3u, dummy, ack);
    const uint8_t V = MapAckToDapResp(ack);

    if (V != LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK)
    {
      response_value = V;
    }
    else if (EC != ErrorCode::OK)
    {
      response_value = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
    }
  }

  resp[1] = response_count;
  resp[2] = response_value;
  out_len = resp_off;
  return ErrorCode::OK;
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::HandleJtagTransferBlock(bool /*in_isr*/,
                                                  const uint8_t* req, uint16_t req_len,
                                                  uint8_t* resp, uint16_t resp_cap,
                                                  uint16_t& out_len)
{
  if (!resp || resp_cap < 4u)
  {
    out_len = 0u;
    return ErrorCode::NOT_FOUND;
  }

  resp[0] = to_u8(LibXR::USB::DapLinkV1Def::CommandId::TRANSFER_BLOCK);
  resp[1] = 0u;
  resp[2] = 0u;
  resp[3] = 0u;
  out_len = 4u;

  if (!req || req_len < 5u || jtag_ == nullptr || jtag_chain_.count == 0u)
  {
    resp[3] = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
    return ErrorCode::ARG_ERR;
  }

  if (dap_state_.transfer_abort)
  {
    dap_state_.transfer_abort = false;
    resp[3] = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
    return ErrorCode::OK;
  }

  const uint8_t TAP_INDEX = req[1];
  if (TAP_INDEX >= jtag_chain_.count)
  {
    resp[3] = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
    return ErrorCode::OK;
  }

  jtag_chain_.index = TAP_INDEX;
  LibXR::Debug::JtagProtocol::UpdateChainCache(jtag_chain_);

  uint16_t count = 0u;
  Memory::FastCopy(&count, &req[2], sizeof(count));

  const uint8_t DAP_RQ = req[4];

  if ((DAP_RQ & (LibXR::USB::DapLinkV1Def::DAP_TRANSFER_MATCH_VALUE |
                 LibXR::USB::DapLinkV1Def::DAP_TRANSFER_MATCH_MASK)) != 0u)
  {
    resp[3] = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
    return ErrorCode::NOT_SUPPORT;
  }
  if (LibXR::USB::DapLinkV1Def::req_need_timestamp(DAP_RQ))
  {
    resp[3] = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
    return ErrorCode::NOT_SUPPORT;
  }

  if (count == 0u)
  {
    const uint16_t DONE0 = 0u;
    Memory::FastCopy(&resp[1], &DONE0, sizeof(DONE0));
    resp[3] = LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK;
    out_len = 4u;
    return ErrorCode::OK;
  }

  LibXR::Debug::JtagDp dp(*jtag_, &jtag_chain_);

  auto idle_after_attempt = [&]() {
    if (dap_state_.transfer_cfg.idle_cycles != 0u)
    {
      jtag_->IdleClocks(dap_state_.transfer_cfg.idle_cycles);
    }
  };

  auto dp_read_retry = [&](uint8_t addr2b, uint32_t& val,
                           LibXR::Debug::JtagProtocol::Ack& ack) -> ErrorCode
  {
    uint32_t retry = dap_state_.transfer_cfg.retry_count;
    while (true)
    {
      const ErrorCode EC = dp.DpReadTxn(addr2b, val, ack);
      idle_after_attempt();
      if (ack != LibXR::Debug::JtagProtocol::Ack::WAIT || retry == 0u ||
          dap_state_.transfer_abort)
      {
        return EC;
      }
      --retry;
    }
  };

  auto dp_write_retry = [&](uint8_t addr2b, uint32_t val,
                            LibXR::Debug::JtagProtocol::Ack& ack) -> ErrorCode
  {
    uint32_t retry = dap_state_.transfer_cfg.retry_count;
    while (true)
    {
      const ErrorCode EC = dp.DpWriteTxn(addr2b, val, ack);
      idle_after_attempt();
      if (ack != LibXR::Debug::JtagProtocol::Ack::WAIT || retry == 0u ||
          dap_state_.transfer_abort)
      {
        return EC;
      }
      --retry;
    }
  };

  auto ap_read_retry = [&](uint8_t addr2b, uint32_t& val,
                           LibXR::Debug::JtagProtocol::Ack& ack) -> ErrorCode
  {
    uint32_t retry = dap_state_.transfer_cfg.retry_count;
    while (true)
    {
      const ErrorCode EC = dp.ApReadTxn(addr2b, val, ack);
      idle_after_attempt();
      if (ack != LibXR::Debug::JtagProtocol::Ack::WAIT || retry == 0u ||
          dap_state_.transfer_abort)
      {
        return EC;
      }
      --retry;
    }
  };

  auto ap_write_retry = [&](uint8_t addr2b, uint32_t val,
                            LibXR::Debug::JtagProtocol::Ack& ack) -> ErrorCode
  {
    uint32_t retry = dap_state_.transfer_cfg.retry_count;
    while (true)
    {
      const ErrorCode EC = dp.ApWriteTxn(addr2b, val, ack);
      idle_after_attempt();
      if (ack != LibXR::Debug::JtagProtocol::Ack::WAIT || retry == 0u ||
          dap_state_.transfer_abort)
      {
        return EC;
      }
      --retry;
    }
  };

  const bool AP = LibXR::USB::DapLinkV1Def::req_is_ap(DAP_RQ);
  const bool RNW = LibXR::USB::DapLinkV1Def::req_is_read(DAP_RQ);
  const uint8_t ADDR2B = LibXR::USB::DapLinkV1Def::req_addr2b(DAP_RQ);

  uint16_t done = 0u;
  uint8_t xresp = 0u;

  uint16_t req_off = 5u;
  uint16_t resp_off = 4u;

  if (!RNW)
  {
    for (uint32_t i = 0; i < count; ++i)
    {
      LibXR::Debug::JtagProtocol::Ack ack = LibXR::Debug::JtagProtocol::Ack::PROTOCOL;
      ErrorCode EC = ErrorCode::OK;

      if (req_off + 4u > req_len)
      {
        xresp |= LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
        break;
      }

      uint32_t wdata = 0u;
      Memory::FastCopy(&wdata, &req[req_off], sizeof(wdata));
      req_off = static_cast<uint16_t>(req_off + 4u);

      if (AP)
      {
        EC = ap_write_retry(ADDR2B, wdata, ack);
      }
      else
      {
        EC = dp_write_retry(ADDR2B, wdata, ack);
      }

      xresp = MapAckToDapResp(ack);
      if (xresp == 0u)
      {
        xresp |= LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
        break;
      }

      if (ack != LibXR::Debug::JtagProtocol::Ack::OK)
      {
        break;
      }

      if (EC != ErrorCode::OK)
      {
        xresp |= LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
        break;
      }

      done = static_cast<uint16_t>(i + 1u);
    }

    if (xresp == LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK && done > 0u)
    {
      uint32_t dummy = 0u;
      LibXR::Debug::JtagProtocol::Ack ack = LibXR::Debug::JtagProtocol::Ack::PROTOCOL;
      const ErrorCode EC = dp_read_retry(3u, dummy, ack);
      const uint8_t V = MapAckToDapResp(ack);
      if (V != LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK)
      {
        xresp = V;
      }
      else if (EC != ErrorCode::OK)
      {
        xresp |= LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
      }
    }

    Memory::FastCopy(&resp[1], &done, sizeof(done));
    resp[3] = xresp;
    out_len = resp_off;
    return ErrorCode::OK;
  }

  for (uint32_t i = 0; i < count; ++i)
  {
    LibXR::Debug::JtagProtocol::Ack ack = LibXR::Debug::JtagProtocol::Ack::PROTOCOL;
    ErrorCode EC = ErrorCode::OK;
    uint32_t rdata = 0u;

    if (resp_off + 4u > resp_cap)
    {
      xresp |= LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
      break;
    }

    if (AP)
    {
      EC = ap_read_retry(ADDR2B, rdata, ack);
    }
    else
    {
      EC = dp_read_retry(ADDR2B, rdata, ack);
    }

    xresp = MapAckToDapResp(ack);
    if (xresp == 0u)
    {
      xresp |= LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
      break;
    }

    if (ack != LibXR::Debug::JtagProtocol::Ack::OK)
    {
      break;
    }

    if (EC != ErrorCode::OK)
    {
      xresp |= LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
      break;
    }

    Memory::FastCopy(&resp[resp_off], &rdata, sizeof(rdata));
    resp_off = static_cast<uint16_t>(resp_off + 4u);
    done = static_cast<uint16_t>(i + 1u);
  }

  Memory::FastCopy(&resp[1], &done, sizeof(done));
  resp[3] = xresp;
  out_len = resp_off;
  return ErrorCode::OK;
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::HandleJtagWriteAbort(bool /*in_isr*/, const uint8_t* req,
                                               uint16_t req_len, uint8_t* resp,
                                               uint16_t resp_cap, uint16_t& out_len)
{
  if (!resp || resp_cap < 2u)
  {
    out_len = 0u;
    return ErrorCode::ARG_ERR;
  }

  resp[0] = to_u8(LibXR::USB::DapLinkV1Def::CommandId::WRITE_ABORT);
  resp[1] = to_u8(LibXR::USB::DapLinkV1Def::Status::ERROR);
  out_len = 2u;

  if (!req || req_len < 6u || jtag_ == nullptr || jtag_chain_.count == 0u)
  {
    return ErrorCode::ARG_ERR;
  }

  const uint8_t TAP_INDEX = req[1];
  if (TAP_INDEX >= jtag_chain_.count)
  {
    return ErrorCode::OK;
  }

  jtag_chain_.index = TAP_INDEX;
  LibXR::Debug::JtagProtocol::UpdateChainCache(jtag_chain_);

  uint32_t flags = 0u;
  Memory::FastCopy(&flags, &req[2], sizeof(flags));

  LibXR::Debug::JtagDp dp(*jtag_, &jtag_chain_);
  LibXR::Debug::JtagProtocol::Ack ack = LibXR::Debug::JtagProtocol::Ack::PROTOCOL;
  const ErrorCode EC = dp.WriteAbort(flags, ack);
  const uint8_t V = MapAckToDapResp(ack);

  if (EC == ErrorCode::OK && V == LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK)
  {
    resp[1] = to_u8(LibXR::USB::DapLinkV1Def::Status::OK);
  }

  return ErrorCode::OK;
}

template <typename SwdPort>
void DapLinkV1Class<SwdPort>::DriveReset(bool release)
{
  last_nreset_level_high_ = release;

  if (release)
  {
    swj_shadow_ |= LibXR::USB::DapLinkV1Def::DAP_SWJ_NRESET;
  }
  else
  {
    swj_shadow_ = static_cast<uint8_t>(
        swj_shadow_ & static_cast<uint8_t>(~LibXR::USB::DapLinkV1Def::DAP_SWJ_NRESET));
  }

  if (nreset_gpio_ != nullptr)
  {
    (void)nreset_gpio_->Write(release);
  }
}

template <typename SwdPort>
void DapLinkV1Class<SwdPort>::DelayUsIfAllowed(bool /*in_isr*/, uint32_t us)
{
  LibXR::Timebase::DelayMicroseconds(us);
}

}  // namespace LibXR::USB
