// NOLINTBEGIN(cppcoreguidelines-pro-type-cstyle-cast,performance-no-int-to-ptr)
// ch32_usb_otghs_h41x.cpp  (OTG HS, CH32H41x)
#include "ch32h41x_usb_otghs.hpp"
#include "ch32_usb_rcc.hpp"
#include "ep.hpp"

using namespace LibXR;
using namespace LibXR::USB;

#if defined(USBHSD) && defined(LIBXR_CH32_IS_H41X)

extern "C" __attribute__((weak)) void CH32H41xUsbHsDebugStage(uint32_t)
{
}

extern "C" __attribute__((weak)) void CH32H41xUsbHsDebugSnapshot(uint32_t, uint32_t,
                                                             uint32_t, uint32_t,
                                                             uint32_t, uint32_t)
{
}

namespace
{

using OtgHsEndpointMap = decltype(LibXR::CH32H41xEndpointOtgHs::map_otg_hs_);

constexpr uint8_t OUT_IDX = static_cast<uint8_t>(LibXR::USB::Endpoint::Direction::OUT);
constexpr uint8_t IN_IDX = static_cast<uint8_t>(LibXR::USB::Endpoint::Direction::IN);

static void EnableUsbHsControllerClock()
{
#if defined(RCC_AHBPeriph_USBHS)
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBHS, ENABLE);
#elif defined(RCC_HBPeriph_USBHS)
  RCC_HBPeriphClockCmd(RCC_HBPeriph_USBHS, ENABLE);
#endif
}

static void DisableUsbHsControllerClock()
{
#if defined(RCC_AHBPeriph_USBHS)
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBHS, DISABLE);
#elif defined(RCC_HBPeriph_USBHS)
  RCC_HBPeriphClockCmd(RCC_HBPeriph_USBHS, DISABLE);
#endif
}

static void PrepareUsbHsPinsForCH32H41x()
{
#if defined(RCC_HB2Periph_AFIO) || defined(RCC_HB2Periph_GPIOB)
  uint32_t hb2_bits = 0u;
#if defined(RCC_HB2Periph_AFIO)
  hb2_bits |= RCC_HB2Periph_AFIO;
#endif
#if defined(RCC_HB2Periph_GPIOB)
  hb2_bits |= RCC_HB2Periph_GPIOB;
#endif
  if (hb2_bits != 0u)
  {
    RCC_HB2PeriphClockCmd(hb2_bits, ENABLE);
  }
#elif defined(RCC_APB2Periph_AFIO) || defined(RCC_APB2Periph_GPIOB)
  uint32_t apb2_bits = 0u;
#if defined(RCC_APB2Periph_AFIO)
  apb2_bits |= RCC_APB2Periph_AFIO;
#endif
#if defined(RCC_APB2Periph_GPIOB)
  apb2_bits |= RCC_APB2Periph_GPIOB;
#endif
  if (apb2_bits != 0u)
  {
    RCC_APB2PeriphClockCmd(apb2_bits, ENABLE);
  }
#endif
#if defined(GPIO_Remap_SWJ_Disable)
  // CH32H41x USBHS device examples explicitly release the SWJ mux before enabling
  // USBHS; otherwise the shared HS pins never leave the debug function.
  // CH32H41x 的 USBHS 设备例程会先释放 SWJ 复用，否则与调试口复用的 HS
  // 管脚不会切回 USB 功能。
  GPIO_PinRemapConfig(GPIO_Remap_SWJ_Disable, ENABLE);
#endif
}

static void AllocateUsbHsIrqToV3f()
{
#if defined(Core_ID_V3F)
  constexpr uint32_t kPficAllocCount = 256u;
  const auto irqn = static_cast<uint32_t>(USBHS_IRQn);
  if (irqn < kPficAllocCount)
  {
    NVIC->IALLOCR[irqn] = Core_ID_V3F;
  }
#endif
}

