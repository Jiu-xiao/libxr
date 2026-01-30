#pragma once

#include <cstdint>

#include "libxr_def.hpp"

namespace LibXR::Debug
{
/**
 * @brief JTAG TAP 状态。JTAG TAP states.
 */
enum class TapState : uint8_t
{
  TEST_LOGIC_RESET = 0,
  RUN_TEST_IDLE,
  SELECT_DR_SCAN,
  CAPTURE_DR,
  SHIFT_DR,
  EXIT1_DR,
  PAUSE_DR,
  EXIT2_DR,
  UPDATE_DR,
  SELECT_IR_SCAN,
  CAPTURE_IR,
  SHIFT_IR,
  EXIT1_IR,
  PAUSE_IR,
  EXIT2_IR,
  UPDATE_IR,
};

/**
 * @class Jtag
 * @brief JTAG 抽象基类，提供 TAP 控制与 IR/DR 移位能力。
 *        Abstract JTAG base class providing TAP control and IR/DR shifting.
 */
class Jtag
{
 public:
  virtual ~Jtag() = default;

  Jtag(const Jtag&) = delete;
  Jtag& operator=(const Jtag&) = delete;

  /**
   * @brief 设置 TCK 频率（可选）。Set TCK frequency (optional).
   * @param hz 目标频率（Hz）。Target frequency in Hz.
   * @return ErrorCode 操作结果。Error code.
   */
  virtual ErrorCode SetClockHz(uint32_t hz) = 0;

  /**
   * @brief 关闭 JTAG 并释放资源。Close JTAG and release resources.
   */
  virtual void Close() = 0;

  /**
   * @brief 复位 TAP 到 Test-Logic-Reset。Reset TAP to Test-Logic-Reset.
   * @return ErrorCode 操作结果。Error code.
   */
  virtual ErrorCode ResetTap() = 0;

  /**
   * @brief 切换到指定 TAP 状态。Goto target TAP state.
   * @param target 目标状态。Target state.
   * @return ErrorCode 操作结果。Error code.
   */
  virtual ErrorCode GotoState(TapState target) = 0;

  /**
   * @brief IR 移位（LSB-first）。Shift IR (LSB-first).
   * @param bits 位数。Bit count.
   * @param in_lsb_first 输入 TDI 数据（LSB-first），可为 nullptr。
   * @param out_lsb_first 输出 TDO 数据（LSB-first），可为 nullptr。
   * @return ErrorCode 操作结果。Error code.
   */
  virtual ErrorCode ShiftIR(uint32_t bits, const uint8_t* in_lsb_first,
                            uint8_t* out_lsb_first) = 0;

  /**
   * @brief DR 移位（LSB-first）。Shift DR (LSB-first).
   * @param bits 位数。Bit count.
   * @param in_lsb_first 输入 TDI 数据（LSB-first），可为 nullptr。
   * @param out_lsb_first 输出 TDO 数据（LSB-first），可为 nullptr。
   * @return ErrorCode 操作结果。Error code.
   */
  virtual ErrorCode ShiftDR(uint32_t bits, const uint8_t* in_lsb_first,
                            uint8_t* out_lsb_first) = 0;

  /**
   * @brief 产生固定 TMS 的序列移位（LSB-first）。Shift sequence with fixed TMS (LSB-first).
   * @param cycles 位数。Bit count.
   * @param tms 固定的 TMS 值。Fixed TMS value.
   * @param tdi_lsb_first 输入 TDI 数据（LSB-first），可为 nullptr。
   * @param tdo_lsb_first 输出 TDO 数据（LSB-first），可为 nullptr。
   * @return ErrorCode 操作结果。Error code.
   */
  virtual ErrorCode Sequence(uint32_t cycles, bool tms, const uint8_t* tdi_lsb_first,
                             uint8_t* tdo_lsb_first) = 0;

  /**
   * @brief 插入空闲时钟周期（Run-Test/Idle）。Insert idle cycles.
   * @param cycles 周期数。Number of cycles.
   */
  virtual void IdleClocks(uint32_t cycles) = 0;

 protected:
  Jtag() = default;
};

}  // namespace LibXR::Debug
