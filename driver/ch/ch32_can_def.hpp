#pragma once

#include <cstdint>

#include "libxr.hpp"
#include DEF2STR(LIBXR_CH32_CONFIG_FILE)

/**
 * @brief CH32 CAN instance ID.
 *
 * Note:
 * - Some CH32 parts expose CAN1 only; some expose CAN1 + CAN2.
 * - Filters may be shared across instances (bxCAN-like). This driver defaults to:
 *   CAN1 -> FIFO0, CAN2 -> FIFO1, and uses filter bank 0/14 respectively.
 */
typedef enum
{
#if defined(CAN1)
  CH32_CAN1,
#endif
#if defined(CAN2)
  CH32_CAN2,
#endif
  CH32_CAN_NUMBER,
  CH32_CAN_ID_ERROR
} ch32_can_id_t;

static_assert(CH32_CAN_NUMBER >= 1, "No CAN instance detected for this MCU");

/**
 * @brief Get CAN peripheral ID from instance address.
 */
ch32_can_id_t CH32_CAN_GetID(CAN_TypeDef* addr);  // NOLINT

/**
 * @brief Get CAN peripheral instance pointer from ID.
 */
CAN_TypeDef* CH32_CAN_GetInstanceID(ch32_can_id_t id);  // NOLINT

/**
 * @brief Default "slave start" filter bank split for dual-CAN.
 *
 * For bxCAN-like designs with 28 filter groups, split at 14 is typical:
 *   banks [0..13] for CAN1, [14..27] for CAN2.
 */
static constexpr uint8_t CH32_CAN_DEFAULT_SLAVE_START_BANK = 14;

static constexpr uint32_t CH32_CAN_RCC_PERIPH_MAP[] = {
#if defined(CAN1)
    RCC_APB1Periph_CAN1,
#endif
#if defined(CAN2)
    RCC_APB1Periph_CAN2,
#endif
};

static constexpr IRQn_Type CH32_CAN_TX_IRQ_MAP[] = {
#if defined(CAN1)
    USB_HP_CAN1_TX_IRQn,
#endif
#if defined(CAN2)
    CAN2_TX_IRQn,
#endif
};

static constexpr IRQn_Type CH32_CAN_RX0_IRQ_MAP[] = {
#if defined(CAN1)
    USB_LP_CAN1_RX0_IRQn,
#endif
#if defined(CAN2)
    CAN2_RX0_IRQn,
#endif
};

static constexpr IRQn_Type CH32_CAN_RX1_IRQ_MAP[] = {
#if defined(CAN1)
    CAN1_RX1_IRQn,
#endif
#if defined(CAN2)
    CAN2_RX1_IRQn,
#endif
};

static constexpr IRQn_Type CH32_CAN_SCE_IRQ_MAP[] = {
#if defined(CAN1)
    CAN1_SCE_IRQn,
#endif
#if defined(CAN2)
    CAN2_SCE_IRQn,
#endif
};
