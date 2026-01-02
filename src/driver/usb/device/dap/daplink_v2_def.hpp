// =======================
// File: daplink_v2_def.hpp
// =======================
#pragma once

#include <cstdint>

#include "libxr_def.hpp"
#include "timebase.hpp"

namespace LibXR::USB
{

/**
 * @brief CMSIS-DAP v2 (Bulk) protocol definitions for DapLinkV2DapLinkV2Def USB class
 *
 * 仅包含定义/常量/结构体；不包含任何实现逻辑。
 * Definitions only; no implementation logic here.
 */
namespace DapLinkV2Def
{

// ==============================
// Limits / 限制
// ==============================

static constexpr std::uint16_t kMaxRequestSize = 512;
static constexpr std::uint16_t kMaxResponseSize = 512;

// ==============================
// CMSIS-DAP v2 Command IDs / 命令号
// 注意：按规范值定义（SWO_Data=0x1C, SWD_Sequence=0x1D）
// ==============================

enum class CommandId : std::uint8_t
{
  // Core (0x00-0x0F)
  Info = 0x00,
  HostStatus = 0x01,
  Connect = 0x02,
  Disconnect = 0x03,
  TransferConfigure = 0x04,
  Transfer = 0x05,
  TransferBlock = 0x06,
  TransferAbort = 0x07,
  WriteABORT = 0x08,
  Delay = 0x09,
  ResetTarget = 0x0A,

  // SWJ (0x10-0x1F)
  SWJ_Pins = 0x10,
  SWJ_Clock = 0x11,
  SWJ_Sequence = 0x12,
  SWD_Configure = 0x13,
  JTAG_Sequence = 0x14,
  JTAG_Configure = 0x15,
  JTAG_IDCODE = 0x16,

  // SWO (v2)
  SWO_Transport = 0x17,
  SWO_Mode = 0x18,
  SWO_Baudrate = 0x19,
  SWO_Control = 0x1A,
  SWO_Status = 0x1B,
  SWO_Data = 0x1C,

  // SWD sequence (v2)
  SWD_Sequence = 0x1D,

  // Queue (0x7E-0x7F)
  QueueCommands = 0x7E,
  ExecuteCommands = 0x7F,

  Invalid = 0xFF
};

// ==============================
// DAP_Info IDs / 信息 ID
// ==============================

enum class InfoId : std::uint8_t
{
  Vendor = 0x01,
  Product = 0x02,
  SerialNumber = 0x03,
  FirmwareVersion = 0x04,

  DeviceVendor = 0x05,
  DeviceName = 0x06,
  BoardVendor = 0x07,
  BoardName = 0x08,
  ProductFirmwareVersion = 0x09,

  Capabilities = 0xF0,
  TimestampClock = 0xF1,
  SWO_BufferSize = 0xFD,
  PacketCount = 0xFE,
  PacketSize = 0xFF
};

// ==============================
// Capabilities bits / 能力位（DAP_Info: Capabilities）
// ==============================

static constexpr std::uint8_t DAP_CAP_SWD = 0x01;
static constexpr std::uint8_t DAP_CAP_JTAG = 0x02;
static constexpr std::uint8_t DAP_CAP_SWO = 0x04;

// ==============================
// Status / Port / 状态与端口
// ==============================

enum class Status : std::uint8_t
{
  OK = 0x00,
  Error = 0xFF
};

enum class Port : std::uint8_t
{
  Disabled = 0x00,
  SWD = 0x01,
  JTAG = 0x02
};

enum class DebugPort : std::uint8_t
{
  DISABLED = 0,
  SWD = 1,
  JTAG = 2
};

// ==============================
// Transfer Request bits / 传输请求位
// ==============================

static constexpr std::uint8_t DAP_TRANSFER_APnDP = (1U << 0);
static constexpr std::uint8_t DAP_TRANSFER_RnW = (1U << 1);
static constexpr std::uint8_t DAP_TRANSFER_A2 = (1U << 2);
static constexpr std::uint8_t DAP_TRANSFER_A3 = (1U << 3);
static constexpr std::uint8_t DAP_TRANSFER_MATCH_VALUE = (1U << 4);
static constexpr std::uint8_t DAP_TRANSFER_MATCH_MASK = (1U << 5);
static constexpr std::uint8_t DAP_TRANSFER_TIMESTAMP = (1U << 7);  // v2

// Transfer Response bits / 传输响应位
static constexpr std::uint8_t DAP_TRANSFER_OK = (1U << 0);
static constexpr std::uint8_t DAP_TRANSFER_WAIT = (1U << 1);
static constexpr std::uint8_t DAP_TRANSFER_FAULT = (1U << 2);
static constexpr std::uint8_t DAP_TRANSFER_ERROR = (1U << 3);
static constexpr std::uint8_t DAP_TRANSFER_MISMATCH = (1U << 4);
static constexpr std::uint8_t DAP_TRANSFER_NO_TARGET = (1U << 7);  // v2

// ==============================
// SWJ Pins bits / SWJ 引脚位
// ==============================

static constexpr std::uint8_t DAP_SWJ_SWCLK_TCK = (1U << 0);
static constexpr std::uint8_t DAP_SWJ_SWDIO_TMS = (1U << 1);
static constexpr std::uint8_t DAP_SWJ_TDI = (1U << 2);
static constexpr std::uint8_t DAP_SWJ_TDO = (1U << 3);
static constexpr std::uint8_t DAP_SWJ_nTRST = (1U << 5);
static constexpr std::uint8_t DAP_SWJ_nRESET = (1U << 7);

// ==============================
// SWD / JTAG Sequence fields / 序列字段
// ==============================

static constexpr std::uint8_t SWD_SEQUENCE_CLK = 0x3F;
static constexpr std::uint8_t SWD_SEQUENCE_DIN = (1U << 7);

static constexpr std::uint8_t JTAG_SEQUENCE_TCK = 0x3F;
static constexpr std::uint8_t JTAG_SEQUENCE_TMS = (1U << 6);
static constexpr std::uint8_t JTAG_SEQUENCE_TDO = (1U << 7);

// ==============================
// Helpers / 工具函数
// ==============================

static inline constexpr std::uint8_t ReqAddr2b(std::uint8_t req)
{
  return static_cast<std::uint8_t>(((req & DAP_TRANSFER_A2) ? 1U : 0U) |
                                   ((req & DAP_TRANSFER_A3) ? 2U : 0U));
}

static inline constexpr bool ReqIsAp(std::uint8_t req)
{
  return (req & DAP_TRANSFER_APnDP) != 0U;
}

static inline constexpr bool ReqIsRead(std::uint8_t req)
{
  return (req & DAP_TRANSFER_RnW) != 0U;
}

static inline constexpr bool ReqNeedTimestamp(std::uint8_t req)
{
  return (req & DAP_TRANSFER_TIMESTAMP) != 0U;
}

// ==============================
// State structs / 运行态结构体
// ==============================

struct TransferConfig
{
  std::uint8_t idle_cycles = 0;
  std::uint16_t retry_count = 100;
  std::uint16_t match_retry = 0;
  std::uint32_t match_mask = 0;
};

struct SwdConfig
{
  std::uint8_t turnaround = 1;
  bool data_phase = false;
};

struct State
{
  DebugPort debug_port = DebugPort::DISABLED;
  volatile bool transfer_abort = false;

  TransferConfig transfer_cfg;
  SwdConfig swd_cfg;
};

struct CommandResult
{
  std::uint16_t request_consumed = 0;
  std::uint16_t response_generated = 0;
};

}  // namespace DapLinkV2Def

}  // namespace LibXR::USB
