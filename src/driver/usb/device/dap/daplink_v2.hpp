// ===================
// File: daplink_v2.hpp
// ===================
#pragma once

#include <cstddef>
#include <cstdint>

#include "daplink_v2_def.hpp"  // LibXR::USB::DapLinkV2Def
#include "debug/swd.hpp"       // LibXR::Debug::Swd
#include "dev_core.hpp"
#include "gpio.hpp"
#include "libxr_def.hpp"
#include "libxr_type.hpp"
#include "timebase.hpp"
#include "usb/core/desc_cfg.hpp"
#include "winusb_msos20.hpp"  // LibXR::USB::WinUsbMsOs20

namespace LibXR::USB
{

/**
 * @brief CMSIS-DAP v2 (Bulk) USB class (SWD-only minimal, optional nRESET control)
 *
 * - CMSIS-DAP v2 Bulk transport (2x Bulk EP: IN/OUT)
 * - SWD backend via injected LibXR::Debug::Swd instance
 * - SWJ_Pins (0x10) only supports nRESET, ignores SWDIO/SWCLK modifications
 * - SWJ_Clock default 1MHz
 *
 * WinUSB (MS OS 2.0) support:
 * - Provide BOS (Platform Capability: MS OS 2.0)
 * - Provide MS OS 2.0 descriptor set with CompatibleID "WINUSB"
 * - Provide DeviceInterfaceGUIDs (REG_MULTI_SZ) so user-mode can enumerate/open
 */
class DapLinkV2Class : public DeviceClass
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

 public:
  explicit DapLinkV2Class(
      LibXR::Debug::Swd& swd_link, LibXR::GPIO* nreset_gpio = nullptr,
      Endpoint::EPNumber data_in_ep_num = Endpoint::EPNumber::EP_AUTO,
      Endpoint::EPNumber data_out_ep_num = Endpoint::EPNumber::EP_AUTO);

  ~DapLinkV2Class() = default;

  DapLinkV2Class(const DapLinkV2Class&) = delete;
  DapLinkV2Class& operator=(const DapLinkV2Class&) = delete;

 public:
  void SetInfoStrings(const InfoStrings& info);

  const LibXR::USB::DapLinkV2Def::State& GetState() const;
  bool IsInited() const;

 protected:
  void Init(EndpointPool& endpoint_pool, uint8_t start_itf_num) override;
  void Deinit(EndpointPool& endpoint_pool) override;

  size_t GetInterfaceNum() override;
  bool HasIAD() override;
  bool OwnsEndpoint(uint8_t ep_addr) const override;
  size_t GetMaxConfigSize() override;

  // ---- WinUSB MS OS 2.0 hook points (DeviceCore / ConfigDescriptor will query these)
  // ----
  bool HasWinUSB20Descriptor() override { return true; }
  ConstRawData GetWinUSB20Descriptor() override;
  ConstRawData GetWinUSBBOSDescriptor() override;
  uint8_t GetWinUSBVendorCode() override;

 private:
  // USB callbacks
  static void OnDataOutCompleteStatic(bool in_isr, DapLinkV2Class* self,
                                      LibXR::ConstRawData& data);
  static void OnDataInCompleteStatic(bool in_isr, DapLinkV2Class* self,
                                     LibXR::ConstRawData& data);

  void OnDataOutComplete(bool in_isr, LibXR::ConstRawData& data);
  void OnDataInComplete(bool in_isr, LibXR::ConstRawData& data);

 private:
  // Arm OUT endpoint
  void ArmOutTransferIfIdle();

