// jtag_protocol.hpp
#pragma once

#include <cstdint>

namespace LibXR::Debug::JtagProtocol
{
/**
 * @brief JTAG 传输端口选择 / JTAG transfer port selector
 */
enum class Port : uint8_t
{
  DP = 0,  ///< Debug Port / 调试端口
  AP = 1,  ///< Access Port / 访问端口
};

/**
 * @brief JTAG ACK 返回码 / JTAG ACK response codes
 *
 * @note ACK 为 3-bit（LSB-first）编码；此处以解码后的枚举值表示。
 *       ACK is a 3-bit (LSB-first) encoding; this enum represents decoded values.
 */
enum class Ack : uint8_t
{
  NO_ACK = 0x0,    ///< 无应答 / No ACK
  OK = 0x1,        ///< OK / OK
  WAIT = 0x2,      ///< WAIT / WAIT
  FAULT = 0x4,     ///< FAULT / FAULT
  PROTOCOL = 0x7,  ///< 协议错误（非法 ACK）/ Protocol error (invalid ACK)
};

/**
 * @brief JTAG 传输请求 / JTAG transfer request
 *
 * @note request 位域与 SWD 一致：APnDP/RnW/A2/A3。JTAG 中由 DPACC/APACC IR 选择端口。
 */
struct Request
{
  Port port = Port::DP;  ///< 目标端口（DP/AP）/ Target port (DP/AP)
  bool rnw =
      true;  ///< 读写标志：true=读，false=写 / Read-not-write: true=read, false=write
  uint8_t addr2b = 0;  ///< A[3:2] 两位地址编码（0..3）/ A[3:2] encoded as 0..3
  uint32_t wdata = 0;  ///< 写数据（仅写请求有效）/ Write data (valid for write requests)
};

/**
 * @brief JTAG 传输响应 / JTAG transfer response
 */
struct Response
{
  Ack ack = Ack::PROTOCOL;  ///< ACK / ACK
  uint32_t rdata = 0;  ///< 读数据（仅读响应有效）/ Read data (valid for read responses)
};

/**
 * @brief JTAG IR 指令码 / JTAG IR codes
 */
inline constexpr uint32_t JTAG_IR_ABORT = 0x08u;
inline constexpr uint32_t JTAG_IR_DPACC = 0x0Au;
inline constexpr uint32_t JTAG_IR_APACC = 0x0Bu;
inline constexpr uint32_t JTAG_IR_IDCODE = 0x0Eu;
inline constexpr uint32_t JTAG_IR_BYPASS = 0x0Fu;

/**
 * @brief JTAG Sequence 信息字段 / JTAG sequence info fields
 */
inline constexpr uint8_t JTAG_SEQUENCE_TCK = 0x3Fu;  ///< TCK count (1..64, 0->64)
inline constexpr uint8_t JTAG_SEQUENCE_TMS = 0x40u;  ///< TMS value
inline constexpr uint8_t JTAG_SEQUENCE_TDO = 0x80u;  ///< TDO capture

/**
 * @brief JTAG-DP DR 长度（bit）/ JTAG-DP DR length in bits
 */
inline constexpr uint32_t JTAG_DP_DR_LEN = 35u;

/**
 * @brief JTAG ACK 原始值映射（JTAG-DP 与 SWD ACK 编码不同）
 *
 * JTAG 原始 3-bit：OK=0x2, WAIT=0x1, FAULT=0x4
 * 映射后使用本枚举（OK=0x1, WAIT=0x2, FAULT=0x4）
 */
constexpr Ack map_jtag_ack(uint8_t raw_ack)
{
  switch (raw_ack & 0x7u)
  {
    case 0x2:
      return Ack::OK;
    case 0x1:
      return Ack::WAIT;
    case 0x4:
      return Ack::FAULT;
    case 0x0:
      return Ack::NO_ACK;
    default:
      return Ack::PROTOCOL;
  }
}

/**
 * @brief JTAG 设备链配置 / JTAG device chain configuration
 *
 * @note 该结构仅描述设备链参数，不分配内存。
 */
struct ChainConfig
{
  uint8_t count = 0;        ///< 设备数（TDO 端为 index=0）/ Device count (TDO side index=0)
  uint8_t index = 0;        ///< 当前设备索引 / Current device index
  const uint8_t* ir_length = nullptr;  ///< 各设备 IR 长度数组 / IR length array
  const uint16_t* ir_before = nullptr; ///< 当前设备前的 bypass 位数 / Bypass bits before
  const uint16_t* ir_after = nullptr;  ///< 当前设备后的 bypass 位数 / Bypass bits after

  // 运行期缓存（可选）：用于减少每次 Shift 计算
  uint32_t ir_before_bits_len = 0;  ///< 前面设备 IR 总位数 / IR bits before
  uint32_t ir_after_bits_len = 0;   ///< 后面设备 IR 总位数 / IR bits after
  uint32_t dr_before_bits_len = 0;  ///< 前面设备 DR 总位数 / DR bits before
  uint32_t dr_after_bits_len = 0;   ///< 后面设备 DR 总位数 / DR bits after
};

/**
 * @brief 构造 DP 请求 / Build DP request
 */
constexpr Request make_dp_req(bool rnw, uint8_t addr2b, uint32_t wdata = 0u)
{
  return Request{Port::DP, rnw, static_cast<uint8_t>(addr2b & 0x03u), wdata};
}

/**
 * @brief 构造 AP 请求 / Build AP request
 */
constexpr Request make_ap_req(bool rnw, uint8_t addr2b, uint32_t wdata = 0u)
{
  return Request{Port::AP, rnw, static_cast<uint8_t>(addr2b & 0x03u), wdata};
}

}  // namespace LibXR::Debug::JtagProtocol
