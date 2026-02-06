#include <cstdint>
#include <cstring>

#include "ch32_usb_endpoint.hpp"
#include "ep.hpp"

using namespace LibXR;

#if defined(RCC_APB1Periph_USB) && !defined(USBHSD)

//------------------------------------------------------------------------------
// Classic USB device (FSDEV / PMA) register access helpers
//------------------------------------------------------------------------------

// Prefer vendor-provided bases if available.
#ifdef USB_BASE
static constexpr uintptr_t REG_BASE = USB_BASE;
#else
static constexpr uintptr_t REG_BASE = 0x40005C00UL;
#endif

// PMA base differs across families; demo uses 0x40006000.
#ifdef PMAAddr
static constexpr uintptr_t PMA_BASE = PMAAddr;
#else
static constexpr uintptr_t PMA_BASE = 0x40006000UL;
#endif

static inline volatile uint16_t* CNTR()
{
  return reinterpret_cast<volatile uint16_t*>(REG_BASE + 0x40U);
}
static inline volatile uint16_t* ISTR()
{
  return reinterpret_cast<volatile uint16_t*>(REG_BASE + 0x44U);
}
static inline volatile uint16_t* FNR()
{
  return reinterpret_cast<volatile uint16_t*>(REG_BASE + 0x48U);
}
static inline volatile uint16_t* DADDR()
{
  return reinterpret_cast<volatile uint16_t*>(REG_BASE + 0x4CU);
}
static inline volatile uint16_t* BTABLE()
{
  return reinterpret_cast<volatile uint16_t*>(REG_BASE + 0x50U);
}

static inline volatile uint16_t* EP_REG(uint8_t ep)
{
  // Endpoint registers are 16-bit, spaced by 4 bytes.
  return reinterpret_cast<volatile uint16_t*>(REG_BASE + (static_cast<uintptr_t>(ep) * 4U));
}

//------------------------------------------------------------------------------
// Bit definitions (use vendor macros if present; otherwise fall back)
//------------------------------------------------------------------------------

// Compatibility bridge: if the vendor headers provide the classic ST USBLIB names
// (e.g., EP_CTR_RX / ISTR_RESET), map them to the USB_* names used below.
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
#if !defined(USB_ISTR_DIR) && defined(ISTR_DIR)
#define USB_ISTR_DIR ISTR_DIR
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

#if !defined(USB_EP_CTR_RX) && defined(EP_CTR_RX)
#define USB_EP_CTR_RX EP_CTR_RX
#endif
#if !defined(USB_EP_DTOG_RX) && defined(EP_DTOG_RX)
#define USB_EP_DTOG_RX EP_DTOG_RX
#endif
#if !defined(USB_EPRX_STAT) && defined(EPRX_STAT)
#define USB_EPRX_STAT EPRX_STAT
#endif
#if !defined(USB_EP_SETUP) && defined(EP_SETUP)
#define USB_EP_SETUP EP_SETUP
#endif
#if !defined(USB_EP_TYPE) && defined(EP_TYPE)
#define USB_EP_TYPE EP_TYPE
#endif
#if !defined(USB_EP_KIND) && defined(EP_KIND)
#define USB_EP_KIND EP_KIND
#endif
#if !defined(USB_EP_CTR_TX) && defined(EP_CTR_TX)
#define USB_EP_CTR_TX EP_CTR_TX
#endif
#if !defined(USB_EP_DTOG_TX) && defined(EP_DTOG_TX)
#define USB_EP_DTOG_TX EP_DTOG_TX
#endif
#if !defined(USB_EPTX_STAT) && defined(EPTX_STAT)
#define USB_EPTX_STAT EPTX_STAT
#endif
#if !defined(USB_EPADDR_FIELD) && defined(EPADDR_FIELD)
#define USB_EPADDR_FIELD EPADDR_FIELD
#endif

#if !defined(USB_EP_TX_DIS) && defined(EP_TX_DIS)
#define USB_EP_TX_DIS EP_TX_DIS
#endif
#if !defined(USB_EP_TX_STALL) && defined(EP_TX_STALL)
#define USB_EP_TX_STALL EP_TX_STALL
#endif
#if !defined(USB_EP_TX_NAK) && defined(EP_TX_NAK)
#define USB_EP_TX_NAK EP_TX_NAK
#endif
#if !defined(USB_EP_TX_VALID) && defined(EP_TX_VALID)
#define USB_EP_TX_VALID EP_TX_VALID
#endif
#if !defined(USB_EP_RX_DIS) && defined(EP_RX_DIS)
#define USB_EP_RX_DIS EP_RX_DIS
#endif
#if !defined(USB_EP_RX_STALL) && defined(EP_RX_STALL)
#define USB_EP_RX_STALL EP_RX_STALL
#endif
#if !defined(USB_EP_RX_NAK) && defined(EP_RX_NAK)
#define USB_EP_RX_NAK EP_RX_NAK
#endif
#if !defined(USB_EP_RX_VALID) && defined(EP_RX_VALID)
#define USB_EP_RX_VALID EP_RX_VALID
#endif
//------------------------------------------------------------------------------

