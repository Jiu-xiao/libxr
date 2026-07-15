#pragma once

#include <cstdint>

#include "libxr_def.hpp"

namespace LibXR::Debug
{
/**
 * @brief JTAG TAP states.
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
 * @brief Abstract JTAG base class providing TAP control and IR/DR shifting.
 */
class Jtag
{
 public:
  virtual ~Jtag() = default;

  Jtag(const Jtag&) = delete;
  Jtag& operator=(const Jtag&) = delete;

  /**
   * @brief Set TCK frequency, if supported.
   * @param hz Target frequency in Hz.
   * @return Operation result.
   */
  virtual ErrorCode SetClockHz(uint32_t hz) = 0;

  /**
   * @brief Close JTAG and release resources.
   */
  virtual void Close() = 0;

  /**
   * @brief Reset TAP to Test-Logic-Reset.
   * @return Operation result.
   */
  virtual ErrorCode ResetTap() = 0;

  /**
   * @brief Move to the requested TAP state.
   * @param target Target TAP state.
   * @return Operation result.
   */
  virtual ErrorCode GotoState(TapState target) = 0;

  /**
   * @brief Shift IR, LSB first.
   * @param bits Bit count.
   * @param in_lsb_first Optional TDI data, LSB first.
   * @param out_lsb_first Optional TDO data, LSB first.
   * @return Operation result.
   */
  virtual ErrorCode ShiftIR(uint32_t bits, const uint8_t* in_lsb_first,
                            uint8_t* out_lsb_first) = 0;

  /**
   * @brief Shift DR, LSB first.
   * @param bits Bit count.
   * @param in_lsb_first Optional TDI data, LSB first.
   * @param out_lsb_first Optional TDO data, LSB first.
   * @return Operation result.
   */
  virtual ErrorCode ShiftDR(uint32_t bits, const uint8_t* in_lsb_first,
                            uint8_t* out_lsb_first) = 0;

  /**
   * @brief Shift a fixed-TMS sequence, LSB first.
   * @param cycles Bit count.
   * @param tms Fixed TMS value.
   * @param tdi_lsb_first Optional TDI data, LSB first.
   * @param tdo_lsb_first Optional TDO data, LSB first.
   * @return Operation result.
   */
  virtual ErrorCode Sequence(uint32_t cycles, bool tms, const uint8_t* tdi_lsb_first,
                             uint8_t* tdo_lsb_first) = 0;

  /**
   * @brief Insert idle clock cycles in Run-Test/Idle.
   * @param cycles Number of cycles.
   */
  virtual void IdleClocks(uint32_t cycles) = 0;

 protected:
  Jtag() = default;
};

}  // namespace LibXR::Debug