static inline volatile uint8_t* get_tx_control_addr(USB::Endpoint::EPNumber ep_num)
{
  return reinterpret_cast<volatile uint8_t*>(
      reinterpret_cast<volatile uint8_t*>(&USBHSD->UEP0_TX_CTRL) +
      static_cast<int>(ep_num) * 4);
}

static inline volatile uint8_t* get_rx_control_addr(USB::Endpoint::EPNumber ep_num)
{
  return reinterpret_cast<volatile uint8_t*>(
      reinterpret_cast<volatile uint8_t*>(&USBHSD->UEP0_TX_CTRL) +
      static_cast<int>(ep_num) * 4 + 1);
}

static uint16_t GetOtgHsRxLen(uint8_t ep_num)
{
  switch (ep_num)
  {
    case 0u:
      return USBHSD->UEP0_RX_LEN;
    case 1u:
      return USBHSD->UEP1_RX_LEN;
    case 2u:
      return USBHSD->UEP2_RX_LEN;
    case 3u:
      return USBHSD->UEP3_RX_LEN;
    case 4u:
      return USBHSD->UEP4_RX_LEN;
    case 5u:
      return USBHSD->UEP5_RX_LEN;
    case 6u:
      return USBHSD->UEP6_RX_LEN;
    case 7u:
      return USBHSD->UEP7_RX_LEN;
    default:
      return 0u;
  }
}

static bool IsOtgHsTxDone(uint8_t ep_num)
{
  return (*get_tx_control_addr(static_cast<USB::Endpoint::EPNumber>(ep_num)) &
          USBHS_UEP_T_DONE) == USBHS_UEP_T_DONE;
}

static bool IsOtgHsRxDone(uint8_t ep_num)
{
  return (*get_rx_control_addr(static_cast<USB::Endpoint::EPNumber>(ep_num)) &
          USBHS_UEP_R_DONE) == USBHS_UEP_R_DONE;
}

static void ResetEp0State(OtgHsEndpointMap& map)
{
  auto* out0 = map[0][OUT_IDX];
  auto* in0 = map[0][IN_IDX];

  ASSERT(out0 != nullptr);
  ASSERT(in0 != nullptr);

  out0->SetState(LibXR::USB::Endpoint::State::IDLE);
  out0->tog0_ = true;
  out0->tog1_ = false;
  in0->SetState(LibXR::USB::Endpoint::State::IDLE);
  in0->tog0_ = true;
  in0->tog1_ = false;

  // After reset/suspend recovery, EP0 must be ready for the next setup packet.
  // reset/suspend 恢复后，EP0 必须回到等待下一包 setup 的状态。
  USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_TOG_DATA1 | USBHS_UEP_T_RES_NAK;
  USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_ACK;
}

static void RestoreUsbHsEndpointState()
{
  auto& ep_map = LibXR::CH32H41xEndpointOtgHs::map_otg_hs_;
  constexpr uint8_t N_EP =
      static_cast<uint8_t>(LibXR::CH32H41xEndpointOtgHs::EP_OTG_HS_MAX_SIZE);

  bool rearm_out[N_EP] = {};

  for (uint8_t ep = 0; ep < N_EP; ++ep)
  {
    auto* out = ep_map[ep][OUT_IDX];
    auto* in = ep_map[ep][IN_IDX];

    if (out != nullptr && out->GetState() != LibXR::USB::Endpoint::State::DISABLED)
    {
      rearm_out[ep] = out->GetState() == LibXR::USB::Endpoint::State::BUSY;
      out->Configure({out->GetDirection(), out->GetType(), out->MaxPacketSize(),
                      out->UseDoubleBuffer()});
    }

    if (in != nullptr && in->GetState() != LibXR::USB::Endpoint::State::DISABLED)
    {
      in->Configure({in->GetDirection(), in->GetType(), in->MaxPacketSize(),
                     in->UseDoubleBuffer()});
    }
  }

  ResetEp0State(ep_map);

  for (uint8_t ep = 1; ep < N_EP; ++ep)
  {
    if (!rearm_out[ep])
    {
      continue;
    }

    auto* out = ep_map[ep][OUT_IDX];
    if (out == nullptr)
    {
      continue;
    }

    (void)out->Transfer(out->MaxTransferSize());
  }
}