#ifndef USB_ISTR_CTR
#define USB_ISTR_CTR ((uint16_t)0x8000U)
#endif
#ifndef USB_ISTR_DIR
#define USB_ISTR_DIR ((uint16_t)0x0010U)
#endif
#ifndef USB_ISTR_EP_ID
#define USB_ISTR_EP_ID ((uint16_t)0x000FU)
#endif
#ifndef USB_ISTR_RESET
#define USB_ISTR_RESET ((uint16_t)0x0400U)
#endif
#ifndef USB_ISTR_SUSP
#define USB_ISTR_SUSP ((uint16_t)0x0800U)
#endif
#ifndef USB_ISTR_WKUP
#define USB_ISTR_WKUP ((uint16_t)0x1000U)
#endif
#ifndef USB_CLR_RESET
#define USB_CLR_RESET (~USB_ISTR_RESET)
#endif
#ifndef USB_CLR_SUSP
#define USB_CLR_SUSP (~USB_ISTR_SUSP)
#endif
#ifndef USB_CLR_WKUP
#define USB_CLR_WKUP (~USB_ISTR_WKUP)
#endif

#ifndef USB_CNTR_CTRM
#define USB_CNTR_CTRM ((uint16_t)0x8000U)
#endif
#ifndef USB_CNTR_RESETM
#define USB_CNTR_RESETM ((uint16_t)0x0400U)
#endif
#ifndef USB_CNTR_SUSPM
#define USB_CNTR_SUSPM ((uint16_t)0x0800U)
#endif
#ifndef USB_CNTR_WKUPM
#define USB_CNTR_WKUPM ((uint16_t)0x1000U)
#endif
#ifndef USB_CNTR_PDWN
#define USB_CNTR_PDWN ((uint16_t)0x0002U)
#endif
#ifndef USB_CNTR_FRES
#define USB_CNTR_FRES ((uint16_t)0x0001U)
#endif

#ifndef USB_DADDR_EF
#define USB_DADDR_EF ((uint8_t)0x80U)
#endif

#ifndef USB_EP_CTR_RX
#define USB_EP_CTR_RX ((uint16_t)0x8000U)
#endif
#ifndef USB_EP_DTOG_RX
#define USB_EP_DTOG_RX ((uint16_t)0x4000U)
#endif
#ifndef USB_EPRX_STAT
#define USB_EPRX_STAT ((uint16_t)0x3000U)
#endif
#ifndef USB_EP_SETUP
#define USB_EP_SETUP ((uint16_t)0x0800U)
#endif
#ifndef USB_EP_T_FIELD
#define USB_EP_T_FIELD ((uint16_t)0x0600U)
#endif
#ifndef USB_EP_KIND
#define USB_EP_KIND ((uint16_t)0x0100U)
#endif
#ifndef USB_EP_CTR_TX
#define USB_EP_CTR_TX ((uint16_t)0x0080U)
#endif
#ifndef USB_EP_DTOG_TX
#define USB_EP_DTOG_TX ((uint16_t)0x0040U)
#endif
#ifndef USB_EPTX_STAT
#define USB_EPTX_STAT ((uint16_t)0x0030U)
#endif
#ifndef USB_EPADDR_FIELD
#define USB_EPADDR_FIELD ((uint16_t)0x000FU)
#endif
#ifndef USB_EPREG_MASK
#define USB_EPREG_MASK (USB_EP_CTR_RX | USB_EP_SETUP | USB_EP_T_FIELD | USB_EP_KIND | USB_EP_CTR_TX | USB_EPADDR_FIELD)
#endif

#ifndef USB_EP_CONTROL
#define USB_EP_CONTROL ((uint16_t)0x0200U)
#endif
#ifndef USB_EP_BULK
#define USB_EP_BULK ((uint16_t)0x0000U)
#endif
#ifndef USB_EP_INTERRUPT
#define USB_EP_INTERRUPT ((uint16_t)0x0600U)
#endif
#ifndef USB_EP_ISOCHRONOUS
#define USB_EP_ISOCHRONOUS ((uint16_t)0x0400U)
#endif

