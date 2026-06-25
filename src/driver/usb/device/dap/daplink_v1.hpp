#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "daplink_v1_def.hpp"
#include "debug/swd.hpp"
#include "gpio.hpp"
#include "hid.hpp"
#include "libxr_def.hpp"
#include "libxr_mem.hpp"
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
  const LibXR::USB::DapLinkV1Def::State& GetState() const;
  bool IsInited() const;

 protected:
  ErrorCode WriteDeviceDescriptor(DeviceDescriptor& header) override;
  ConstRawData GetReportDesc() override;

  void BindEndpoints(EndpointPool& endpoint_pool, uint8_t start_itf_num,
                     bool in_isr) override;
  void UnbindEndpoints(EndpointPool& endpoint_pool, bool in_isr) override;

  void OnDataOutComplete(bool in_isr, LibXR::ConstRawData& data) override;
  void OnDataInComplete(bool in_isr, LibXR::ConstRawData& data) override;
  ErrorCode OnSetReportData(bool in_isr, LibXR::ConstRawData& data) override;
  ErrorCode OnGetInputReport(uint8_t report_id,
                             DeviceClass::ControlTransferResult& result) override;
  ErrorCode OnGetFeatureReport(uint8_t report_id,
                               DeviceClass::ControlTransferResult& result) override;

 private:
  static void OnDataOutCompleteStatic(bool in_isr, DapLinkV1Class* self,
                                      LibXR::ConstRawData& data);
  static void OnDataInCompleteStatic(bool in_isr, DapLinkV1Class* self,
                                     LibXR::ConstRawData& data);

  uint16_t GetDapPacketSize() const;
  uint16_t ClipResponseLength(uint16_t len, uint16_t cap) const;
  static constexpr uint8_t NextRespQueueIndex(uint8_t idx);
  void ResetResponseQueue();
  bool HasDeferredResponseInEpBuffer() const;
  void SetDeferredResponseInEpBuffer(uint16_t len);
  bool SubmitDeferredResponseIfIdle();
  bool IsResponseQueueEmpty() const;
  bool IsResponseQueueFull() const;
  bool TryBuildAndEnqueueResponse(bool in_isr, const uint8_t* req, uint16_t req_len);
  uint8_t OutstandingResponseCount() const;
  bool EnqueueResponse(const uint8_t* data, uint16_t len);
  bool SubmitNextQueuedResponseIfIdle();
  void ArmOutTransferIfIdle();
  uint16_t PrepareResponseReport(uint8_t* resp, uint16_t payload_len, uint16_t cap);
  void UpdateControlInputReport(const uint8_t* resp, uint16_t len);
  ErrorCode HandleControlReportRequest(bool in_isr, const uint8_t* req, uint16_t req_len);
  void HandleHostRequest(bool in_isr, const uint8_t* req, uint16_t req_len);

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

  ErrorCode BuildInfoStringResponse(uint8_t cmd, const char* str, uint8_t* resp,
                                    uint16_t resp_cap, uint16_t& out_len);
  ErrorCode BuildInfoU8Response(uint8_t cmd, uint8_t val, uint8_t* resp,
                                uint16_t resp_cap, uint16_t& out_len);
  ErrorCode BuildInfoU16Response(uint8_t cmd, uint16_t val, uint8_t* resp,
                                 uint16_t resp_cap, uint16_t& out_len);
  ErrorCode BuildInfoU32Response(uint8_t cmd, uint32_t val, uint8_t* resp,
                                 uint16_t resp_cap, uint16_t& out_len);

  uint8_t MapAckToDapResp(LibXR::Debug::SwdProtocol::Ack ack) const;
  void SetTransferAbortFlag(bool on);

  void DriveReset(bool release);
  void DelayUsIfAllowed(bool in_isr, uint32_t us);

 private:
  template <typename E>
  static constexpr uint8_t ToU8(E e)
  {
    return static_cast<uint8_t>(e);
  }

  static constexpr uint8_t DAP_OK = 0x00u;
  static constexpr uint8_t DAP_ERROR = 0xFFu;

  static inline ErrorCode BuildUnknowCmdResponse(uint8_t* resp, uint16_t cap,
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

  static constexpr uint16_t MAX_REQ = DapLinkV1Def::MAX_REQUEST_SIZE;
  static constexpr uint16_t MAX_RESP = DapLinkV1Def::MAX_RESPONSE_SIZE;
  static constexpr uint16_t DEFAULT_DAP_PACKET_SIZE = MAX_RESP;
  static constexpr uint8_t PACKET_COUNT_ADVERTISED = 1u;
  static constexpr uint8_t MAX_OUTSTANDING_RESPONSES = PACKET_COUNT_ADVERTISED;
  static constexpr uint16_t MAX_DAP_PACKET_SIZE = MAX_RESP;
  static constexpr uint16_t RESP_SLOT_SIZE = MAX_DAP_PACKET_SIZE;
  static constexpr uint8_t RESP_QUEUE_DEPTH = PACKET_COUNT_ADVERTISED;
  static_assert(RESP_SLOT_SIZE >= DEFAULT_DAP_PACKET_SIZE,
                "RESP_SLOT_SIZE must cover HID packet size");
  static_assert((RESP_QUEUE_DEPTH & (RESP_QUEUE_DEPTH - 1u)) == 0u,
                "Response queue depth must be power-of-two");

  struct ResponseSlot
  {
    uint16_t len = 0u;
    uint8_t payload[RESP_SLOT_SIZE] = {};
  };

  SwdPort& swd_;
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
  ResponseSlot resp_q_[RESP_QUEUE_DEPTH] = {};
  uint8_t resp_q_head_ = 0u;
  uint8_t resp_q_tail_ = 0u;
  uint8_t resp_q_count_ = 0u;
  bool deferred_in_resp_valid_ = false;
  uint16_t deferred_in_resp_len_ = 0u;
  uint8_t control_input_report_[MAX_DAP_PACKET_SIZE] = {};
  uint16_t control_input_report_len_ = DEFAULT_DAP_PACKET_SIZE;
};

