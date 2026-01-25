#pragma once

#include <cstddef>
#include <cstdint>

#include "daplink_v1_def.hpp"
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

  explicit DapLinkV1Class(LibXR::Debug::Swd& swd_link, LibXR::GPIO* nreset_gpio = nullptr,
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

  void BindEndpoints(EndpointPool& endpoint_pool, uint8_t start_itf_num) override;
  void UnbindEndpoints(EndpointPool& endpoint_pool) override;

  void OnDataOutComplete(bool in_isr, LibXR::ConstRawData& data) override;
  void OnDataInComplete(bool in_isr, LibXR::ConstRawData& data) override;

 private:
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
  void SetTransferAbortFlag(bool on);

  void DriveReset(bool release);
  void DelayUsIfAllowed(bool in_isr, uint32_t us);

 private:
  static constexpr uint16_t MAX_REQ = DapLinkV1Def::MAX_REQUEST_SIZE;
  static constexpr uint16_t MAX_RESP = DapLinkV1Def::MAX_RESPONSE_SIZE;

  LibXR::Debug::Swd& swd_;
  LibXR::GPIO* nreset_gpio_ = nullptr;

  uint8_t swj_shadow_ = static_cast<uint8_t>(DapLinkV1Def::DAP_SWJ_SWDIO_TMS |
                                             DapLinkV1Def::DAP_SWJ_NRESET);
  bool last_nreset_level_high_ = true;

  LibXR::USB::DapLinkV1Def::State dap_state_{};
  InfoStrings info_{"XRobot", "DAPLinkV1", "00000001", "1.0.0", "XRUSB",
                    "XRDAP",  "XRobot",    "DAP_DEMO", "0.1.0"};

  uint32_t swj_clock_hz_ = 1000000u;
  uint32_t match_mask_ = 0xFFFFFFFFu;

  uint8_t tx_buf_[MAX_RESP]{};
  bool tx_busy_ = false;
  bool inited_ = false;
};

}  // namespace LibXR::USB