#ifndef USB_EP_TX_DIS
#define USB_EP_TX_DIS ((uint16_t)0x0000U)
#endif
#ifndef USB_EP_TX_STALL
#define USB_EP_TX_STALL ((uint16_t)0x0010U)
#endif
#ifndef USB_EP_TX_NAK
#define USB_EP_TX_NAK ((uint16_t)0x0020U)
#endif
#ifndef USB_EP_TX_VALID
#define USB_EP_TX_VALID ((uint16_t)0x0030U)
#endif
#ifndef USB_EPTX_DTOG1
#define USB_EPTX_DTOG1 ((uint16_t)0x0010U)
#endif
#ifndef USB_EPTX_DTOG2
#define USB_EPTX_DTOG2 ((uint16_t)0x0020U)
#endif
#ifndef USB_EPTX_DTOGMASK
#define USB_EPTX_DTOGMASK (USB_EPTX_STAT | USB_EPREG_MASK)
#endif

#ifndef USB_EP_RX_DIS
#define USB_EP_RX_DIS ((uint16_t)0x0000U)
#endif
#ifndef USB_EP_RX_STALL
#define USB_EP_RX_STALL ((uint16_t)0x1000U)
#endif
#ifndef USB_EP_RX_NAK
#define USB_EP_RX_NAK ((uint16_t)0x2000U)
#endif
#ifndef USB_EP_RX_VALID
#define USB_EP_RX_VALID ((uint16_t)0x3000U)
#endif
#ifndef USB_EPRX_DTOG1
#define USB_EPRX_DTOG1 ((uint16_t)0x1000U)
#endif
#ifndef USB_EPRX_DTOG2
#define USB_EPRX_DTOG2 ((uint16_t)0x2000U)
#endif
#ifndef USB_EPRX_DTOGMASK
#define USB_EPRX_DTOGMASK (USB_EPRX_STAT | USB_EPREG_MASK)
#endif

// BTABLE layout: same as STM32 FSDEV
struct BTableEntry
{
  uint16_t addr_tx;
  uint16_t count_tx;
  uint16_t addr_rx;
  uint16_t count_rx;
};

static inline volatile BTableEntry* BTABLE_ENTRIES()
{
  return reinterpret_cast<volatile BTableEntry*>(PMA_BASE + ((*BTABLE()) & 0xFFF8U));
}

//------------------------------------------------------------------------------
// PMA access
//------------------------------------------------------------------------------

static inline void PMAWrite(uint16_t pma_offset, const void* src, size_t len)
{
  const uint8_t* s = reinterpret_cast<const uint8_t*>(src);
  volatile uint16_t* p = reinterpret_cast<volatile uint16_t*>(PMA_BASE + pma_offset);

  // PMA is 16-bit addressed (word). Each word holds 2 bytes.
  for (size_t i = 0; i < len; i += 2)
  {
    uint16_t w = s[i];
    if (i + 1 < len)
    {
      w |= static_cast<uint16_t>(s[i + 1]) << 8;
    }
    *p++ = w;
  }
}

static inline void PMARead(void* dst, uint16_t pma_offset, size_t len)
{
  uint8_t* d = reinterpret_cast<uint8_t*>(dst);
  const volatile uint16_t* p = reinterpret_cast<const volatile uint16_t*>(PMA_BASE + pma_offset);

  for (size_t i = 0; i < len; i += 2)
  {
    const uint16_t w = *p++;
    d[i] = static_cast<uint8_t>(w & 0xFFU);
    if (i + 1 < len)
    {
      d[i + 1] = static_cast<uint8_t>((w >> 8) & 0xFFU);
    }
  }
}

//------------------------------------------------------------------------------
// Endpoint register helpers (STAT/DTOG are "write-1-to-toggle")
//------------------------------------------------------------------------------

static inline void SetTxStatus(uint8_t ep, uint16_t status)
{
  const uint16_t cur = *EP_REG(ep);
  uint16_t reg = cur & USB_EPTX_DTOGMASK;

  if ((status & USB_EPTX_DTOG1) != 0)
  {
    reg ^= USB_EPTX_DTOG1;
  }
  if ((status & USB_EPTX_DTOG2) != 0)
  {
    reg ^= USB_EPTX_DTOG2;
  }

  // Preserve CTR bits (write 1 keeps them set)
  reg |= (cur & (USB_EP_CTR_RX | USB_EP_CTR_TX));
  *EP_REG(ep) = reg;
}