static void PrepareEp0ForSetup(OtgHsEndpointMap& map)
{
  auto* out0 = map[0][OUT_IDX];
  auto* in0 = map[0][IN_IDX];

  ASSERT(out0 != nullptr);
  ASSERT(in0 != nullptr);

  // A fresh SETUP cancels the previous control transfer, so EP0 must be re-armed
  // to the default control endpoint shape before the setup packet is dispatched.
  // 新的 SETUP 会中断前一笔控制传输，因此在分发 setup 包之前，
  // 必须先把 EP0 恢复成默认控制端点形态。
  out0->SetState(LibXR::USB::Endpoint::State::IDLE);
  in0->SetState(LibXR::USB::Endpoint::State::IDLE);
  out0->tog0_ = true;
  out0->tog1_ = false;
  in0->tog0_ = true;
  in0->tog1_ = false;
  USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_TOG_DATA1 | USBHS_UEP_T_RES_NAK;
  USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_NAK;
}

static void HandleTransferOut(CH32H41xEndpointOtgHs* ep_out, uint8_t ep_num,
                              uint8_t int_st)
{
  if (ep_out == nullptr)
  {
    return;
  }

  UNUSED(int_st);
  const bool busy = (ep_out->GetState() == LibXR::USB::Endpoint::State::BUSY);
  const bool rx_done = IsOtgHsRxDone(ep_num);
  const uint16_t rx_len = GetOtgHsRxLen(ep_num);
  if ((busy || ep_num == 0u) && rx_done)
  {
    ep_out->TransferComplete(rx_len);
  }
}

static void HandleTransferIn(CH32H41xEndpointOtgHs* ep_in, uint8_t ep_num,
                             uint8_t int_st)
{
  if (ep_in == nullptr)
  {
    return;
  }

  UNUSED(int_st);
  if (ep_in->GetState() == LibXR::USB::Endpoint::State::BUSY &&
      IsOtgHsTxDone(ep_num))
  {
    ep_in->TransferComplete(0u);
  }
}

static void HandleTransferToken(OtgHsEndpointMap& map, uint8_t int_st)
{
  const uint8_t ep_num = int_st & USBHS_UDIS_EP_ID_MASK;
  auto& ep = map[ep_num];
  if ((int_st & USBHS_UDIS_EP_DIR) != 0u)
  {
    HandleTransferIn(ep[IN_IDX], ep_num, int_st);
  }
  else
  {
    HandleTransferOut(ep[OUT_IDX], ep_num, int_st);
  }
}

static void ClearPendingOtgHsInterrupts()
{
  while (true)
  {
    const uint16_t INTFGST = *reinterpret_cast<volatile uint16_t*>(
        reinterpret_cast<uintptr_t>(&USBHSD->INT_FG));
    const uint8_t INTFLAG = static_cast<uint8_t>(INTFGST & 0x00FFu);
    if (INTFLAG == 0u)
    {
      break;
    }
    USBHSD->INT_FG = INTFLAG;

    // This loop drains only the bits already latched in INT_FG. If hardware
    // observes another bus event later, it will assert a new IRQ and the next
    // handler entry will clear it.
    // 这个循环只清理 INT_FG 里已经锁存的位；如果之后又出现新的总线事件，
    // 硬件会重新触发 IRQ，由下一次进入 handler 时再清。
  }
}

static void CaptureUsbHsSnapshot(uint8_t intflag, uint8_t intst)
{
  CH32H41xUsbHsDebugSnapshot(intflag, intst, USBHSD->MIS_ST, USBHSD->BUS,
                         USBHSD->FRAME_NO, USBHSD->CONTROL);
}

}  // namespace

// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" __attribute__((interrupt("WCH-Interrupt-fast"))) void USBHS_IRQHandler(void)
{
  auto* usb = LibXR::CH32H41xUSBOtgHS::self_;
  if (usb == nullptr || !usb->IsInited())
  {
    ClearPendingOtgHsInterrupts();
    return;
  }

  auto& map = LibXR::CH32H41xEndpointOtgHs::map_otg_hs_;

  // Handle order matters: recover bus-level events first, then settle EP0 setup
  // arbitration, and finally dispatch ordinary token completions.
  // 处理顺序很重要：先恢复总线级事件，再收束 EP0 setup 仲裁，
  // 最后分发普通 token 完成事件。
  while (true)
  {
    const uint8_t INTFLAG = USBHSD->INT_FG;
    const uint8_t INTST = USBHSD->INT_ST;

    if (INTFLAG == 0)
    {
      break;
    }

    uint8_t clear_mask = 0;

    // 1) Bus-level recovery: reset/suspend must rebuild EP0 first.
    // 1) 总线级恢复：reset/suspend 必须优先重建 EP0。
    if (INTFLAG & USBHS_UDIF_BUS_RST)
    {
      CaptureUsbHsSnapshot(INTFLAG, INTST);
      CH32H41xUsbHsDebugStage(0x4320u);
      USBHSD->DEV_AD = 0;
      usb->OnBusReset(true);
      USBHSD->UEP_TX_EN = 0u;
      USBHSD->UEP_RX_EN = 0u;
      USBHSD->UEP_TX_ISO = 0u;
      USBHSD->UEP_RX_ISO = 0u;
      RestoreUsbHsEndpointState();
      clear_mask |= USBHS_UDIF_BUS_RST;
    }

    if (INTFLAG & USBHS_UDIF_SUSPEND)
    {
      CaptureUsbHsSnapshot(INTFLAG, INTST);
      CH32H41xUsbHsDebugStage(0x4321u);
      clear_mask |= USBHS_UDIF_SUSPEND;
    }

    if (INTFLAG & USBHS_UDIF_LINK_RDY)
    {
      CaptureUsbHsSnapshot(INTFLAG, INTST);
      CH32H41xUsbHsDebugStage(0x4322u);
      clear_mask |= USBHS_UDIF_LINK_RDY;
    }

    if ((INTFLAG & USBHS_UDIF_TRANSFER) != 0u)
    {
      const uint8_t ep_num = INTST & USBHS_UDIS_EP_ID_MASK;
      if (((INTST & USBHS_UDIS_EP_DIR) == 0u) && ep_num == 0u &&
          ((USBHSD->UEP0_RX_CTRL & USBHS_UEP_R_SETUP_IS) == USBHS_UEP_R_SETUP_IS))
      {
        CaptureUsbHsSnapshot(INTFLAG, INTST);
        CH32H41xUsbHsDebugStage(0x4323u);
        USBHSD->UEP0_RX_CTRL &= static_cast<uint8_t>(~USBHS_UEP_R_DONE);
        PrepareEp0ForSetup(map);

        auto* out0 = map[0][OUT_IDX];
        ASSERT(out0 != nullptr);
        const auto* setup =
            reinterpret_cast<const LibXR::USB::SetupPacket*>(out0->GetBuffer().addr_);
        usb->OnSetupPacket(true, setup);
      }
      else
      {
        CaptureUsbHsSnapshot(INTFLAG, INTST);
        CH32H41xUsbHsDebugStage(((INTST & USBHS_UDIS_EP_DIR) != 0u) ? 0x4324u : 0x4325u);
        HandleTransferToken(map, INTST);
      }
      clear_mask |= USBHS_UDIF_TRANSFER;
    }

    clear_mask |= static_cast<uint8_t>(INTFLAG & ~(clear_mask));
    USBHSD->INT_FG = clear_mask;
  }
}

