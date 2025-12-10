#pragma once

#include <cstddef>
#include <cstdint>

namespace LibXR::USB::GsUsb
{

// =================== CAN ID 标志（与 Linux can.h 对齐） ===================

constexpr uint32_t CAN_EFF_FLAG = 0x80000000U;  // 扩展帧标志
constexpr uint32_t CAN_RTR_FLAG = 0x40000000U;  // RTR 标志
constexpr uint32_t CAN_ERR_FLAG = 0x20000000U;  // 错误帧标志

constexpr uint32_t CAN_SFF_MASK = 0x000007FFU;
constexpr uint32_t CAN_EFF_MASK = 0x1FFFFFFFU;

constexpr uint8_t CAN_ERR_DLC = 8;

// 错误类（can_id 中的掩码，参照 linux/can/error.h）
constexpr uint32_t CAN_ERR_TX_TIMEOUT = 0x00000001U;
constexpr uint32_t CAN_ERR_LOSTARB = 0x00000002U;
constexpr uint32_t CAN_ERR_CRTL = 0x00000004U;
constexpr uint32_t CAN_ERR_PROT = 0x00000008U;
constexpr uint32_t CAN_ERR_TRX = 0x00000010U;
constexpr uint32_t CAN_ERR_ACK = 0x00000020U;
constexpr uint32_t CAN_ERR_BUSOFF = 0x00000040U;
constexpr uint32_t CAN_ERR_BUSERROR = 0x00000080U;
constexpr uint32_t CAN_ERR_RESTARTED = 0x00000100U;

// 下面这些是 error frame data[] 初始值里用到的一些常量（简化版）
constexpr uint8_t CAN_ERR_LOSTARB_UNSPEC = 0x00;
constexpr uint8_t CAN_ERR_CRTL_UNSPEC = 0x00;
constexpr uint8_t CAN_ERR_PROT_UNSPEC = 0x00;
constexpr uint8_t CAN_ERR_PROT_LOC_UNSPEC = 0x00;
constexpr uint8_t CAN_ERR_TRX_UNSPEC = 0x00;

// =================== BREQ（控制请求号，与 gs_usb proto 对齐） ===================

enum class BReq : uint8_t
{
  HOST_FORMAT = 0,
  BITTIMING,
  MODE,
  BERR,
  BT_CONST,
  DEVICE_CONFIG,
  TIMESTAMP,
  IDENTIFY,
  GET_USER_ID,
  SET_USER_ID,
  DATA_BITTIMING,
  BT_CONST_EXT,
  SET_TERMINATION,
  GET_TERMINATION,
  GET_STATE,
};

// =================== CAN 模式 / 状态 ===================

enum class CanMode : uint32_t
{
  RESET = 0,
  START = 1,
};

enum class CanState : uint32_t
{
  ERROR_ACTIVE = 0,
  ERROR_WARNING,
  ERROR_PASSIVE,
  BUS_OFF,
  STOPPED,
  SLEEPING
};

enum class IdentifyMode : uint32_t
{
  OFF = 0,
  ON = 1,
};

enum class TerminationState : uint32_t
{
  OFF = 0,
  ON = 1,
};

// =================== 控制传输结构体（packed 对齐原协议） ===================

struct HostConfig
{
  uint32_t byte_order;  // host 写 0x0000beef，小端
} __attribute__((packed));

// 设备配置（per-device）
struct DeviceConfig
{
  uint8_t reserved1;
  uint8_t reserved2;
  uint8_t reserved3;
  uint8_t icount;       // CAN 通道数 - 1
  uint32_t sw_version;  // 软件版本
  uint32_t hw_version;  // 硬件版本
} __attribute__((packed));

// MODE 设置（per-channel）
struct DeviceMode
{
  uint32_t mode;   // CanMode
  uint32_t flags;  // GS_CAN_MODE_*
} __attribute__((packed));

struct DeviceState
{
  uint32_t state;  // CanState
  uint32_t rxerr;
  uint32_t txerr;
} __attribute__((packed));

struct DeviceBitTiming
{
  uint32_t prop_seg;
  uint32_t phase_seg1;
  uint32_t phase_seg2;
  uint32_t sjw;
  uint32_t brp;
} __attribute__((packed));

struct CanBitTimingConst
{
  uint32_t tseg1_min;
  uint32_t tseg1_max;
  uint32_t tseg2_min;
  uint32_t tseg2_max;
  uint32_t sjw_max;
  uint32_t brp_min;
  uint32_t brp_max;
  uint32_t brp_inc;
} __attribute__((packed));

struct DeviceBTConst
{
  uint32_t feature;  // CAN_FEAT_*
  uint32_t fclk_can;
  CanBitTimingConst btc;
} __attribute__((packed));

struct DeviceBTConstExtended
{
  uint32_t feature;  // CAN_FEAT_* (包含 FD)
  uint32_t fclk_can;
  CanBitTimingConst btc;   // 仲裁相位
  CanBitTimingConst dbtc;  // 数据相位
} __attribute__((packed));

struct Identify
{
  uint32_t mode;  // IdentifyMode
} __attribute__((packed));

struct DeviceTerminationState
{
  uint32_t state;  // TerminationState
} __attribute__((packed));

// =================== MODE 标志位（GS_CAN_MODE_*） ===================

constexpr uint32_t GSCAN_MODE_NORMAL = 0;
constexpr uint32_t GSCAN_MODE_LISTEN_ONLY = (1u << 0);
constexpr uint32_t GSCAN_MODE_LOOP_BACK = (1u << 1);
constexpr uint32_t GSCAN_MODE_TRIPLE_SAMPLE = (1u << 2);
constexpr uint32_t GSCAN_MODE_ONE_SHOT = (1u << 3);
constexpr uint32_t GSCAN_MODE_HW_TIMESTAMP = (1u << 4);
constexpr uint32_t GSCAN_MODE_PAD_PKTS_TO_MAX_PKT_SIZE = (1u << 7);
constexpr uint32_t GSCAN_MODE_FD = (1u << 8);
constexpr uint32_t GSCAN_MODE_BERR_REPORTING = (1u << 12);

// =================== 功能位（GS_CAN_FEATURE_*） ===================

constexpr uint32_t CAN_FEAT_LISTEN_ONLY = (1u << 0);
constexpr uint32_t CAN_FEAT_LOOP_BACK = (1u << 1);
constexpr uint32_t CAN_FEAT_TRIPLE_SAMPLE = (1u << 2);
constexpr uint32_t CAN_FEAT_ONE_SHOT = (1u << 3);
constexpr uint32_t CAN_FEAT_HW_TIMESTAMP = (1u << 4);
constexpr uint32_t CAN_FEAT_IDENTIFY = (1u << 5);
constexpr uint32_t CAN_FEAT_USER_ID = (1u << 6);
constexpr uint32_t CAN_FEAT_PAD_PKTS_TO_MAX_PKT_SIZE = (1u << 7);
constexpr uint32_t CAN_FEAT_FD = (1u << 8);
constexpr uint32_t CAN_FEAT_REQ_USB_QUIRK_LPC546XX = (1u << 9);
constexpr uint32_t CAN_FEAT_BT_CONST_EXT = (1u << 10);
constexpr uint32_t CAN_FEAT_TERMINATION = (1u << 11);
constexpr uint32_t CAN_FEAT_BERR_REPORTING = (1u << 12);
constexpr uint32_t CAN_FEAT_GET_STATE = (1u << 13);

// =================== 帧标志（GS_CAN_FLAG_*） ===================

constexpr uint8_t CAN_FLAG_OVERFLOW = (1u << 0);
constexpr uint8_t CAN_FLAG_FD = (1u << 1);   // FD 帧
constexpr uint8_t CAN_FLAG_BRS = (1u << 2);  // Bit Rate Switch
constexpr uint8_t CAN_FLAG_ESI = (1u << 3);  // Error State Indicator

// =================== USB HostFrame 结构 ===================
// 使用最大尺寸布局：header + data[64] + timestamp_us
// 发送时按 flags / timestamp 选择实际长度

struct HostFrame
{
  uint32_t echo_id;
  uint32_t can_id;

  uint8_t can_dlc;
  uint8_t channel;
  uint8_t flags;
  uint8_t reserved;

  uint8_t data[64];       // 经典 CAN 仅用前 8 字节
  uint32_t timestamp_us;  // 硬件时间戳（可选）
} __attribute__((packed));

constexpr uint32_t ECHO_ID_INVALID = 0xFFFFFFFFu;

// 最小长度：header + 8 data（classic）
constexpr std::size_t HOST_FRAME_CLASSIC_SIZE =
    sizeof(uint32_t) + sizeof(uint32_t) + 4 + 8;
// classic + timestamp
constexpr std::size_t HOST_FRAME_CLASSIC_TS_SIZE =
    HOST_FRAME_CLASSIC_SIZE + sizeof(uint32_t);

// FD：header + 64 data
constexpr std::size_t HOST_FRAME_FD_SIZE = sizeof(uint32_t) + sizeof(uint32_t) + 4 + 64;
// FD + timestamp
constexpr std::size_t HOST_FRAME_FD_TS_SIZE = HOST_FRAME_FD_SIZE + sizeof(uint32_t);

}  // namespace LibXR::USB::GsUsb