static inline void SetRxStatus(uint8_t ep, uint16_t status)
{
  const uint16_t cur = *EP_REG(ep);
  uint16_t reg = cur & USB_EPRX_DTOGMASK;

  if ((status & USB_EPRX_DTOG1) != 0)
  {
    reg ^= USB_EPRX_DTOG1;
  }
  if ((status & USB_EPRX_DTOG2) != 0)
  {
    reg ^= USB_EPRX_DTOG2;
  }

  reg |= (cur & (USB_EP_CTR_RX | USB_EP_CTR_TX));
  *EP_REG(ep) = reg;
}

static inline void ClearCtrTx(uint8_t ep)
{
  uint16_t cur = *EP_REG(ep);
  cur &= static_cast<uint16_t>(~USB_EP_CTR_TX);
  // Preserve CTR_RX by writing it back if set.
  cur |= (cur & USB_EP_CTR_RX);
  *EP_REG(ep) = cur;
}

static inline void ClearCtrRx(uint8_t ep)
{
  uint16_t cur = *EP_REG(ep);
  cur &= static_cast<uint16_t>(~USB_EP_CTR_RX);
  cur |= (cur & USB_EP_CTR_TX);
  *EP_REG(ep) = cur;
}

static inline uint16_t GetRxCountFromBtable(uint8_t ep)
{
  return BTABLE_ENTRIES()[ep].count_rx & 0x03FFU;
}

//------------------------------------------------------------------------------
// Simple PMA allocator (byte offsets in PMA)
//------------------------------------------------------------------------------

static uint16_t g_pma_next = 0x0040U;  // keep space for BTABLE

void CH32EndpointDevFs::ResetPMAAllocator()
{
  g_pma_next = 0x0040U;
}

static inline uint16_t AllocPma(size_t bytes)
{
  uint16_t addr = g_pma_next;
  uint16_t inc = static_cast<uint16_t>((bytes + 1U) & ~1U);  // even
  g_pma_next = static_cast<uint16_t>(g_pma_next + inc);
  return addr;
}

//------------------------------------------------------------------------------
// CH32EndpointDevFs
//------------------------------------------------------------------------------

CH32EndpointDevFs::CH32EndpointDevFs(EPNumber ep_num, Direction dir, LibXR::RawData buffer,
                                     bool is_isochronous)
    : Endpoint(ep_num, dir, buffer), is_isochronous_(is_isochronous)
{
  const uint8_t ep_i = static_cast<uint8_t>(EPNumberToInt8(GetNumber()));
  map_dev_fs_[ep_i][static_cast<uint8_t>(dir)] = this;
}

void CH32EndpointDevFs::Configure(const Config& cfg)
{
  auto& ep_cfg = GetConfig();
  ep_cfg = cfg;

  const uint8_t ep_i = static_cast<uint8_t>(EPNumberToInt8(GetNumber()));

  // Allocate PMA for this direction if not yet allocated.
  if (pma_addr_ == 0)
  {
    pma_addr_ = AllocPma(GetBuffer().size_);
  }

  // Program BTABLE
  volatile BTableEntry* bt = BTABLE_ENTRIES();

  if (GetDirection() == Direction::IN)
  {
    bt[ep_i].addr_tx = pma_addr_;
    bt[ep_i].count_tx = 0;
  }
  else
  {
    bt[ep_i].addr_rx = pma_addr_;

    // RX count encoding: use 2-byte blocks for <= 62 bytes, else 32-byte blocks.
    const uint16_t mps = static_cast<uint16_t>(GetBuffer().size_);
    uint16_t cnt;
    if (mps <= 62)
    {
      const uint16_t blocks = static_cast<uint16_t>((mps + 1U) / 2U);
      cnt = static_cast<uint16_t>(blocks << 10);
    }
    else
    {
      const uint16_t blocks = static_cast<uint16_t>((mps + 31U) / 32U);
      cnt = static_cast<uint16_t>(0x8000U | ((blocks - 1U) << 10));
    }
    bt[ep_i].count_rx = cnt;
  }

  // Set endpoint type
  uint16_t epr = *EP_REG(ep_i);
  epr &= static_cast<uint16_t>(~USB_EP_T_FIELD);

  switch (ep_cfg.type)
  {
    case Type::CONTROL:
      epr |= USB_EP_CONTROL;
      break;
    case Type::BULK:
      epr |= USB_EP_BULK;
      break;
    case Type::INTERRUPT:
      epr |= USB_EP_INTERRUPT;
      break;
    case Type::ISOCHRONOUS:
      epr |= USB_EP_ISOCHRONOUS;
      break;
    default:
      epr |= USB_EP_BULK;
      break;
  }

  // Preserve CTR flags when writing.
  const uint16_t cur = *EP_REG(ep_i);
  epr = static_cast<uint16_t>((epr & USB_EPREG_MASK) | (cur & (USB_EP_CTR_RX | USB_EP_CTR_TX)));
  *EP_REG(ep_i) = epr;

  // Default to NAK; stack will arm transfers.
  if (GetDirection() == Direction::IN)
  {
    SetTxStatus(ep_i, USB_EP_TX_NAK);
  }
  else
  {
    SetRxStatus(ep_i, USB_EP_RX_NAK);
  }
}

