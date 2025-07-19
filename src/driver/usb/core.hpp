#pragma once
#include <cstdint>

namespace LibXR::USB
{
#pragma pack(push, 1)
// USB请求方向
enum class RequestDirection : uint8_t
{
  OUT = 0x00,  // Host-to-Device
  IN = 0x80    // Device-to-Host
};

// USB请求类型
enum class RequestType : uint8_t
{
  STANDARD = 0x00,
  CLASS = 0x20,
  VENDOR = 0x40,
  RESERVED = 0x60
};

// USB请求目标
enum class Recipient : uint8_t
{
  DEVICE = 0x00,
  INTERFACE = 0x01,
  ENDPOINT = 0x02,
  OTHER = 0x03
};

constexpr uint8_t REQ_DIRECTION_MASK = 0x80;
constexpr uint8_t REQ_TYPE_MASK = 0x60;
constexpr uint8_t REQ_RECIPIENT_MASK = 0x1F;

// 8字节SETUP包
struct SetupPacket
{
  uint8_t bmRequestType;
  uint8_t bRequest;
  uint16_t wValue;
  uint16_t wIndex;
  uint16_t wLength;
};

// 标准请求枚举
enum class StandardRequest : uint8_t
{
  GET_STATUS = 0,
  CLEAR_FEATURE = 1,
  SET_FEATURE = 3,
  SET_ADDRESS = 5,
  GET_DESCRIPTOR = 6,
  SET_DESCRIPTOR = 7,
  GET_CONFIGURATION = 8,
  SET_CONFIGURATION = 9,
  GET_INTERFACE = 10,
  SET_INTERFACE = 11,
  SYNCH_FRAME = 12
};
#pragma pack(pop)

enum class Speed : uint8_t
{
  LOW,        // 1.5 Mbps
  FULL,       // 12 Mbps
  HIGH,       // 480 Mbps
  SUPER,      // 5 Gbps (USB 3.x，可选)
  SUPER_PLUS  // 10 Gbps（可选，暂不支持）
};

enum class USBSpec : uint16_t
{
  USB_1_0 = 0x0100,
  USB_1_1 = 0x0110,
  USB_2_0 = 0x0200,
  USB_2_1 = 0x0210,
  USB_3_0 = 0x0300,
  USB_3_1 = 0x0310,
  USB_3_2 = 0x0320,
  USB_3_1_SUPER_SPEED_PLUS = 0x0321
};
}  // namespace LibXR::USB
