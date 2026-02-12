#pragma once
#include <atomic>
#include <cstdint>

#include "libxr_def.hpp"
#include DEF2STR(LIBXR_CH32_CONFIG_FILE)

namespace LibXR::CH32UsbCanShared
{
using IrqFn = void (*)();

inline std::atomic_bool usb_inited{false};
inline std::atomic_bool can1_inited{false};

static constexpr uint16_t USBD_PMA_BYTES_SOLO = 512;
static constexpr uint16_t USBD_PMA_BYTES_WITHCAN = 384;

// Build-time capabilities / mode detection.
#if defined(RCC_APB1Periph_USB) && !defined(USBHSD)
inline constexpr bool K_HAS_USB_DEV_FS = true;
#else
inline constexpr bool K_HAS_USB_DEV_FS = false;
#endif

#if defined(CAN1)
inline constexpr bool K_HAS_CAN1 = true;
#else
inline constexpr bool K_HAS_CAN1 = false;
#endif

#if defined(CAN2)
inline constexpr bool K_HAS_CAN2 = true;
#else
inline constexpr bool K_HAS_CAN2 = false;
#endif

inline constexpr bool K_SINGLE_CAN1 = K_HAS_CAN1 && !K_HAS_CAN2;
inline constexpr bool K_USB_CAN_SHARE = K_HAS_USB_DEV_FS && K_SINGLE_CAN1;

inline constexpr bool usb_can_share_enabled() { return K_USB_CAN_SHARE; }

inline uint16_t usb_pma_limit_bytes()
{
  if constexpr (!K_USB_CAN_SHARE)
  {
    return USBD_PMA_BYTES_SOLO;
  }

  return can1_inited.load(std::memory_order_acquire) ? USBD_PMA_BYTES_WITHCAN
                                                     : USBD_PMA_BYTES_SOLO;
}

inline std::atomic<IrqFn> usb_irq_cb{nullptr};
inline std::atomic<IrqFn> can1_rx0_cb{nullptr};
inline std::atomic<IrqFn> can1_tx_cb{nullptr};

inline void register_usb_irq(IrqFn fn)
{
  usb_irq_cb.store(fn, std::memory_order_release);
}

inline void register_can1_rx0(IrqFn fn)
{
  if constexpr (K_USB_CAN_SHARE)
  {
    can1_rx0_cb.store(fn, std::memory_order_release);
  }
  else
  {
    (void)fn;
  }
}

inline void register_can1_tx(IrqFn fn)
{
  if constexpr (K_USB_CAN_SHARE)
  {
    can1_tx_cb.store(fn, std::memory_order_release);
  }
  else
  {
    (void)fn;
  }
}

inline bool can1_active()
{
  if constexpr (!K_USB_CAN_SHARE)
  {
    return false;
  }
  return (can1_rx0_cb.load(std::memory_order_acquire) != nullptr) ||
         (can1_tx_cb.load(std::memory_order_acquire) != nullptr);
}
}  // namespace LibXR::CH32UsbCanShared

#ifdef CH32_USBCAN_SHARED_IMPLEMENTATION
#if defined(RCC_APB1Periph_USB) && !defined(USBHSD) && defined(CAN1) && !defined(CAN2)

// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" __attribute__((interrupt)) void USB_LP_CAN1_RX0_IRQHandler(void)
{
  using namespace LibXR::CH32UsbCanShared;

  if (auto fn = usb_irq_cb.load(std::memory_order_acquire))
  {
    fn();
  }
  if (auto fn = can1_rx0_cb.load(std::memory_order_acquire))
  {
    fn();
  }
}

// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" __attribute__((interrupt)) void USB_HP_CAN1_TX_IRQHandler(void)
{
  using namespace LibXR::CH32UsbCanShared;

  if (auto fn = usb_irq_cb.load(std::memory_order_acquire))
  {
    fn();
  }
  if (auto fn = can1_tx_cb.load(std::memory_order_acquire))
  {
    fn();
  }
}

#endif
#endif  // CH32_USBCAN_SHARED_IMPLEMENTATION
