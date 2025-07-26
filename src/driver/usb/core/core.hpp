#pragma once
#include <cstdint>

namespace LibXR::USB
{
/**
 * @brief USB 请求方向
 *        USB Request direction (bit 7 of bmRequestType)
 */
enum class RequestDirection : uint8_t
{
  OUT = 0x00,  ///< 主机到设备 / Host-to-Device
  IN = 0x80    ///< 设备到主机 / Device-to-Host
};

/**
 * @brief USB 请求类型
 *        USB Request type (bits 6:5 of bmRequestType)
 */
enum class RequestType : uint8_t
{
  STANDARD = 0x00,  ///< 标准请求 / Standard
  CLASS = 0x20,     ///< 类请求 / Class-specific
  VENDOR = 0x40,    ///< 厂商自定义请求 / Vendor-specific
  RESERVED = 0x60   ///< 保留类型 / Reserved
};

/**
 * @brief USB 请求目标（接收对象）
 *        USB Request recipient (bits 4:0 of bmRequestType)
 */
enum class Recipient : uint8_t
{
  DEVICE = 0x00,     ///< 设备本身 / Device
  INTERFACE = 0x01,  ///< 接口 / Interface
  ENDPOINT = 0x02,   ///< 端点 / Endpoint
  OTHER = 0x03       ///< 其他 / Other
};

/**
 * @brief bmRequestType 解析掩码
 *        Masks to extract bmRequestType fields
 */
constexpr uint8_t REQ_DIRECTION_MASK = 0x80;  ///< 位7：方向 / Bit 7: Direction
constexpr uint8_t REQ_TYPE_MASK = 0x60;       ///< 位6-5：类型 / Bits 6-5: Type
constexpr uint8_t REQ_RECIPIENT_MASK = 0x1F;  ///< 位4-0：接收对象 / Bits 4-0: Recipient

#pragma pack(push, 1)
/**
 * @brief USB 标准请求 SETUP 包（固定8字节）
 *        Standard USB setup packet (8 bytes)
 *
 * 对应 USB 协议中控制传输的第1阶段，用于描述请求类型与参数。
 * Used in control transfers to carry request and arguments.
 */
struct SetupPacket
{
  uint8_t bmRequestType;  ///< 请求类型、方向、目标编码 / Bitmap encoding direction, type,
                          ///< recipient
  uint8_t bRequest;       ///< 请求码 / Request code (e.g., GET_DESCRIPTOR)
  uint16_t
      wValue;  ///< 参数字段，含请求相关值 / Value field (e.g., descriptor type/index)
  uint16_t wIndex;   ///< 对象索引，如接口号或端点号 / Index (e.g., interface or endpoint)
  uint16_t wLength;  ///< 数据阶段长度 / Number of bytes in data stage
};
#pragma pack(pop)

// 标准请求枚举
enum class StandardRequest : uint8_t
{
  GET_STATUS = 0,         ///< 获取状态 / Get Status
  CLEAR_FEATURE = 1,      ///< 清除功能 / Clear Feature
  SET_FEATURE = 3,        ///< 设置功能 / Set Feature
  SET_ADDRESS = 5,        ///< 设置设备地址 / Set Device Address
  GET_DESCRIPTOR = 6,     ///< 获取描述符 / Get Descriptor
  SET_DESCRIPTOR = 7,     ///< 设置描述符 / Set Descriptor
  GET_CONFIGURATION = 8,  ///< 获取配置值 / Get Configuration
  SET_CONFIGURATION = 9,  ///< 设置配置值 / Set Configuration
  GET_INTERFACE = 10,     ///< 获取接口设置 / Get Interface
  SET_INTERFACE = 11,     ///< 设置接口设置 / Set Interface
  SYNCH_FRAME = 12        ///< 同步帧 / Synch Frame (仅限ISO端点)
};

/**
 * @brief USB 传输速率等级（用于描述符或控制器初始化）
 *        USB speed level (used in descriptors or controller setup)
 */
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
  USB_1_0 = 0x0100,                  ///< USB 1.0
  USB_1_1 = 0x0110,                  ///< USB 1.1
  USB_2_0 = 0x0200,                  ///< USB 2.0
  USB_2_1 = 0x0210,                  ///< USB 2.1
  USB_3_0 = 0x0300,                  ///< USB 3.0
  USB_3_1 = 0x0310,                  ///< USB 3.1
  USB_3_2 = 0x0320,                  ///< USB 3.2
  USB_3_1_SUPER_SPEED_PLUS = 0x0321  ///< USB 3.1+ SuperSpeedPlus
};
}  // namespace LibXR::USB