CH32H41xUSBOtgHS::CH32H41xUSBOtgHS(
    const std::initializer_list<CH32H41xUSBOtgHS::EPConfig> EP_CFGS, uint16_t vid,
    uint16_t pid, uint16_t bcd,
    const std::initializer_list<const USB::DescriptorStrings::LanguagePack*> LANG_LIST,
    const std::initializer_list<const std::initializer_list<USB::ConfigDescriptorItem*>>
        CONFIGS,
    ConstRawData uid)
    : USB::EndpointPool(EP_CFGS.size() * 2),
      // Align CH32H41x USBHS enumeration baseline with the official SimulateCDC
      // examples first. They expose a USB 2.0 HS CDC device, not a USB 2.1 one.
      // 先把 CH32H41x USBHS 的枚举基线对齐官方 SimulateCDC：它导出的是 USB 2.0
      // HS CDC 设备，而不是 USB 2.1。
      USB::DeviceCore(*this, USB::USBSpec::USB_2_0, USB::Speed::HIGH,
                      USB::DeviceDescriptor::PacketSize0::SIZE_64, vid, pid, bcd,
                      LANG_LIST, CONFIGS, uid)
{
  ASSERT(EP_CFGS.size() > 0 && EP_CFGS.size() <= 8u);

  auto cfgs_itr = EP_CFGS.begin();

  ASSERT(cfgs_itr->buffer_tx.size_ >= 64 && cfgs_itr->buffer_rx.size_ >= 64 &&
         cfgs_itr->double_buffer == false);

  auto ep0_out =
      new CH32H41xEndpointOtgHs(USB::Endpoint::EPNumber::EP0,
                                USB::Endpoint::Direction::OUT, cfgs_itr->buffer_rx,
                                false);
  auto ep0_in =
      new CH32H41xEndpointOtgHs(USB::Endpoint::EPNumber::EP0,
                                USB::Endpoint::Direction::IN, cfgs_itr->buffer_tx,
                                false);

  USB::EndpointPool::SetEndpoint0(ep0_in, ep0_out);

  USB::Endpoint::EPNumber ep_index = USB::Endpoint::EPNumber::EP1;

  for (++cfgs_itr, ep_index = USB::Endpoint::EPNumber::EP1; cfgs_itr != EP_CFGS.end();
       ++cfgs_itr, ep_index = USB::Endpoint::NextEPNumber(ep_index))
  {
    if (!cfgs_itr->double_buffer)
    {
      auto ep_out = new CH32H41xEndpointOtgHs(
          ep_index, USB::Endpoint::Direction::OUT, cfgs_itr->buffer_rx, false);
      USB::EndpointPool::Put(ep_out);

      auto ep_in = new CH32H41xEndpointOtgHs(
          ep_index, USB::Endpoint::Direction::IN, cfgs_itr->buffer_tx, false);
      USB::EndpointPool::Put(ep_in);
    }
    else
    {
      auto ep = new CH32H41xEndpointOtgHs(
          ep_index,
          cfgs_itr->is_in ? USB::Endpoint::Direction::IN : USB::Endpoint::Direction::OUT,
          cfgs_itr->buffer_tx, true);
      USB::EndpointPool::Put(ep);
    }
  }
}

LibXR::ErrorCode CH32H41xUSBOtgHS::SetAddress(uint8_t address,
                                              USB::DeviceCore::Context context)
{
  if (context == USB::DeviceCore::Context::STATUS_IN_COMPLETE)
  {
    USBHSD->DEV_AD = address;
  }
  return LibXR::ErrorCode::OK;
}

void CH32H41xUSBOtgHS::OnConfigurationSwitched(uint16_t value, bool in_isr)
{
  UNUSED(value);
  UNUSED(in_isr);
  CH32H41xUsbHsDebugStage(0x4326u);
}