template <typename SwdPort>
DapLinkV1Class<SwdPort>::DapLinkV1Class(SwdPort& swd_link, LibXR::GPIO* nreset_gpio,
                                        Endpoint::EPNumber in_ep_num,
                                        Endpoint::EPNumber out_ep_num)
    : HID(true, 1, 1, in_ep_num, out_ep_num), swd_(swd_link), nreset_gpio_(nreset_gpio)
{
  (void)swd_.SetClockHz(swj_clock_hz_);
}

template <typename SwdPort>
void DapLinkV1Class<SwdPort>::SetInfoStrings(const InfoStrings& info)
{
  info_ = info;
}

template <typename SwdPort>
const LibXR::USB::DapLinkV1Def::State& DapLinkV1Class<SwdPort>::GetState() const
{
  return dap_state_;
}

template <typename SwdPort>
bool DapLinkV1Class<SwdPort>::IsInited() const
{
  return inited_;
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::WriteDeviceDescriptor(DeviceDescriptor& header)
{
  header.data_.bDeviceClass = DeviceDescriptor::ClassID::PER_INTERFACE;
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
void DapLinkV1Class<SwdPort>::BindEndpoints(EndpointPool& endpoint_pool,
                                            uint8_t start_itf_num, bool in_isr)
{
  HID::BindEndpoints(endpoint_pool, start_itf_num, in_isr);

  ep_in_ = GetInEndpoint();
  ep_out_ = GetOutEndpoint();

  if (ep_in_ != nullptr)
  {
    ep_in_->Configure(
        {Endpoint::Direction::IN, Endpoint::Type::INTERRUPT, MAX_RESP, true});
    ep_in_->SetOnTransferCompleteCallback(on_data_in_cb_);
  }

  if (ep_out_ != nullptr)
  {
    ep_out_->Configure(
        {Endpoint::Direction::OUT, Endpoint::Type::INTERRUPT, MAX_REQ, true});
    ep_out_->SetOnTransferCompleteCallback(on_data_out_cb_);
  }

  inited_ = true;
  match_mask_ = 0xFFFFFFFFu;

  dap_state_ = {};
  dap_state_.debug_port = LibXR::USB::DapLinkV1Def::DebugPort::DISABLED;
  dap_state_.transfer_abort = false;
  ResetResponseQueue();
  if (ep_in_ != nullptr)
  {
    ep_in_->SetActiveLength(0);
  }

  swj_clock_hz_ = 1000000u;
  (void)swd_.SetClockHz(swj_clock_hz_);

  last_nreset_level_high_ = true;
  swj_shadow_ = static_cast<uint8_t>(DapLinkV1Def::DAP_SWJ_SWDIO_TMS |
                                     DapLinkV1Def::DAP_SWJ_NRESET);

  ArmOutTransferIfIdle();
}

template <typename SwdPort>
void DapLinkV1Class<SwdPort>::UnbindEndpoints(EndpointPool& endpoint_pool, bool in_isr)
{
  inited_ = false;
  ResetResponseQueue();

  dap_state_.debug_port = LibXR::USB::DapLinkV1Def::DebugPort::DISABLED;
  dap_state_.transfer_abort = false;

  if (ep_in_ != nullptr)
  {
    ep_in_->SetActiveLength(0);
  }
  if (ep_out_ != nullptr)
  {
    ep_out_->SetActiveLength(0);
  }

  HID::UnbindEndpoints(endpoint_pool, in_isr);

  ep_in_ = nullptr;
  ep_out_ = nullptr;

  swd_.Close();

  last_nreset_level_high_ = true;
  swj_shadow_ = static_cast<uint8_t>(DapLinkV1Def::DAP_SWJ_SWDIO_TMS |
                                     DapLinkV1Def::DAP_SWJ_NRESET);
}

template <typename SwdPort>
void DapLinkV1Class<SwdPort>::OnDataOutCompleteStatic(bool in_isr, DapLinkV1Class* self,
                                                      LibXR::ConstRawData& data)
{
  if (self && self->inited_)
  {
    self->OnDataOutComplete(in_isr, data);
  }
}

template <typename SwdPort>
void DapLinkV1Class<SwdPort>::OnDataInCompleteStatic(bool in_isr, DapLinkV1Class* self,
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

  ArmOutTransferIfIdle();
  HandleHostRequest(in_isr, static_cast<const uint8_t*>(data.addr_),
                    static_cast<uint16_t>(data.size_));
}

template <typename SwdPort>
void DapLinkV1Class<SwdPort>::OnDataInComplete(bool /*in_isr*/,
                                               LibXR::ConstRawData& /*data*/)
{
  (void)SubmitDeferredResponseIfIdle();
  (void)SubmitNextQueuedResponseIfIdle();
  ArmOutTransferIfIdle();
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::OnSetReportData(bool in_isr, LibXR::ConstRawData& data)
{
  (void)HID<sizeof(DAPLINK_V1_REPORT_DESC), DapLinkV1Def::MAX_REQUEST_SIZE,
            DapLinkV1Def::MAX_RESPONSE_SIZE>::OnSetReportData(in_isr, data);
  const auto* req = static_cast<const uint8_t*>(data.addr_);
  uint16_t req_len = static_cast<uint16_t>(data.size_);
  return HandleControlReportRequest(in_isr, req, req_len);
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::HandleControlReportRequest(bool in_isr,
                                                              const uint8_t* req,
                                                              uint16_t req_len)
{
  if (!req || req_len == 0u)
  {
    control_input_report_len_ = 0u;
    return ErrorCode::ARG_ERR;
  }

  uint16_t out_len = 0u;
  const ErrorCode ANS = ProcessOneCommand(in_isr, req, req_len, control_input_report_,
                                          MAX_DAP_PACKET_SIZE, out_len);

  out_len = ClipResponseLength(out_len, MAX_DAP_PACKET_SIZE);

  uint16_t tx_len = GetDapPacketSize();
  if (tx_len == 0u || tx_len > MAX_DAP_PACKET_SIZE)
  {
    tx_len = MAX_DAP_PACKET_SIZE;
  }
  if (tx_len < out_len)
  {
    tx_len = out_len;
  }
  if (tx_len > out_len)
  {
    Memory::FastSet(control_input_report_ + out_len, 0, tx_len - out_len);
  }

  control_input_report_len_ = tx_len;
  return ANS;
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::OnGetInputReport(
    uint8_t /*report_id*/, DeviceClass::ControlTransferResult& result)
{
  const uint16_t TX_LEN =
      (control_input_report_len_ > 0u) ? control_input_report_len_ : GetDapPacketSize();
  result.write_data = ConstRawData{control_input_report_, TX_LEN};
  return ErrorCode::OK;
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::OnGetFeatureReport(
    uint8_t /*report_id*/, DeviceClass::ControlTransferResult& result)
{
  const uint16_t TX_LEN =
      (control_input_report_len_ > 0u) ? control_input_report_len_ : GetDapPacketSize();
  result.write_data = ConstRawData{control_input_report_, TX_LEN};
  return ErrorCode::OK;
}

template <typename SwdPort>
uint16_t DapLinkV1Class<SwdPort>::PrepareResponseReport(uint8_t* resp,
                                                        uint16_t payload_len,
                                                        uint16_t cap)
{
  if (!resp || cap == 0u)
  {
    control_input_report_len_ = 0u;
    return 0u;
  }

  payload_len = ClipResponseLength(payload_len, cap);

  uint16_t tx_len = GetDapPacketSize();
  if (tx_len == 0u || tx_len > cap)
  {
    tx_len = cap;
  }
  if (tx_len < payload_len)
  {
    tx_len = payload_len;
  }
  if (tx_len > payload_len)
  {
    Memory::FastSet(resp + payload_len, 0, tx_len - payload_len);
  }

  UpdateControlInputReport(resp, tx_len);
  return tx_len;
}

template <typename SwdPort>
void DapLinkV1Class<SwdPort>::UpdateControlInputReport(const uint8_t* resp, uint16_t len)
{
  if (!resp)
  {
    control_input_report_len_ = 0u;
    return;
  }

  if (len > MAX_DAP_PACKET_SIZE)
  {
    len = MAX_DAP_PACKET_SIZE;
  }

  if (len > 0u)
  {
    Memory::FastCopy(control_input_report_, resp, len);
  }
  if (len < MAX_DAP_PACKET_SIZE)
  {
    Memory::FastSet(control_input_report_ + len, 0, MAX_DAP_PACKET_SIZE - len);
  }

  control_input_report_len_ = len;
}

template <typename SwdPort>
void DapLinkV1Class<SwdPort>::HandleHostRequest(bool in_isr, const uint8_t* req,
                                                uint16_t req_len)
{
  if (!inited_ || !ep_in_)
  {
    return;
  }

  if (!req || req_len == 0u)
  {
    ArmOutTransferIfIdle();
    return;
  }

  if (!HasDeferredResponseInEpBuffer() && IsResponseQueueEmpty() &&
      ep_in_->GetState() == Endpoint::State::IDLE)
  {
    auto tx_buff = ep_in_->GetBuffer();
    if (tx_buff.addr_ && tx_buff.size_ > 0u)
    {
      auto* tx_buf = static_cast<uint8_t*>(tx_buff.addr_);
      uint16_t out_len = 0u;
      const auto ANS = ProcessOneCommand(in_isr, req, req_len, tx_buf,
                                         static_cast<uint16_t>(tx_buff.size_), out_len);
      UNUSED(ANS);

      out_len = ClipResponseLength(out_len, static_cast<uint16_t>(tx_buff.size_));
      const uint16_t TX_LEN =
          PrepareResponseReport(tx_buf, out_len, static_cast<uint16_t>(tx_buff.size_));
      if (TX_LEN > 0u && ep_in_->Transfer(TX_LEN) == ErrorCode::OK)
      {
        return;
      }

      if (!EnqueueResponse(tx_buf, out_len))
      {
        (void)SubmitNextQueuedResponseIfIdle();
        (void)EnqueueResponse(tx_buf, out_len);
      }

      (void)SubmitNextQueuedResponseIfIdle();
      ArmOutTransferIfIdle();
      return;
    }
  }

  if (!HasDeferredResponseInEpBuffer() && IsResponseQueueEmpty() &&
      ep_in_->GetState() == Endpoint::State::BUSY)
  {
    auto tx_buff = ep_in_->GetBuffer();
    if (tx_buff.addr_ && tx_buff.size_ > 0u)
    {
      auto* tx_buf = static_cast<uint8_t*>(tx_buff.addr_);
      uint16_t out_len = 0u;
      const auto ANS = ProcessOneCommand(in_isr, req, req_len, tx_buf,
                                         static_cast<uint16_t>(tx_buff.size_), out_len);
      UNUSED(ANS);

      out_len = ClipResponseLength(out_len, static_cast<uint16_t>(tx_buff.size_));
      const uint16_t TX_LEN =
          PrepareResponseReport(tx_buf, out_len, static_cast<uint16_t>(tx_buff.size_));
      SetDeferredResponseInEpBuffer(TX_LEN);
      ArmOutTransferIfIdle();
      return;
    }
  }

  if (TryBuildAndEnqueueResponse(in_isr, req, req_len))
  {
    (void)SubmitDeferredResponseIfIdle();
    (void)SubmitNextQueuedResponseIfIdle();
    ArmOutTransferIfIdle();
    return;
  }

  (void)SubmitNextQueuedResponseIfIdle();
  (void)TryBuildAndEnqueueResponse(in_isr, req, req_len);

  (void)SubmitDeferredResponseIfIdle();
  (void)SubmitNextQueuedResponseIfIdle();
  ArmOutTransferIfIdle();
}

template <typename SwdPort>
uint16_t DapLinkV1Class<SwdPort>::GetDapPacketSize() const
{
  const uint16_t IN_PS = ep_in_ ? ep_in_->MaxPacketSize() : 0u;
  const uint16_t OUT_PS = ep_out_ ? ep_out_->MaxPacketSize() : 0u;
  uint16_t dap_ps = 0u;

  if (IN_PS > 0u && OUT_PS > 0u)
  {
    dap_ps = (IN_PS < OUT_PS) ? IN_PS : OUT_PS;
  }
  else
  {
    dap_ps = (IN_PS > 0u) ? IN_PS : OUT_PS;
  }

  if (dap_ps == 0u)
  {
    dap_ps = DEFAULT_DAP_PACKET_SIZE;
  }
  if (dap_ps > MAX_DAP_PACKET_SIZE)
  {
    dap_ps = MAX_DAP_PACKET_SIZE;
  }
  return dap_ps;
}

template <typename SwdPort>
uint16_t DapLinkV1Class<SwdPort>::ClipResponseLength(uint16_t len, uint16_t cap) const
{
  const uint16_t DAP_PS = GetDapPacketSize();
  if (len > DAP_PS)
  {
    len = DAP_PS;
  }
  if (len > RESP_SLOT_SIZE)
  {
    len = RESP_SLOT_SIZE;
  }
  if (len > cap)
  {
    len = cap;
  }
  return len;
}

template <typename SwdPort>
constexpr uint8_t DapLinkV1Class<SwdPort>::NextRespQueueIndex(uint8_t idx)
{
  return static_cast<uint8_t>((idx + 1u) & (RESP_QUEUE_DEPTH - 1u));
}

template <typename SwdPort>
void DapLinkV1Class<SwdPort>::ResetResponseQueue()
{
  resp_q_head_ = 0u;
  resp_q_tail_ = 0u;
  resp_q_count_ = 0u;
  deferred_in_resp_valid_ = false;
  deferred_in_resp_len_ = 0u;
}

template <typename SwdPort>
bool DapLinkV1Class<SwdPort>::HasDeferredResponseInEpBuffer() const
{
  return deferred_in_resp_valid_;
}

template <typename SwdPort>
void DapLinkV1Class<SwdPort>::SetDeferredResponseInEpBuffer(uint16_t len)
{
  deferred_in_resp_len_ = len;
  deferred_in_resp_valid_ = true;
}

template <typename SwdPort>
bool DapLinkV1Class<SwdPort>::SubmitDeferredResponseIfIdle()
{
  if (!deferred_in_resp_valid_ || !ep_in_ || ep_in_->GetState() != Endpoint::State::IDLE)
  {
    return false;
  }

  const uint16_t TX_LEN = deferred_in_resp_len_;
  if (ep_in_->Transfer(TX_LEN) != ErrorCode::OK)
  {
    return false;
  }

  deferred_in_resp_valid_ = false;
  deferred_in_resp_len_ = 0u;
  return true;
}

template <typename SwdPort>
bool DapLinkV1Class<SwdPort>::IsResponseQueueEmpty() const
{
  return resp_q_count_ == 0u;
}

template <typename SwdPort>
bool DapLinkV1Class<SwdPort>::IsResponseQueueFull() const
{
  return resp_q_count_ >= RESP_QUEUE_DEPTH;
}

template <typename SwdPort>
bool DapLinkV1Class<SwdPort>::TryBuildAndEnqueueResponse(bool in_isr, const uint8_t* req,
                                                         uint16_t req_len)
{
  if (!req || IsResponseQueueFull())
  {
    return false;
  }

  auto& slot = resp_q_[resp_q_tail_];
  uint16_t out_len = 0u;
  auto ans =
      ProcessOneCommand(in_isr, req, req_len, slot.payload, RESP_SLOT_SIZE, out_len);
  UNUSED(ans);
  slot.len = ClipResponseLength(out_len, RESP_SLOT_SIZE);
  (void)PrepareResponseReport(slot.payload, slot.len, RESP_SLOT_SIZE);

  resp_q_tail_ = NextRespQueueIndex(resp_q_tail_);
  ++resp_q_count_;
  return true;
}

template <typename SwdPort>
uint8_t DapLinkV1Class<SwdPort>::OutstandingResponseCount() const
{
  const uint8_t IN_FLIGHT =
      (ep_in_ && ep_in_->GetState() == Endpoint::State::BUSY) ? 1u : 0u;
  const uint8_t DEFERRED = deferred_in_resp_valid_ ? 1u : 0u;
  return static_cast<uint8_t>(resp_q_count_ + IN_FLIGHT + DEFERRED);
}

template <typename SwdPort>
bool DapLinkV1Class<SwdPort>::EnqueueResponse(const uint8_t* data, uint16_t len)
{
  if (!data || IsResponseQueueFull())
  {
    return false;
  }

  auto& slot = resp_q_[resp_q_tail_];
  const uint16_t CLIPPED = ClipResponseLength(len, RESP_SLOT_SIZE);
  slot.len = CLIPPED;
  if (CLIPPED > 0u)
  {
    Memory::FastCopy(slot.payload, data, CLIPPED);
  }

  resp_q_tail_ = NextRespQueueIndex(resp_q_tail_);
  ++resp_q_count_;
  return true;
}

template <typename SwdPort>
bool DapLinkV1Class<SwdPort>::SubmitNextQueuedResponseIfIdle()
{
  if (!ep_in_ || ep_in_->GetState() != Endpoint::State::IDLE || IsResponseQueueEmpty())
  {
    return false;
  }

  auto tx_buff = ep_in_->GetBuffer();
  if (!tx_buff.addr_ || tx_buff.size_ == 0u)
  {
    return false;
  }

  auto& slot = resp_q_[resp_q_head_];
  uint16_t payload_len = slot.len;
  if (payload_len > tx_buff.size_)
  {
    payload_len = static_cast<uint16_t>(tx_buff.size_);
  }

  if (payload_len > 0u)
  {
    Memory::FastCopy(tx_buff.addr_, slot.payload, payload_len);
  }

  uint16_t tx_len = GetDapPacketSize();
  if (tx_len == 0u || tx_len > tx_buff.size_)
  {
    tx_len = static_cast<uint16_t>(tx_buff.size_);
  }
  if (tx_len < payload_len)
  {
    tx_len = payload_len;
  }
  if (tx_len > payload_len)
  {
    Memory::FastSet(reinterpret_cast<uint8_t*>(tx_buff.addr_) + payload_len, 0,
                    tx_len - payload_len);
  }

  if (ep_in_->Transfer(tx_len) != ErrorCode::OK)
  {
    return false;
  }

  resp_q_head_ = NextRespQueueIndex(resp_q_head_);
  --resp_q_count_;
  return true;
}

template <typename SwdPort>
void DapLinkV1Class<SwdPort>::ArmOutTransferIfIdle()
{
  if (!inited_ || ep_out_ == nullptr || ep_in_ == nullptr)
  {
    return;
  }

  if (ep_out_->GetState() != Endpoint::State::IDLE)
  {
    return;
  }

  if (OutstandingResponseCount() >= MAX_OUTSTANDING_RESPONSES)
  {
    return;
  }

  uint16_t out_rx_len = ep_out_->MaxPacketSize();
  if (out_rx_len == 0u)
  {
    out_rx_len = ep_out_->MaxTransferSize();
  }
  (void)ep_out_->Transfer(out_rx_len);
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
    case ToU8(LibXR::USB::DapLinkV1Def::CommandId::INFO):
      return HandleInfo(in_isr, req, req_len, resp, resp_cap, out_len);
    case ToU8(LibXR::USB::DapLinkV1Def::CommandId::HOST_STATUS):
      return HandleHostStatus(in_isr, req, req_len, resp, resp_cap, out_len);
    case ToU8(LibXR::USB::DapLinkV1Def::CommandId::CONNECT):
      return HandleConnect(in_isr, req, req_len, resp, resp_cap, out_len);
    case ToU8(LibXR::USB::DapLinkV1Def::CommandId::DISCONNECT):
      return HandleDisconnect(in_isr, req, req_len, resp, resp_cap, out_len);
    case ToU8(LibXR::USB::DapLinkV1Def::CommandId::TRANSFER_CONFIGURE):
      return HandleTransferConfigure(in_isr, req, req_len, resp, resp_cap, out_len);
    case ToU8(LibXR::USB::DapLinkV1Def::CommandId::TRANSFER):
      return HandleTransfer(in_isr, req, req_len, resp, resp_cap, out_len);
    case ToU8(LibXR::USB::DapLinkV1Def::CommandId::TRANSFER_BLOCK):
      return HandleTransferBlock(in_isr, req, req_len, resp, resp_cap, out_len);
    case ToU8(LibXR::USB::DapLinkV1Def::CommandId::TRANSFER_ABORT):
      return HandleTransferAbort(in_isr, req, req_len, resp, resp_cap, out_len);
    case ToU8(LibXR::USB::DapLinkV1Def::CommandId::WRITE_ABORT):
      return HandleWriteABORT(in_isr, req, req_len, resp, resp_cap, out_len);
    case ToU8(LibXR::USB::DapLinkV1Def::CommandId::DELAY):
      return HandleDelay(in_isr, req, req_len, resp, resp_cap, out_len);
    case ToU8(LibXR::USB::DapLinkV1Def::CommandId::RESET_TARGET):
      return HandleResetTarget(in_isr, req, req_len, resp, resp_cap, out_len);
    case ToU8(LibXR::USB::DapLinkV1Def::CommandId::SWJ_PINS):
      return HandleSWJPins(in_isr, req, req_len, resp, resp_cap, out_len);
    case ToU8(LibXR::USB::DapLinkV1Def::CommandId::SWJ_CLOCK):
      return HandleSWJClock(in_isr, req, req_len, resp, resp_cap, out_len);
    case ToU8(LibXR::USB::DapLinkV1Def::CommandId::SWJ_SEQUENCE):
      return HandleSWJSequence(in_isr, req, req_len, resp, resp_cap, out_len);
    case ToU8(LibXR::USB::DapLinkV1Def::CommandId::SWD_CONFIGURE):
      return HandleSWDConfigure(in_isr, req, req_len, resp, resp_cap, out_len);
    case ToU8(LibXR::USB::DapLinkV1Def::CommandId::SWD_SEQUENCE):
      return HandleSWDSequence(in_isr, req, req_len, resp, resp_cap, out_len);
    default:
      (void)BuildUnknowCmdResponse(resp, resp_cap, out_len);
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
                                              uint16_t req_len, uint8_t* resp,
                                              uint16_t resp_cap, uint16_t& out_len)
{
  if (req_len < 2u)
  {
    resp[0] = ToU8(LibXR::USB::DapLinkV1Def::CommandId::INFO);
    resp[1] = 0u;
    out_len = 2u;
    return ErrorCode::ARG_ERR;
  }

  const uint8_t INFO_ID = req[1];

  resp[0] = ToU8(LibXR::USB::DapLinkV1Def::CommandId::INFO);

  switch (INFO_ID)
  {
    case ToU8(LibXR::USB::DapLinkV1Def::InfoId::VENDOR):
      return BuildInfoStringResponse(resp[0], info_.vendor, resp, resp_cap, out_len);
    case ToU8(LibXR::USB::DapLinkV1Def::InfoId::PRODUCT):
      return BuildInfoStringResponse(resp[0], info_.product, resp, resp_cap, out_len);
    case ToU8(LibXR::USB::DapLinkV1Def::InfoId::SERIAL_NUMBER):
      return BuildInfoStringResponse(resp[0], info_.serial, resp, resp_cap, out_len);
    case ToU8(LibXR::USB::DapLinkV1Def::InfoId::FIRMWARE_VERSION):
      return BuildInfoStringResponse(resp[0], info_.firmware_ver, resp, resp_cap,
                                     out_len);
    case ToU8(LibXR::USB::DapLinkV1Def::InfoId::DEVICE_VENDOR):
      return BuildInfoStringResponse(resp[0], info_.device_vendor, resp, resp_cap,
                                     out_len);
    case ToU8(LibXR::USB::DapLinkV1Def::InfoId::DEVICE_NAME):
      return BuildInfoStringResponse(resp[0], info_.device_name, resp, resp_cap, out_len);
    case ToU8(LibXR::USB::DapLinkV1Def::InfoId::BOARD_VENDOR):
      return BuildInfoStringResponse(resp[0], info_.board_vendor, resp, resp_cap,
                                     out_len);
    case ToU8(LibXR::USB::DapLinkV1Def::InfoId::BOARD_NAME):
      return BuildInfoStringResponse(resp[0], info_.board_name, resp, resp_cap, out_len);
    case ToU8(LibXR::USB::DapLinkV1Def::InfoId::PRODUCT_FIRMWARE_VERSION):
      return BuildInfoStringResponse(resp[0], info_.product_fw_ver, resp, resp_cap,
                                     out_len);
    case ToU8(LibXR::USB::DapLinkV1Def::InfoId::CAPABILITIES):
      return BuildInfoU8Response(resp[0], LibXR::USB::DapLinkV1Def::DAP_CAP_SWD, resp,
                                 resp_cap, out_len);
    case ToU8(LibXR::USB::DapLinkV1Def::InfoId::PACKET_COUNT):
      return BuildInfoU8Response(resp[0], PACKET_COUNT_ADVERTISED, resp, resp_cap,
                                 out_len);
    case ToU8(LibXR::USB::DapLinkV1Def::InfoId::PACKET_SIZE):
    {
      const uint16_t DAP_PS = GetDapPacketSize();
      return BuildInfoU16Response(resp[0], DAP_PS, resp, resp_cap, out_len);
    }
    case ToU8(LibXR::USB::DapLinkV1Def::InfoId::TIMESTAMP_CLOCK):
      return BuildInfoU32Response(resp[0], 1000000U, resp, resp_cap, out_len);
    default:
      resp[1] = 0u;
      out_len = 2u;
      return ErrorCode::OK;
  }
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::BuildInfoStringResponse(uint8_t cmd, const char* str,
                                                           uint8_t* resp,
                                                           uint16_t resp_cap,
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
  resp[2 + COPY_N - 1] = 0x00;
  resp[1] = static_cast<uint8_t>(COPY_N);
  out_len = static_cast<uint16_t>(COPY_N + 2u);
  return ErrorCode::OK;
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::BuildInfoU8Response(uint8_t cmd, uint8_t val,
                                                       uint8_t* resp, uint16_t resp_cap,
                                                       uint16_t& out_len)
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
ErrorCode DapLinkV1Class<SwdPort>::BuildInfoU16Response(uint8_t cmd, uint16_t val,
                                                        uint8_t* resp, uint16_t resp_cap,
                                                        uint16_t& out_len)
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
ErrorCode DapLinkV1Class<SwdPort>::BuildInfoU32Response(uint8_t cmd, uint32_t val,
                                                        uint8_t* resp, uint16_t resp_cap,
                                                        uint16_t& out_len)
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
ErrorCode DapLinkV1Class<SwdPort>::HandleHostStatus(bool /*in_isr*/,
                                                    const uint8_t* /*req*/,
                                                    uint16_t /*req_len*/, uint8_t* resp,
                                                    uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0u;
    return ErrorCode::NOT_FOUND;
  }
  resp[0] = ToU8(LibXR::USB::DapLinkV1Def::CommandId::HOST_STATUS);
  resp[1] = ToU8(LibXR::USB::DapLinkV1Def::Status::OK);
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

  resp[0] = ToU8(LibXR::USB::DapLinkV1Def::CommandId::CONNECT);

  uint8_t port = 0u;
  if (req_len >= 2u)
  {
    port = req[1];
  }

  if (port == 0u || port == ToU8(LibXR::USB::DapLinkV1Def::Port::SWD))
  {
    (void)swd_.EnterSwd();
    (void)swd_.SetClockHz(swj_clock_hz_);

    dap_state_.debug_port = LibXR::USB::DapLinkV1Def::DebugPort::SWD;
    dap_state_.transfer_abort = false;

    resp[1] = ToU8(LibXR::USB::DapLinkV1Def::Port::SWD);
  }
  else
  {
    resp[1] = ToU8(LibXR::USB::DapLinkV1Def::Port::DISABLED);
  }

  out_len = 2u;
  return ErrorCode::OK;
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::HandleDisconnect(bool /*in_isr*/,
                                                    const uint8_t* /*req*/,
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

  resp[0] = ToU8(LibXR::USB::DapLinkV1Def::CommandId::DISCONNECT);
  resp[1] = ToU8(LibXR::USB::DapLinkV1Def::Status::OK);
  out_len = 2u;
  return ErrorCode::OK;
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::HandleTransferConfigure(
    bool /*in_isr*/, const uint8_t* req, uint16_t req_len, uint8_t* resp,
    uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0u;
    return ErrorCode::NOT_FOUND;
  }

  resp[0] = ToU8(LibXR::USB::DapLinkV1Def::CommandId::TRANSFER_CONFIGURE);

  if (req_len < 6u)
  {
    resp[1] = ToU8(LibXR::USB::DapLinkV1Def::Status::ERROR);
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

  resp[1] = ToU8(LibXR::USB::DapLinkV1Def::Status::OK);
  out_len = 2u;
  return ErrorCode::OK;
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::HandleTransferAbort(bool /*in_isr*/,
                                                       const uint8_t* /*req*/,
                                                       uint16_t /*req_len*/,
                                                       uint8_t* resp, uint16_t resp_cap,
                                                       uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0u;
    return ErrorCode::NOT_FOUND;
  }

  SetTransferAbortFlag(true);

  resp[0] = ToU8(LibXR::USB::DapLinkV1Def::CommandId::TRANSFER_ABORT);
  resp[1] = ToU8(LibXR::USB::DapLinkV1Def::Status::OK);
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

  resp[0] = ToU8(LibXR::USB::DapLinkV1Def::CommandId::WRITE_ABORT);

  if (req_len < 6u)
  {
    resp[1] = ToU8(LibXR::USB::DapLinkV1Def::Status::ERROR);
    out_len = 2u;
    return ErrorCode::ARG_ERR;
  }

  uint32_t flags = 0u;
  Memory::FastCopy(&flags, &req[2], sizeof(flags));

  LibXR::Debug::SwdProtocol::Ack ack = LibXR::Debug::SwdProtocol::Ack::PROTOCOL;
  const ErrorCode EC = swd_.WriteAbortTxn(flags, ack);
  resp[1] = (EC == ErrorCode::OK && ack == LibXR::Debug::SwdProtocol::Ack::OK)
                ? ToU8(LibXR::USB::DapLinkV1Def::Status::OK)
                : ToU8(LibXR::USB::DapLinkV1Def::Status::ERROR);

  out_len = 2u;
  return ErrorCode::OK;
}

template <typename SwdPort>
ErrorCode DapLinkV1Class<SwdPort>::HandleDelay(bool /*in_isr*/, const uint8_t* req,
                                               uint16_t req_len, uint8_t* resp,
                                               uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0u;
    return ErrorCode::NOT_FOUND;
  }

  resp[0] = ToU8(LibXR::USB::DapLinkV1Def::CommandId::DELAY);

  if (req_len < 3u)
  {
    resp[1] = ToU8(LibXR::USB::DapLinkV1Def::Status::ERROR);
    out_len = 2u;
    return ErrorCode::ARG_ERR;
  }

  uint16_t us = 0u;
  Memory::FastCopy(&us, &req[1], sizeof(us));

  LibXR::Timebase::DelayMicroseconds(us);

  resp[1] = ToU8(LibXR::USB::DapLinkV1Def::Status::OK);
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

  resp[0] = ToU8(LibXR::USB::DapLinkV1Def::CommandId::RESET_TARGET);

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

  resp[0] = ToU8(LibXR::USB::DapLinkV1Def::CommandId::SWJ_PINS);

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
    const bool LEVEL_HIGH = ((PIN_OUT & LibXR::USB::DapLinkV1Def::DAP_SWJ_NRESET) != 0u);
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

  resp[0] = ToU8(LibXR::USB::DapLinkV1Def::CommandId::SWJ_CLOCK);

  if (req_len < 5u)
  {
    resp[1] = ToU8(LibXR::USB::DapLinkV1Def::Status::ERROR);
    out_len = 2u;
    return ErrorCode::ARG_ERR;
  }

  uint32_t hz = 0u;
  Memory::FastCopy(&hz, &req[1], sizeof(hz));

  swj_clock_hz_ = hz;
  (void)swd_.SetClockHz(hz);

  resp[1] = ToU8(LibXR::USB::DapLinkV1Def::Status::OK);
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

  resp[0] = ToU8(LibXR::USB::DapLinkV1Def::CommandId::SWJ_SEQUENCE);
  resp[1] = ToU8(LibXR::USB::DapLinkV1Def::Status::OK);
  out_len = 2u;

  if (!req || req_len < 2u)
  {
    resp[1] = ToU8(LibXR::USB::DapLinkV1Def::Status::ERROR);
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
    resp[1] = ToU8(LibXR::USB::DapLinkV1Def::Status::ERROR);
    return ErrorCode::ARG_ERR;
  }

  const uint8_t* data = &req[2];
  const ErrorCode EC = swd_.SeqWriteBits(bit_count, data);
  if (EC != ErrorCode::OK)
  {
    resp[1] = ToU8(LibXR::USB::DapLinkV1Def::Status::ERROR);
    return ErrorCode::OK;
  }

  swj_shadow_ = static_cast<uint8_t>(
      swj_shadow_ & static_cast<uint8_t>(~LibXR::USB::DapLinkV1Def::DAP_SWJ_SWCLK_TCK));

  bool last_swdio = false;
  if (bit_count != 0u)
  {
    const uint32_t LAST_I = bit_count - 1u;
    last_swdio = (((data[LAST_I / 8u] >> (LAST_I & 7u)) & 0x01u) != 0u);
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
ErrorCode DapLinkV1Class<SwdPort>::HandleSWDConfigure(bool /*in_isr*/,
                                                      const uint8_t* /*req*/,
                                                      uint16_t /*req_len*/, uint8_t* resp,
                                                      uint16_t resp_cap,
                                                      uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0u;
    return ErrorCode::NOT_FOUND;
  }

  resp[0] = ToU8(LibXR::USB::DapLinkV1Def::CommandId::SWD_CONFIGURE);
  resp[1] = ToU8(LibXR::USB::DapLinkV1Def::Status::OK);
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

  resp[0] = ToU8(LibXR::USB::DapLinkV1Def::CommandId::SWD_SEQUENCE);
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

      const uint8_t* data = &req[req_off];
      req_off = static_cast<uint16_t>(req_off + BYTES);

      const ErrorCode EC = swd_.SeqWriteBits(cycles, data);
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
void DapLinkV1Class<SwdPort>::SetTransferAbortFlag(bool on)
{
  dap_state_.transfer_abort = on;
}

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

  resp[0] = ToU8(LibXR::USB::DapLinkV1Def::CommandId::TRANSFER);
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
    ErrorCode ec = ErrorCode::OK;

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
        ec = swd_.ApWriteTxn(ADDR2B, wdata, ack);
      }
      else
      {
        ec = swd_.DpWriteTxn(static_cast<LibXR::Debug::SwdProtocol::DpWriteReg>(ADDR2B),
                             wdata, ack);
      }

      response_value = ack_to_dap(ack);
      if (response_value != LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK)
      {
        break;
      }
      if (ec != ErrorCode::OK)
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
            ec = swd_.ApReadTxn(ADDR2B, rdata, ack);
            if (ec == ErrorCode::OK && ack == LibXR::Debug::SwdProtocol::Ack::OK)
            {
              check_write = false;
            }
          }
          else
          {
            ec = swd_.DpReadTxn(static_cast<LibXR::Debug::SwdProtocol::DpReadReg>(ADDR2B),
                                rdata, ack);
          }

          response_value = ack_to_dap(ack);
          if (response_value != LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK)
          {
            break;
          }
          if (ec != ErrorCode::OK)
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
        ec = swd_.DpReadTxn(static_cast<LibXR::Debug::SwdProtocol::DpReadReg>(ADDR2B),
                            rdata, ack);

        response_value = ack_to_dap(ack);
        if (response_value != LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK)
        {
          break;
        }
        if (ec != ErrorCode::OK)
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
        ec = swd_.ApReadPostedTxn(ADDR2B, dummy_posted, ack);

        response_value = ack_to_dap(ack);
        if (response_value != LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK)
        {
          break;
        }
        if (ec != ErrorCode::OK)
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
        ec = swd_.ApReadPostedTxn(ADDR2B, posted_prev, ack);

        const uint8_t CUR_V = ack_to_dap(ack);
        if (CUR_V != LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK || ec != ErrorCode::OK)
        {
          const uint8_t PRIOR_FAIL = (CUR_V != LibXR::USB::DapLinkV1Def::DAP_TRANSFER_OK)
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
ErrorCode DapLinkV1Class<SwdPort>::HandleTransferBlock(bool /*in_isr*/,
                                                       const uint8_t* req,
                                                       uint16_t req_len, uint8_t* resp,
                                                       uint16_t resp_cap,
                                                       uint16_t& out_len)
{
  if (!resp || resp_cap < 4u)
  {
    out_len = 0u;
    return ErrorCode::NOT_FOUND;
  }

  resp[0] = ToU8(LibXR::USB::DapLinkV1Def::CommandId::TRANSFER_BLOCK);
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
      ErrorCode ec = ErrorCode::OK;

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
        ec = swd_.ApWriteTxn(ADDR2B, wdata, ack);
      }
      else
      {
        ec = swd_.DpWriteTxn(static_cast<LibXR::Debug::SwdProtocol::DpWriteReg>(ADDR2B),
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

      if (ec != ErrorCode::OK)
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
      ErrorCode ec = ErrorCode::OK;
      uint32_t rdata = 0u;

      if (resp_off + 4u > resp_cap)
      {
        xresp |= LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
        break;
      }

      ec = swd_.DpReadTxn(static_cast<LibXR::Debug::SwdProtocol::DpReadReg>(ADDR2B),
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

      if (ec != ErrorCode::OK)
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
    ErrorCode ec = ErrorCode::OK;

    uint32_t dummy_posted = 0u;
    ec = swd_.ApReadPostedTxn(ADDR2B, dummy_posted, ack);
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
    if (ec != ErrorCode::OK)
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
      ec = swd_.ApReadPostedTxn(ADDR2B, posted_prev, ack);
      const uint8_t CUR = MapAckToDapResp(ack);

      if (CUR == 0u)
      {
        xresp = CUR | LibXR::USB::DapLinkV1Def::DAP_TRANSFER_ERROR;
        goto out_ap_read;  // NOLINT
      }

      if (ack != LibXR::Debug::SwdProtocol::Ack::OK || ec != ErrorCode::OK)
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
        if (ec != ErrorCode::OK)
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
