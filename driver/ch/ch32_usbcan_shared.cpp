#include "ch32_usbcan_shared.hpp"

#if defined(RCC_APB1Periph_USB) && defined(CAN1) && !defined(CAN2)

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
