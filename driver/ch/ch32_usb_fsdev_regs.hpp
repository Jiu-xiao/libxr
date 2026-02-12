// NOLINTBEGIN(cppcoreguidelines-pro-type-cstyle-cast,performance-no-int-to-ptr)
#pragma once

#include <cstdint>

#include "ch32_usb.hpp"

// Only for classic FSDEV/PMA path
#if defined(RCC_APB1Periph_USB)

namespace LibXR::CH32::FSDEV
{

#ifdef USB_BASE
static constexpr uintptr_t REG_BASE = USB_BASE;
#else
static constexpr uintptr_t REG_BASE = 0x40005C00UL;
#endif

static inline volatile uint16_t* cntr()
{
  return reinterpret_cast<volatile uint16_t*>(REG_BASE + 0x40U);
}
static inline volatile uint16_t* istr()
{
  return reinterpret_cast<volatile uint16_t*>(REG_BASE + 0x44U);
}
static inline volatile uint16_t* daddr()
{
  return reinterpret_cast<volatile uint16_t*>(REG_BASE + 0x4CU);
}
static inline volatile uint16_t* btable()
{
  return reinterpret_cast<volatile uint16_t*>(REG_BASE + 0x50U);
}
static inline volatile uint16_t* ep_reg(uint8_t ep)
{
  return reinterpret_cast<volatile uint16_t*>(REG_BASE + static_cast<uintptr_t>(ep) * 4U);
}

// Compatibility bridge (vendor macro -> USB_* macro)
#if !defined(USB_ISTR_CTR) && defined(ISTR_CTR)
#define USB_ISTR_CTR ISTR_CTR
#endif
#if !defined(USB_ISTR_RESET) && defined(ISTR_RESET)
#define USB_ISTR_RESET ISTR_RESET
#endif
#if !defined(USB_ISTR_SUSP) && defined(ISTR_SUSP)
#define USB_ISTR_SUSP ISTR_SUSP
#endif
#if !defined(USB_ISTR_WKUP) && defined(ISTR_WKUP)
#define USB_ISTR_WKUP ISTR_WKUP
#endif
#if !defined(USB_ISTR_EP_ID) && defined(ISTR_EP_ID)
#define USB_ISTR_EP_ID ISTR_EP_ID
#endif

#if !defined(USB_CNTR_FRES) && defined(CNTR_FRES)
#define USB_CNTR_FRES CNTR_FRES
#endif
#if !defined(USB_CNTR_CTRM) && defined(CNTR_CTRM)
#define USB_CNTR_CTRM CNTR_CTRM
#endif
#if !defined(USB_CNTR_RESETM) && defined(CNTR_RESETM)
#define USB_CNTR_RESETM CNTR_RESETM
#endif
#if !defined(USB_CNTR_SUSPM) && defined(CNTR_SUSPM)
#define USB_CNTR_SUSPM CNTR_SUSPM
#endif
#if !defined(USB_CNTR_WKUPM) && defined(CNTR_WKUPM)
#define USB_CNTR_WKUPM CNTR_WKUPM
#endif

#if !defined(USB_DADDR_EF) && defined(DADDR_EF)
#define USB_DADDR_EF DADDR_EF
#endif

#if !defined(USB_EP_CTR_TX) && defined(EP_CTR_TX)
#define USB_EP_CTR_TX EP_CTR_TX
#endif
#if !defined(USB_EP_CTR_RX) && defined(EP_CTR_RX)
#define USB_EP_CTR_RX EP_CTR_RX
#endif
#if !defined(USB_EP_SETUP) && defined(EP_SETUP)
#define USB_EP_SETUP EP_SETUP
#endif
#if !defined(USB_EP_KIND) && defined(EP_KIND)
#define USB_EP_KIND EP_KIND
#endif
#if !defined(USB_EP_T_FIELD) && defined(EP_T_FIELD)
#define USB_EP_T_FIELD EP_T_FIELD
#endif
#if !defined(USB_EPADDR_FIELD) && defined(EPADDR_FIELD)
#define USB_EPADDR_FIELD EPADDR_FIELD
#endif

#ifndef USB_ISTR_CTR
#define USB_ISTR_CTR 0x8000u
#endif
#ifndef USB_ISTR_RESET
#define USB_ISTR_RESET 0x0400u
#endif
#ifndef USB_ISTR_SUSP
#define USB_ISTR_SUSP 0x0800u
#endif
#ifndef USB_ISTR_WKUP
#define USB_ISTR_WKUP 0x1000u
#endif
#ifndef USB_ISTR_EP_ID
#define USB_ISTR_EP_ID 0x000Fu
#endif

#ifndef USB_CNTR_FRES
#define USB_CNTR_FRES 0x0001u
#endif
#ifndef USB_CNTR_CTRM
#define USB_CNTR_CTRM 0x8000u
#endif
#ifndef USB_CNTR_RESETM
#define USB_CNTR_RESETM 0x0400u
#endif
#ifndef USB_CNTR_SUSPM
#define USB_CNTR_SUSPM 0x0800u
#endif
#ifndef USB_CNTR_WKUPM
#define USB_CNTR_WKUPM 0x1000u
#endif

#ifndef USB_DADDR_EF
#define USB_DADDR_EF 0x0080u
#endif

#ifndef USB_EP_CTR_RX
#define USB_EP_CTR_RX 0x8000u
#endif
#ifndef USB_EP_CTR_TX
#define USB_EP_CTR_TX 0x0080u
#endif
#ifndef USB_EP_SETUP
#define USB_EP_SETUP 0x0800u
#endif
#ifndef USB_EP_KIND
#define USB_EP_KIND 0x0100u
#endif
#ifndef USB_EP_T_FIELD
#define USB_EP_T_FIELD 0x0600u
#endif
#ifndef USB_EPADDR_FIELD
#define USB_EPADDR_FIELD 0x000Fu
#endif

#ifndef USB_EP_TX_NAK
#define USB_EP_TX_NAK 0x0020u
#endif
#ifndef USB_EP_RX_VALID
#define USB_EP_RX_VALID 0x3000u
#endif
#ifndef USB_EP_RX_NAK
#define USB_EP_RX_NAK 0x2000u
#endif

#ifndef USB_EPREG_MASK
#define USB_EPREG_MASK                                                           \
  (USB_EP_CTR_RX | USB_EP_SETUP | USB_EP_T_FIELD | USB_EP_KIND | USB_EP_CTR_TX | \
   USB_EPADDR_FIELD)
#endif

static inline void clear_istr(uint16_t mask) { *istr() = static_cast<uint16_t>(~mask); }

// Write EA field correctly (clear old EA bits first)
static inline void set_ep_address(uint8_t ep, uint8_t addr)
{
  volatile uint16_t* reg = ep_reg(ep);
  const uint16_t CUR = *reg;
  const uint16_t V = static_cast<uint16_t>(USB_EP_CTR_RX | USB_EP_CTR_TX |
                                           (CUR & (USB_EPREG_MASK & ~USB_EPADDR_FIELD)) |
                                           (addr & USB_EPADDR_FIELD));
  *reg = V;
}

}  // namespace LibXR::CH32::FSDEV

#endif  // defined(RCC_APB1Periph_USB)

// NOLINTEND(cppcoreguidelines-pro-type-cstyle-cast,performance-no-int-to-ptr)