void CH32EndpointDevFs::Close()
{
  const uint8_t ep_i = static_cast<uint8_t>(EPNumberToInt8(GetNumber()));
  if (GetDirection() == Direction::IN)
  {
    SetTxStatus(ep_i, USB_EP_TX_DIS);
  }
  else
  {
    SetRxStatus(ep_i, USB_EP_RX_DIS);
  }
}

ErrorCode CH32EndpointDevFs::Transfer(size_t size)
{
  if (size > GetBuffer().size_)
  {
    return ErrorCode::OUT_OF_RANGE;
  }

  const uint8_t ep_i = static_cast<uint8_t>(EPNumberToInt8(GetNumber()));
  last_transfer_size_ = size;

  if (GetDirection() == Direction::IN)
  {
    PMAWrite(pma_addr_, GetBuffer().addr_, size);
    BTABLE_ENTRIES()[ep_i].count_tx = static_cast<uint16_t>(size);
    SetTxStatus(ep_i, USB_EP_TX_VALID);
  }
  else
  {
    SetRxStatus(ep_i, USB_EP_RX_VALID);
  }

  SetState(State::BUSY);
  return ErrorCode::OK;
}

void CH32EndpointDevFs::CopyRxDataToBuffer(size_t size)
{
  if (size > GetBuffer().size_)
  {
    size = GetBuffer().size_;
  }
  PMARead(GetBuffer().addr_, pma_addr_, size);
}

void CH32EndpointDevFs::TransferComplete(size_t size)
{
  const uint8_t ep_i = static_cast<uint8_t>(EPNumberToInt8(GetNumber()));

  if (GetDirection() == Direction::OUT)
  {
    const uint16_t rx_cnt = GetRxCountFromBtable(ep_i);
    size_t n = rx_cnt;
    if (n > GetBuffer().size_)
    {
      n = GetBuffer().size_;
    }

    PMARead(GetBuffer().addr_, pma_addr_, n);
    size = n;

    // Default back to NAK; upper layer will re-arm if needed.
    SetRxStatus(ep_i, USB_EP_RX_NAK);
  }
  else
  {
    // Default back to NAK after IN complete.
    SetTxStatus(ep_i, USB_EP_TX_NAK);
    size = last_transfer_size_;
  }

  SetState(State::IDLE);
  OnTransferCompleteCallback(true, size);
}

ErrorCode CH32EndpointDevFs::Stall()
{
  const uint8_t ep_i = static_cast<uint8_t>(EPNumberToInt8(GetNumber()));
  if (GetDirection() == Direction::IN)
  {
    SetTxStatus(ep_i, USB_EP_TX_STALL);
  }
  else
  {
    SetRxStatus(ep_i, USB_EP_RX_STALL);
  }
  return ErrorCode::OK;
}

ErrorCode CH32EndpointDevFs::ClearStall()
{
  const uint8_t ep_i = static_cast<uint8_t>(EPNumberToInt8(GetNumber()));
  if (GetDirection() == Direction::IN)
  {
    SetTxStatus(ep_i, USB_EP_TX_NAK);
  }
  else
  {
    SetRxStatus(ep_i, USB_EP_RX_NAK);
  }
  return ErrorCode::OK;
}

void CH32EndpointDevFs::SwitchBuffer()
{
  // Classic PMA controller has no DMA double-buffer mechanism exposed like OTG.
}

//------------------------------------------------------------------------------
// ISR-facing static helpers
//------------------------------------------------------------------------------

void CH32EndpointDevFs::SetEpTxStatus(uint8_t ep, uint16_t status)
{
  SetTxStatus(ep, status);
}

void CH32EndpointDevFs::SetEpRxStatus(uint8_t ep, uint16_t status)
{
  SetRxStatus(ep, status);
}

void CH32EndpointDevFs::ClearEpCtrTx(uint8_t ep)
{
  ClearCtrTx(ep);
}

void CH32EndpointDevFs::ClearEpCtrRx(uint8_t ep)
{
  ClearCtrRx(ep);
}

uint16_t CH32EndpointDevFs::GetRxCount(uint8_t ep)
{
  return GetRxCountFromBtable(ep);
}

#endif  // defined(RCC_APB1Periph_USB) && !USBHSD
