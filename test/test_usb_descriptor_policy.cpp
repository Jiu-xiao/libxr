#include <cstdint>

#include "daplink_v2.hpp"
#include "dfu.hpp"
#include "test.hpp"
#include "winusb_msos20.hpp"

namespace
{
using LibXR::ConstRawData;
using LibXR::ErrorCode;
using LibXR::RawData;
using LibXR::USB::DFUCapabilities;
using LibXR::USB::DFUClass;
using LibXR::USB::DFUStatusCode;
using LibXR::USB::DfuRuntimeClass;
using LibXR::USB::WinUsbMsOs20::DeviceScopedWinUsbMsOs20DescSet;
using LibXR::USB::WinUsbMsOs20::FunctionScopedWinUsbMsOs20DescSet;
using LibXR::USB::WinUsbMsOs20::GUID_MULTI_SZ_UTF16_BYTES;

struct DummySwdPort
{
  ErrorCode SetClockHz(uint32_t) { return ErrorCode::OK; }
};

template <typename SwdPort>
struct DapLinkProbe : public LibXR::USB::DapLinkV2Class<SwdPort>
{
  using Base = LibXR::USB::DapLinkV2Class<SwdPort>;
  using Base::kWinUsbConfigurationValue;
};

struct DummyDfuBackend
{
  DFUCapabilities GetDfuCapabilities() const { return {}; }
  ErrorCode DfuSetAlternate(uint8_t) { return ErrorCode::OK; }
  void DfuAbort(uint8_t) {}
  void DfuClearStatus(uint8_t) {}
  DFUStatusCode DfuDownload(uint8_t, uint16_t, ConstRawData, uint32_t& poll_timeout_ms)
  {
    poll_timeout_ms = 0u;
    return DFUStatusCode::OK;
  }
  DFUStatusCode DfuGetDownloadStatus(uint8_t, bool& busy, uint32_t& poll_timeout_ms)
  {
    busy = false;
    poll_timeout_ms = 0u;
    return DFUStatusCode::OK;
  }
  size_t DfuUpload(uint8_t, uint16_t, RawData, DFUStatusCode& status,
                   uint32_t& poll_timeout_ms)
  {
    status = DFUStatusCode::OK;
    poll_timeout_ms = 0u;
    return 0u;
  }
  DFUStatusCode DfuManifest(uint8_t, uint32_t& poll_timeout_ms)
  {
    poll_timeout_ms = 0u;
    return DFUStatusCode::OK;
  }
  DFUStatusCode DfuGetManifestStatus(uint8_t, bool& busy, uint32_t& poll_timeout_ms)
  {
    busy = false;
    poll_timeout_ms = 0u;
    return DFUStatusCode::OK;
  }
  void DfuCommitManifestWaitReset(uint8_t) {}
};

template <typename Backend>
struct DfuClassProbe : public DFUClass<Backend>
{
  using Base = DFUClass<Backend>;
  using Base::Base;
  using Base::GetBosCapability;
  using Base::GetBosCapabilityCount;
};

struct DfuRuntimeProbe : public DfuRuntimeClass
{
  using Base = DfuRuntimeClass;
  using Base::Base;
  using Base::GetBosCapability;
  using Base::GetBosCapabilityCount;
};
}  // namespace

void test_usb_descriptor_policy()
{
  {
    FunctionScopedWinUsbMsOs20DescSet<GUID_MULTI_SZ_UTF16_BYTES> set = {};
    set.Init(0u, 3u, "{CDB3B5AD-293B-4663-AA36-1AAE46463776}");
    ASSERT(set.cfg.bConfigurationValue == 0u);
    ASSERT(set.func.bFirstInterface == 3u);
    ASSERT(set.set.wTotalLength == sizeof(set));
  }

  {
    DeviceScopedWinUsbMsOs20DescSet<GUID_MULTI_SZ_UTF16_BYTES> set = {};
    set.Init("{4066E5F4-3B02-4B90-9475-12F770A7841B}");
    ASSERT(set.set.wTotalLength == sizeof(set));
  }

  {
    ASSERT(DapLinkProbe<DummySwdPort>::kWinUsbConfigurationValue == 0u);
  }

  {
    DummyDfuBackend backend = {};
    DfuClassProbe<DummyDfuBackend> dfu(backend);
    ASSERT(dfu.GetBosCapabilityCount() == 1u);
    ASSERT(dfu.GetBosCapability(0u) != nullptr);
    ASSERT(dfu.GetBosCapability(1u) == nullptr);
  }

  {
    DfuRuntimeProbe runtime(nullptr);
    ASSERT(runtime.GetBosCapabilityCount() == 0u);
    ASSERT(runtime.GetBosCapability(0u) == nullptr);
  }

  {
    DfuRuntimeProbe runtime(nullptr, nullptr, 50u, DfuRuntimeProbe::DEFAULT_INTERFACE_STRING,
                            "https://example.com/dfu");
    ASSERT(runtime.GetBosCapabilityCount() == 1u);
    ASSERT(runtime.GetBosCapability(0u) != nullptr);
    ASSERT(runtime.GetBosCapability(1u) == nullptr);
  }
}