void CH32H41xUSBOtgHS::Start(bool)
{
  CH32H41xUsbHsDebugStage(0x4310u);
  NVIC_DisableIRQ(USBHS_IRQn);
  CH32H41xUsbHsDebugStage(0x4311u);
  NVIC_ClearPendingIRQ(USBHS_IRQn);
  CH32H41xUsbHsDebugStage(0x4312u);
  AllocateUsbHsIrqToV3f();

  CH32H41xUsbHsDebugStage(0x4313u);
  PrepareUsbHsPinsForCH32H41x();
  CH32H41xUsbHsDebugStage(0x4314u);
  LibXR::CH32UsbRcc::ConfigureUsbHsForCH32H41x();
  CH32H41xUsbHsDebugStage(0x4315u);
  RCC_UTMIcmd(ENABLE);
  CH32H41xUsbHsDebugStage(0x4316u);
  EnableUsbHsControllerClock();

  CH32H41xUsbHsDebugStage(0x4317u);
  USBHSD->CONTROL = USBHS_UD_CLR_ALL | USBHS_UD_RST_SIE | USBHS_UD_PHY_SUSPENDM;
  CH32H41xUsbHsDebugStage(0x4318u);
  USBHSD->CONTROL = USBHS_UD_PHY_SUSPENDM;
  CH32H41xUsbHsDebugStage(0x4319u);
  USBHSD->CONTROL = USBHS_UD_RST_LINK | USBHS_UD_PHY_SUSPENDM;
  CH32H41xUsbHsDebugStage(0x431Au);
  USBHSD->INT_FG = 0xFFu;
  // Rebuild the CH32H41x endpoint image from scratch. The official USBHS examples
  // program the global direction-enable masks explicitly on every init; doing
  // the same here avoids carrying stale endpoint enables across failed HS runs.
  // 每次 HS 启动都从干净的端点全局使能状态重建。官方例程每轮初始化都会显式
  // 重写方向使能寄存器；这里同样先清空，避免失败过的 HS 会话残留旧端点位图。
  USBHSD->UEP_TX_EN = 0u;
  USBHSD->UEP_RX_EN = 0u;
  USBHSD->UEP_TX_ISO = 0u;
  USBHSD->UEP_RX_ISO = 0u;
  CH32H41xUsbHsDebugStage(0x431Bu);
  RestoreUsbHsEndpointState();
  CH32H41xUsbHsDebugStage(0x431Cu);
  USBHSD->BASE_MODE = USBHS_UD_SPEED_HIGH;
  CH32H41xUsbHsDebugStage(0x431Du);
  USBHSD->INT_EN = USBHS_UDIE_BUS_RST | USBHS_UDIE_SUSPEND | USBHS_UDIE_BUS_SLEEP |
                   USBHS_UDIE_LPM_ACT | USBHS_UDIE_TRANSFER | USBHS_UDIE_LINK_RDY;
  CH32H41xUsbHsDebugStage(0x431Eu);
  USBHSD->CONTROL =
      USBHS_UD_DEV_EN | USBHS_UD_DMA_EN | USBHS_UD_LPM_EN | USBHS_UD_PHY_SUSPENDM;
  CH32H41xUsbHsDebugStage(0x431Fu);
  self_ = this;
  CH32H41xUsbHsDebugStage(0x4327u);
  NVIC_EnableIRQ(USBHS_IRQn);
  CH32H41xUsbHsDebugStage(0x4328u);
}

void CH32H41xUSBOtgHS::Stop(bool)
{
  NVIC_DisableIRQ(USBHS_IRQn);
  NVIC_ClearPendingIRQ(USBHS_IRQn);
  USBHSD->INT_EN = 0u;
  USBHSD->CONTROL = USBHS_UD_RST_SIE | USBHS_UD_RST_LINK;
  DisableUsbHsControllerClock();
  RCC_UTMIcmd(DISABLE);
  if ((RCC->PLLCFGR & RCC_SYSPLL_SEL) != RCC_SYSPLL_USBHS)
  {
    RCC_USBHS_PLLCmd(DISABLE);
  }
  self_ = nullptr;
}

#endif  // defined(USBHSD) && defined(LIBXR_CH32_IS_H41X)

// NOLINTEND(cppcoreguidelines-pro-type-cstyle-cast,performance-no-int-to-ptr)
