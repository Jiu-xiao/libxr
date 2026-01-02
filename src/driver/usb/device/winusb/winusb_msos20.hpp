// ===================
// File: winusb_msos20.hpp
// ===================
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace LibXR::USB::WinUsbMsOs20
{

// ---- constants ----

// wIndex values
static constexpr uint16_t kMsOs20DescriptorIndex = 0x0007;
static constexpr uint16_t kMsOs20SetAltEnumeration = 0x0008;

// MS OS 2.0 descriptor types
static constexpr uint16_t MS_OS_20_SET_HEADER_DESCRIPTOR = 0x0000;
static constexpr uint16_t MS_OS_20_SUBSET_HEADER_CONFIGURATION = 0x0001;
static constexpr uint16_t MS_OS_20_SUBSET_HEADER_FUNCTION = 0x0002;
static constexpr uint16_t MS_OS_20_FEATURE_COMPATIBLE_ID = 0x0003;
static constexpr uint16_t MS_OS_20_FEATURE_REG_PROPERTY = 0x0004;

// registry property type
static constexpr uint16_t REG_MULTI_SZ = 0x0007;

// MS OS 2.0 platform capability UUID: D8DD60DF-4589-4CC7-9CD2-659D9E648A9F
static constexpr uint8_t kMsOs20PlatformCapabilityUuid[16] = {
    0xDF, 0x60, 0xDD, 0xD8, 0x89, 0x45, 0xC7, 0x4C,
    0x9C, 0xD2, 0x65, 0x9D, 0x9E, 0x64, 0x8A, 0x9F,
};

// "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}" + UTF-16 NUL
static constexpr uint16_t kGuidCharsWithBraces = 38;
static constexpr uint16_t kGuidStrUtf16Bytes = (kGuidCharsWithBraces + 1) * 2;  // 78

// "DeviceInterfaceGUIDs" (UTF-16LE) + NUL
static constexpr uint8_t kPropName_DeviceInterfaceGUIDs_Utf16[] = {
    'D', 0x00, 'e', 0x00, 'v', 0x00, 'i', 0x00, 'c', 0x00, 'e', 0x00, 'I',  0x00,
    'n', 0x00, 't', 0x00, 'e', 0x00, 'r', 0x00, 'f', 0x00, 'a', 0x00, 'c',  0x00,
    'e', 0x00, 'G', 0x00, 'U', 0x00, 'I', 0x00, 'D', 0x00, 's', 0x00, 0x00, 0x00};
static constexpr uint16_t kPropName_DeviceInterfaceGUIDs_Bytes =
    static_cast<uint16_t>(sizeof(kPropName_DeviceInterfaceGUIDs_Utf16));

#pragma pack(push, 1)

// ---- MS OS 2.0 basic blocks ----

// 9.22.1 MS OS 2.0 descriptor set header
struct MsOs20SetHeader
{
  uint16_t wLength = 0x000A;
  uint16_t wDescriptorType = MS_OS_20_SET_HEADER_DESCRIPTOR;
  uint32_t dwWindowsVersion = 0x06030000;  // 0x06030000 = Win 8.1
  uint16_t wTotalLength = 0;               // sizeof(whole set)
};

// 9.22.2 Configuration subset header
struct MsOs20SubsetHeaderConfiguration
{
  uint16_t wLength = 0x0008;
  uint16_t wDescriptorType = MS_OS_20_SUBSET_HEADER_CONFIGURATION;
  uint8_t bConfigurationValue = 0;
  uint8_t bReserved = 0;
  uint16_t wTotalLength = 0;  // sizeof(this cfg subset)
};

// 9.22.3 Function subset header
struct MsOs20SubsetHeaderFunction
{
  uint16_t wLength = 0x0008;
  uint16_t wDescriptorType = MS_OS_20_SUBSET_HEADER_FUNCTION;
  uint8_t bFirstInterface = 0;
  uint8_t bReserved = 0;
  uint16_t wTotalLength = 0;  // sizeof(this function subset)
};

// 9.22.4 Compatible ID descriptor
struct MsOs20FeatureCompatibleId
{
  uint16_t wLength = 0x0014;
  uint16_t wDescriptorType = MS_OS_20_FEATURE_COMPATIBLE_ID;

  // 默认 "WINUSB"，派生类可以改
  uint8_t CompatibleID[8] = {'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00};
  uint8_t SubCompatibleID[8] = {0};
};

// 9.22.5 Registry property descriptor header部分
// 后面紧跟：
//   - UTF-16LE property name (wPropertyNameLength bytes)
//   - uint16_t wPropertyDataLength
//   - property data (wPropertyDataLength bytes)
struct MsOs20FeatureRegPropertyHeader
{
  uint16_t wLength = 0;  // sizeof(whole reg property feature)
  uint16_t wDescriptorType = MS_OS_20_FEATURE_REG_PROPERTY;
  uint16_t wPropertyDataType = 0;  // e.g. REG_MULTI_SZ
  uint16_t wPropertyNameLength = 0;
};

// ---- BOS / platform capability blocks ----

// 9.6.2 BOS descriptor
struct BosHeader
{
  uint8_t bLength = 0x05;
  uint8_t bDescriptorType = 0x0F;  // BOS
  uint16_t wTotalLength = 0;       // sizeof(whole BOS tree)
  uint8_t bNumDeviceCaps = 0;      // number of following device caps
};

// 9.6.2.1 Device capability header (platform)
struct DeviceCapabilityHeader
{
  uint8_t bLength = 0x1C;             // for MS OS 2.0 platform cap
  uint8_t bDescriptorType = 0x10;     // DEVICE_CAPABILITY
  uint8_t bDevCapabilityType = 0x05;  // PLATFORM
  uint8_t bReserved = 0x00;
};

// MS OS 2.0 Platform Capability = DeviceCapabilityHeader + payload
struct MsOs20PlatformCapability
{
  uint8_t bLength = 0x1C;
  uint8_t bDescriptorType = 0x10;
  uint8_t bDevCapabilityType = 0x05;
  uint8_t bReserved = 0x00;

  uint8_t PlatformCapabilityUUID[16] = {
      0xDF, 0x60, 0xDD, 0xD8, 0x89, 0x45, 0xC7, 0x4C,
      0x9C, 0xD2, 0x65, 0x9D, 0x9E, 0x64, 0x8A, 0x9F,
  };

  uint32_t dwWindowsVersion = 0x06030000;
  uint16_t wMSOSDescriptorSetTotalLength = 0;  // sizeof(MS OS 2.0 set)
  uint8_t bMS_VendorCode = 0x20;
  uint8_t bAltEnumCode = 0x00;
};

#pragma pack(pop)

// ---- sanity checks ----
static_assert(sizeof(MsOs20SetHeader) == 10, "SetHeader size mismatch");
static_assert(sizeof(MsOs20SubsetHeaderConfiguration) == 8, "CfgHeader size mismatch");
static_assert(sizeof(MsOs20SubsetHeaderFunction) == 8, "FuncHeader size mismatch");
static_assert(sizeof(MsOs20FeatureCompatibleId) == 20, "CompatibleId size mismatch");
static_assert(sizeof(MsOs20PlatformCapability) == 28, "PlatformCapability size mismatch");

}  // namespace LibXR::USB::WinUsbMsOs20
