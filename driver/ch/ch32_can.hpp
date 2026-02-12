#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>

#include "can.hpp"
#include "ch32_can_def.hpp"
#include "libxr.hpp"

#include DEF2STR(LIBXR_CH32_CONFIG_FILE)

namespace LibXR
{
/**
 * @brief CH32CAN driver (bxCAN-like) for LibXR::CAN.
 *
 * Design goals (aligned with your STM32CAN driver):
 * - Lock-free TX queue (LockFreePool) + mailbox service in IRQ / thread contexts.
 * - RX dispatch in ISR via LibXR::CAN::OnMessage().
 * - Error to "virtual error frame" mapping using LibXR::CAN::ErrorID.
 *
 * Notes:
 * - This driver is written against WCH StdPeriph CAN API (CAN_Init / CAN_Transmit /
 *   CAN_Receive / CAN_ITConfig...).
 * - Bit timing fields in Configuration are interpreted as:
 *     BRP, (PROP_SEG + PHASE_SEG1) -> BS1, PHASE_SEG2 -> BS2, SJW.
 * - triple_sampling is ignored (WCH StdPeriph init struct has no such field).
 */
class CH32CAN : public CAN
{
 public:
  /**
   * @brief Construct CH32CAN.
   *
   * @param id          CAN instance ID.
   * @param pool_size   TX pool size (number of ClassicPack entries).
   */
  explicit CH32CAN(ch32_can_id_t id, uint32_t pool_size);
  ~CH32CAN() override;

  /**
   * @brief Initialize filter + IRQ routing. Does NOT force a bitrate; call SetConfig().
   */
  ErrorCode Init();

  /**
   * @brief Set CAN configuration (bit timing + mode). Also (re-)enables IRQs.
   */
  ErrorCode SetConfig(const CAN::Configuration& cfg) override;

  /**
   * @brief CAN clock frequency (Hz). CH32 CAN is on APB1.
   */
  uint32_t GetClockFreq() const override;

  /**
   * @brief Enqueue a ClassicPack for transmission.
   */
  ErrorCode AddMessage(const ClassicPack& pack) override;

  /**
   * @brief Read bus error state and counters.
   */
  ErrorCode GetErrorState(CAN::ErrorState& state) const override;

  /**
   * @brief Process RX interrupt (call from CANx_RX0 / CANx_RX1 handlers).
   */
  void ProcessRxInterrupt();

  /**
   * @brief Process TX interrupt (call from CANx_TX handler).
   */
  void ProcessTxInterrupt();

  /**
   * @brief Process SCE/error interrupt (call from CANx_SCE handler).
   */
  void ProcessErrorInterrupt();

  // Map for ISR dispatch
  static CH32CAN* map[CH32_CAN_NUMBER];  // NOLINT

 private:
  static inline void BuildTxMsg(const ClassicPack& p, CanTxMsg& m);

  void EnableIRQs();
  void DisableIRQs();

  void TxService();

 private:
  CAN_TypeDef* instance_{nullptr};
  ch32_can_id_t id_{CH32_CAN_ID_ERROR};

  uint8_t fifo_{0};
  uint8_t filter_bank_{0};

  LockFreePool<ClassicPack> tx_pool_;

  std::atomic<uint32_t> tx_lock_{0};
  std::atomic<uint32_t> tx_pend_{0};

  // Cache last applied configuration so "0 means keep" can be supported safely
  CAN::Configuration cfg_cache_{};

  // Buffers used in IRQ context
  CanRxMsg rx_msg_{};
  CanTxMsg tx_msg_{};
};
}  // namespace LibXR