 private:
  // Command dispatch
  ErrorCode ProcessOneCommand(bool in_isr, const uint8_t* req, uint16_t req_len,
                              uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

  void BuildNotSupportResponse(uint8_t cmd, uint8_t* resp, uint16_t resp_cap,
                               uint16_t& out_len);

 private:
  // Command handlers
  ErrorCode Handle_Info(bool in_isr, const uint8_t* req, uint16_t req_len, uint8_t* resp,
                        uint16_t resp_cap, uint16_t& out_len);

  ErrorCode Handle_HostStatus(bool in_isr, const uint8_t* req, uint16_t req_len,
                              uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

  ErrorCode Handle_Connect(bool in_isr, const uint8_t* req, uint16_t req_len,
                           uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

  ErrorCode Handle_Disconnect(bool in_isr, const uint8_t* req, uint16_t req_len,
                              uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

  ErrorCode Handle_TransferConfigure(bool in_isr, const uint8_t* req, uint16_t req_len,
                                     uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

  ErrorCode Handle_Transfer(bool in_isr, const uint8_t* req, uint16_t req_len,
                            uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

  ErrorCode Handle_TransferBlock(bool in_isr, const uint8_t* req, uint16_t req_len,
                                 uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

  ErrorCode Handle_TransferAbort(bool in_isr, const uint8_t* req, uint16_t req_len,
                                 uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

  ErrorCode Handle_WriteABORT(bool in_isr, const uint8_t* req, uint16_t req_len,
                              uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

  ErrorCode Handle_Delay(bool in_isr, const uint8_t* req, uint16_t req_len, uint8_t* resp,
                         uint16_t resp_cap, uint16_t& out_len);

  ErrorCode Handle_ResetTarget(bool in_isr, const uint8_t* req, uint16_t req_len,
                               uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

  ErrorCode Handle_SWJ_Pins(bool in_isr, const uint8_t* req, uint16_t req_len,
                            uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

  ErrorCode Handle_SWJ_Clock(bool in_isr, const uint8_t* req, uint16_t req_len,
                             uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

  ErrorCode Handle_SWJ_Sequence(bool in_isr, const uint8_t* req, uint16_t req_len,
                                uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

  ErrorCode Handle_SWD_Configure(bool in_isr, const uint8_t* req, uint16_t req_len,
                                 uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

  ErrorCode Handle_SWD_Sequence(bool in_isr, const uint8_t* req, uint16_t req_len,
                                uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

  ErrorCode Handle_QueueCommands(bool in_isr, const uint8_t* req, uint16_t req_len,
                                 uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

  ErrorCode Handle_ExecuteCommands(bool in_isr, const uint8_t* req, uint16_t req_len,
                                   uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

 private:
  // Info response builders
  ErrorCode BuildInfoStringResponse(uint8_t cmd, const char* str, uint8_t* resp,
                                    uint16_t resp_cap, uint16_t& out_len);

  ErrorCode BuildInfoU8Response(uint8_t cmd, uint8_t val, uint8_t* resp,
                                uint16_t resp_cap, uint16_t& out_len);

  ErrorCode BuildInfoU16Response(uint8_t cmd, uint16_t val, uint8_t* resp,
                                 uint16_t resp_cap, uint16_t& out_len);

  ErrorCode BuildInfoU32Response(uint8_t cmd, uint32_t val, uint8_t* resp,
                                 uint16_t resp_cap, uint16_t& out_len);

 private:
  // Transfer helpers
  uint8_t MapAckToDapResp(LibXR::Debug::Swd::Ack ack) const;
  void SetTransferAbortFlag(bool on);

 private:
  // Reset helpers
  void DriveReset(bool release);
  void DelayUsIfAllowed(bool in_isr, uint32_t us);

 private:
  // ---- WinUSB descriptor builders ----
  static constexpr uint8_t kWinUsbVendorCode = 0x20;
  static constexpr uint16_t kGuidMultiSzUtf16Bytes = static_cast<uint16_t>(
      LibXR::USB::WinUsbMsOs20::kGuidStrUtf16Bytes + 2);  // extra UTF-16 NUL

  void InitWinUsbDescriptors_();        // init constant parts
  void UpdateWinUsbInterfaceFields_();  // patch interface-dependent fields

#pragma pack(push, 1)
  struct WinUsbBosDesc
  {
    LibXR::USB::WinUsbMsOs20::BosHeader bos;
    LibXR::USB::WinUsbMsOs20::MsOs20PlatformCapability cap;
  };

  struct WinUsbMsOs20DescSet
  {
    LibXR::USB::WinUsbMsOs20::MsOs20SetHeader set;
    LibXR::USB::WinUsbMsOs20::MsOs20SubsetHeaderConfiguration cfg;
    LibXR::USB::WinUsbMsOs20::MsOs20SubsetHeaderFunction func;
    LibXR::USB::WinUsbMsOs20::MsOs20FeatureCompatibleId compat;

    struct RegProp
    {
      LibXR::USB::WinUsbMsOs20::MsOs20FeatureRegPropertyHeader header;
      uint8_t name[LibXR::USB::WinUsbMsOs20::kPropName_DeviceInterfaceGUIDs_Bytes];
      uint16_t wPropertyDataLength;
      uint8_t data[kGuidMultiSzUtf16Bytes];
    } prop;
  };
#pragma pack(pop)

  WinUsbBosDesc winusb_bos_{};
  WinUsbMsOs20DescSet winusb_msos20_{};

 private:
  LibXR::Debug::Swd& swd_;

  LibXR::GPIO* nreset_gpio_ = nullptr;
  bool last_nreset_level_high_ = true;

  LibXR::USB::DapLinkV2Def::State dap_state_{};
  InfoStrings info_{};

  uint32_t swj_clock_hz_ = 1000000U;

  Endpoint::EPNumber data_in_ep_num_;
  Endpoint::EPNumber data_out_ep_num_;

  Endpoint* ep_data_in_ = nullptr;
  Endpoint* ep_data_out_ = nullptr;

  bool inited_ = false;
  uint8_t interface_num_ = 0;

#pragma pack(push, 1)
  struct DapLinkV2DescBlock
  {
    InterfaceDescriptor intf;
    EndpointDescriptor ep_out;
    EndpointDescriptor ep_in;
  } desc_block_{};
#pragma pack(pop)

 private:
  static constexpr uint16_t kMaxReq = LibXR::USB::DapLinkV2Def::kMaxRequestSize;
  static constexpr uint16_t kMaxResp = LibXR::USB::DapLinkV2Def::kMaxResponseSize;

  uint8_t rx_buf_[kMaxReq]{};
  uint8_t tx_buf_[kMaxResp]{};

  // Serialize OUT->IN->OUT (request/response) to avoid tx_buf_ overwrite
  bool tx_busy_ = false;

  LibXR::Callback<LibXR::ConstRawData&> on_data_out_cb_ =
      LibXR::Callback<LibXR::ConstRawData&>::Create(OnDataOutCompleteStatic, this);

  LibXR::Callback<LibXR::ConstRawData&> on_data_in_cb_ =
      LibXR::Callback<LibXR::ConstRawData&>::Create(OnDataInCompleteStatic, this);
};

}  // namespace LibXR::USB
