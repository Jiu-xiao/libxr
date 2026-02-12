// NOLINTBEGIN(cppcoreguidelines-pro-type-cstyle-cast,performance-no-int-to-ptr)
// ch32_usb_devfs.cpp  (classic FSDEV / PMA)
#define CH32_USBCAN_SHARED_IMPLEMENTATION
#include "ch32_usb_dev.hpp"
#include "ch32_usb_endpoint.hpp"
#include "ch32_usbcan_shared.hpp"
#include "ep.hpp"

using namespace LibXR;
using namespace LibXR::USB;

#if defined(RCC_APB1Periph_USB)

namespace
{

static void ch32_usb_clock48m_config()
{
  RCC_ClocksTypeDef clk{};
  RCC_GetClocksFreq(&clk);

  const uint32_t SYSCLK_HZ = clk.SYSCLK_Frequency;

#if defined(RCC_USBCLKSource_PLLCLK_Div1) && defined(RCC_USBCLKSource_PLLCLK_Div2) && \
    defined(RCC_USBCLKSource_PLLCLK_Div3)
  if (SYSCLK_HZ == 144000000u)
  {
    RCC_USBCLKConfig(RCC_USBCLKSource_PLLCLK_Div3);
  }
  else if (SYSCLK_HZ == 96000000u)
  {
    RCC_USBCLKConfig(RCC_USBCLKSource_PLLCLK_Div2);
  }
  else if (SYSCLK_HZ == 48000000u)
  {
    RCC_USBCLKConfig(RCC_USBCLKSource_PLLCLK_Div1);
  }
#if defined(RCC_USB5PRE_JUDGE) && defined(RCC_USBCLKSource_PLLCLK_Div5)
  else if (SYSCLK_HZ == 240000000u)
  {
    ASSERT(RCC_USB5PRE_JUDGE() == SET);
    RCC_USBCLKConfig(RCC_USBCLKSource_PLLCLK_Div5);
  }
#endif
  else
  {
    ASSERT(false);
  }

#elif defined(RCC_USBCLK48MCLKSource_PLLCLK) && \
    defined(RCC_USBFSCLKSource_PLLCLK_Div1) &&  \
    defined(RCC_USBFSCLKSource_PLLCLK_Div2) && defined(RCC_USBFSCLKSource_PLLCLK_Div3)
  RCC_USBCLK48MConfig(RCC_USBCLK48MCLKSource_PLLCLK);

  if (SYSCLK_HZ == 144000000u)
  {
    RCC_USBFSCLKConfig(RCC_USBFSCLKSource_PLLCLK_Div3);
  }
  else if (SYSCLK_HZ == 96000000u)
  {
    RCC_USBFSCLKConfig(RCC_USBFSCLKSource_PLLCLK_Div2);
  }
  else if (SYSCLK_HZ == 48000000u)
  {
    RCC_USBFSCLKConfig(RCC_USBFSCLKSource_PLLCLK_Div1);
  }
  else
  {
    ASSERT(false);
  }

#else
  (void)SYSCLK_HZ;
#endif
}

}  // namespace

#ifdef USB_BASE
static constexpr uintptr_t USBDEV_REG_BASE = USB_BASE;
#else
static constexpr uintptr_t USBDEV_REG_BASE = 0x40005C00UL;
#endif

static inline volatile uint16_t* usbdev_cntr()
{
  // MMIO fixed physical base address.
  // NOLINTNEXTLINE(performance-no-int-to-ptr)
  return reinterpret_cast<volatile uint16_t*>(USBDEV_REG_BASE + 0x40U);
}
static inline volatile uint16_t* usbdev_istr()
{
  return reinterpret_cast<volatile uint16_t*>(USBDEV_REG_BASE + 0x44U);
}
static inline volatile uint16_t* usbdev_daddr()
{
  return reinterpret_cast<volatile uint16_t*>(USBDEV_REG_BASE + 0x4CU);
}
static inline volatile uint16_t* usbdev_btable()
{
  return reinterpret_cast<volatile uint16_t*>(USBDEV_REG_BASE + 0x50U);
}
static inline volatile uint16_t* usbdev_ep_reg(uint8_t ep)
{
  return reinterpret_cast<volatile uint16_t*>(USBDEV_REG_BASE +
                                              static_cast<uintptr_t>(ep) * 4U);
}

// Compatibility bridge for classic USBLIB-style macro names
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
#ifndef USB_EP_RX_DIS
#define USB_EP_RX_DIS 0x0000u
#endif
#ifndef USB_EP_RX_STALL
#define USB_EP_RX_STALL 0x1000u
#endif
#ifndef USB_EP_RX_NAK
#define USB_EP_RX_NAK 0x2000u
#endif
#ifndef USB_EP_RX_VALID
#define USB_EP_RX_VALID 0x3000u
#endif

#ifndef USB_EPREG_MASK
#define USB_EPREG_MASK                                                           \
  (USB_EP_CTR_RX | USB_EP_SETUP | USB_EP_T_FIELD | USB_EP_KIND | USB_EP_CTR_TX | \
   USB_EPADDR_FIELD)
#endif

static inline void usbdev_clear_istr(uint16_t mask)
{
  *usbdev_istr() = static_cast<uint16_t>(~mask);
}

static inline void usbdev_set_ep_address(uint8_t ep, uint8_t addr)
{
  volatile uint16_t* reg = usbdev_ep_reg(ep);
  const uint16_t CUR = *reg;

  const uint16_t V = static_cast<uint16_t>(USB_EP_CTR_RX | USB_EP_CTR_TX |
                                           (CUR & (USB_EPREG_MASK & ~USB_EPADDR_FIELD)) |
                                           (addr & USB_EPADDR_FIELD));

  *reg = V;
}

static LibXR::RawData select_buffer_dev_fs(USB::Endpoint::EPNumber ep_num,
                                           USB::Endpoint::Direction dir,
                                           const LibXR::RawData& buffer)
{
  if (ep_num == USB::Endpoint::EPNumber::EP0)
  {
    return buffer;
  }

  const size_t HALF = buffer.size_ / 2;
  if (dir == USB::Endpoint::Direction::OUT)
  {
    return LibXR::RawData(buffer.addr_, HALF);
  }
  return LibXR::RawData(reinterpret_cast<uint8_t*>(buffer.addr_) + HALF, HALF);
}

static void usbdev_fs_irqhandler()
{
  auto& map = LibXR::CH32EndpointDevFs::map_dev_fs_;

  constexpr uint8_t OUT_IDX = static_cast<uint8_t>(LibXR::USB::Endpoint::Direction::OUT);
  constexpr uint8_t IN_IDX = static_cast<uint8_t>(LibXR::USB::Endpoint::Direction::IN);

  while (true)
  {
    const uint16_t ISTR = *usbdev_istr();

    if (ISTR & USB_ISTR_RESET)
    {
      usbdev_clear_istr(USB_ISTR_RESET);

      *usbdev_daddr() = USB_DADDR_EF;
      *usbdev_btable() = 0;

      LibXR::CH32EndpointDevFs::ResetPMAAllocator();

      LibXR::CH32USBDeviceFS::self_->Deinit(true);
      LibXR::CH32USBDeviceFS::self_->Init(true);

      if (map[0][OUT_IDX])
      {
        map[0][OUT_IDX]->SetState(LibXR::USB::Endpoint::State::IDLE);
      }
      if (map[0][IN_IDX])
      {
        map[0][IN_IDX]->SetState(LibXR::USB::Endpoint::State::IDLE);
      }

      LibXR::CH32EndpointDevFs::SetEpTxStatus(0, USB_EP_TX_NAK);
      LibXR::CH32EndpointDevFs::SetEpRxStatus(0, USB_EP_RX_VALID);
      continue;
    }

    if (ISTR & USB_ISTR_SUSP)
    {
      usbdev_clear_istr(USB_ISTR_SUSP);

      LibXR::CH32USBDeviceFS::self_->Deinit(true);
      LibXR::CH32USBDeviceFS::self_->Init(true);

      if (map[0][OUT_IDX])
      {
        map[0][OUT_IDX]->SetState(LibXR::USB::Endpoint::State::IDLE);
      }
      if (map[0][IN_IDX])
      {
        map[0][IN_IDX]->SetState(LibXR::USB::Endpoint::State::IDLE);
      }

      LibXR::CH32EndpointDevFs::SetEpTxStatus(0, USB_EP_TX_NAK);
      LibXR::CH32EndpointDevFs::SetEpRxStatus(0, USB_EP_RX_NAK);
      continue;
    }

    if (ISTR & USB_ISTR_WKUP)
    {
      usbdev_clear_istr(USB_ISTR_WKUP);
      continue;
    }

    if ((ISTR & USB_ISTR_CTR) == 0)
    {
      break;
    }

    const uint8_t EP_ID = static_cast<uint8_t>(ISTR & USB_ISTR_EP_ID);

    uint16_t epr = *usbdev_ep_reg(EP_ID);

    if (epr & USB_EP_CTR_RX)
    {
      if (EP_ID == 0)
      {
        if (epr & USB_EP_SETUP)
        {
          // New SETUP starts a fresh control transfer; only clear pending CTR flags here.
          // EP0 RX/TX status is armed by control-transfer handlers
          // (OnSetupPacket/Transfer).
          if (epr & USB_EP_CTR_TX)
          {
            LibXR::CH32EndpointDevFs::ClearEpCtrTx(0);
          }
          LibXR::CH32EndpointDevFs::ClearEpCtrRx(0);

          if (map[0][OUT_IDX])
          {
            map[0][OUT_IDX]->CopyRxDataToBuffer(sizeof(LibXR::USB::SetupPacket));
            LibXR::CH32USBDeviceFS::self_->OnSetupPacket(
                true, reinterpret_cast<const LibXR::USB::SetupPacket*>(
                          map[0][OUT_IDX]->GetBuffer().addr_));
          }

          continue;
        }
        else
        {
          LibXR::CH32EndpointDevFs::ClearEpCtrRx(0);
          const uint16_t LEN = LibXR::CH32EndpointDevFs::GetRxCount(0);
          if (map[0][OUT_IDX])
          {
            map[0][OUT_IDX]->TransferComplete(LEN);
          }
        }
      }
      else
      {
        LibXR::CH32EndpointDevFs::ClearEpCtrRx(EP_ID);
        const uint16_t LEN = LibXR::CH32EndpointDevFs::GetRxCount(EP_ID);
        if (map[EP_ID][OUT_IDX])
        {
          map[EP_ID][OUT_IDX]->TransferComplete(LEN);
        }
      }
    }

    epr = *usbdev_ep_reg(EP_ID);

    if (epr & USB_EP_CTR_TX)
    {
      if (EP_ID == 0)
      {
        LibXR::CH32EndpointDevFs::ClearEpCtrTx(0);
        if (map[0][IN_IDX])
        {
          map[0][IN_IDX]->TransferComplete(0);
        }
      }
      else
      {
        LibXR::CH32EndpointDevFs::ClearEpCtrTx(EP_ID);
        if (map[EP_ID][IN_IDX])
        {
          map[EP_ID][IN_IDX]->TransferComplete(0);
        }
      }
    }
  }
}

static void usb_irq_thunk() { usbdev_fs_irqhandler(); }

// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" __attribute__((interrupt)) void USBWakeUp_IRQHandler(void)
{
#if defined(EXTI_Line18)
  EXTI_ClearITPendingBit(EXTI_Line18);
#endif
}

CH32USBDeviceFS::CH32USBDeviceFS(
    const std::initializer_list<EPConfig> EP_CFGS,
    USB::DeviceDescriptor::PacketSize0 packet_size, uint16_t vid, uint16_t pid,
    uint16_t bcd,
    const std::initializer_list<const USB::DescriptorStrings::LanguagePack*> LANG_LIST,
    const std::initializer_list<const std::initializer_list<USB::ConfigDescriptorItem*>>
        CONFIGS,
    ConstRawData uid)
    : USB::EndpointPool(EP_CFGS.size() * 2),
      USB::DeviceCore(*this, USB::USBSpec::USB_2_1, USB::Speed::FULL, packet_size, vid,
                      pid, bcd, LANG_LIST, CONFIGS, uid)
{
  self_ = this;

  ASSERT(EP_CFGS.size() > 0 && EP_CFGS.size() <= CH32EndpointDevFs::EP_DEV_FS_MAX_SIZE);

  auto cfgs_itr = EP_CFGS.begin();

  auto ep0_out =
      new CH32EndpointDevFs(USB::Endpoint::EPNumber::EP0, USB::Endpoint::Direction::OUT,
                            cfgs_itr->buffer, false);
  auto ep0_in =
      new CH32EndpointDevFs(USB::Endpoint::EPNumber::EP0, USB::Endpoint::Direction::IN,
                            cfgs_itr->buffer, false);

  USB::EndpointPool::SetEndpoint0(ep0_in, ep0_out);

  USB::Endpoint::EPNumber ep_index = USB::Endpoint::EPNumber::EP1;

  for (++cfgs_itr, ep_index = USB::Endpoint::EPNumber::EP1; cfgs_itr != EP_CFGS.end();
       ++cfgs_itr, ep_index = USB::Endpoint::NextEPNumber(ep_index))
  {
    if (cfgs_itr->is_in == -1)
    {
      auto ep_out = new CH32EndpointDevFs(
          ep_index, USB::Endpoint::Direction::OUT,
          select_buffer_dev_fs(ep_index, USB::Endpoint::Direction::OUT, cfgs_itr->buffer),
          false);
      USB::EndpointPool::Put(ep_out);

      auto ep_in = new CH32EndpointDevFs(
          ep_index, USB::Endpoint::Direction::IN,
          select_buffer_dev_fs(ep_index, USB::Endpoint::Direction::IN, cfgs_itr->buffer),
          false);
      USB::EndpointPool::Put(ep_in);
    }
    else
    {
      auto ep = new CH32EndpointDevFs(
          ep_index,
          cfgs_itr->is_in ? USB::Endpoint::Direction::IN : USB::Endpoint::Direction::OUT,
          cfgs_itr->buffer, true);
      USB::EndpointPool::Put(ep);
    }
  }
}

ErrorCode CH32USBDeviceFS::SetAddress(uint8_t address, USB::DeviceCore::Context context)
{
  if (context == USB::DeviceCore::Context::STATUS_IN)
  {
    const uint8_t N_EP = static_cast<uint8_t>(CH32EndpointDevFs::EP_DEV_FS_MAX_SIZE);
    for (uint8_t i = 0; i < N_EP; i++)
    {
      usbdev_set_ep_address(i, i);
    }

    *usbdev_daddr() = static_cast<uint16_t>(USB_DADDR_EF | address);

    CH32EndpointDevFs::SetEpTxStatus(0, USB_EP_TX_NAK);
    CH32EndpointDevFs::SetEpRxStatus(0, USB_EP_RX_VALID);
  }
  return ErrorCode::OK;
}

void CH32USBDeviceFS::Start(bool)
{
  LibXR::CH32UsbCanShared::usb_inited.store(true, std::memory_order_release);
  LibXR::CH32UsbCanShared::register_usb_irq(&usb_irq_thunk);

  ch32_usb_clock48m_config();
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_USB, ENABLE);

#if defined(RCC_APB2Periph_GPIOA) && defined(GPIOA) && defined(GPIO_Pin_11) &&        \
    defined(GPIO_Pin_12) && defined(GPIO_Mode_Out_PP) && defined(GPIO_Speed_50MHz) && \
    defined(GPIO_Mode_IN_FLOATING)
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

  GPIO_InitTypeDef gpio{};
  gpio.GPIO_Pin = GPIO_Pin_11 | GPIO_Pin_12;
  gpio.GPIO_Speed = GPIO_Speed_50MHz;
  gpio.GPIO_Mode = GPIO_Mode_Out_PP;
  GPIO_Init(GPIOA, &gpio);
  GPIO_ResetBits(GPIOA, GPIO_Pin_11 | GPIO_Pin_12);

  gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
  GPIO_Init(GPIOA, &gpio);
#endif

  *usbdev_cntr() = USB_CNTR_FRES;
  *usbdev_cntr() = 0;

  usbdev_clear_istr(0xFFFFu);
  *usbdev_btable() = 0;

  *usbdev_cntr() = static_cast<uint16_t>(USB_CNTR_RESETM | USB_CNTR_SUSPM |
                                         USB_CNTR_WKUPM | USB_CNTR_CTRM);

#if defined(EXTEN_USBD_LS)
  EXTEN->EXTEN_CTR &= ~EXTEN_USBD_LS;
#endif

  NVIC_EnableIRQ(USB_LP_CAN1_RX0_IRQn);
  NVIC_EnableIRQ(USB_HP_CAN1_TX_IRQn);
  NVIC_EnableIRQ(USBWakeUp_IRQn);

#if defined(EXTEN_USBD_PU_EN)
  EXTEN->EXTEN_CTR |= EXTEN_USBD_PU_EN;
#endif

  *usbdev_daddr() = USB_DADDR_EF;

  CH32EndpointDevFs::SetEpTxStatus(0, USB_EP_TX_NAK);
  CH32EndpointDevFs::SetEpRxStatus(0, USB_EP_RX_VALID);

  // DeviceCore::Init() may arm OUT endpoints before FSDEV reset/BTABLE init above.
  // Re-arm non-EP0 OUT endpoints that are logically BUSY after hardware is live.
  auto& ep_map = LibXR::CH32EndpointDevFs::map_dev_fs_;
  constexpr uint8_t OUT_IDX = static_cast<uint8_t>(LibXR::USB::Endpoint::Direction::OUT);
  const uint8_t N_EP = static_cast<uint8_t>(LibXR::CH32EndpointDevFs::EP_DEV_FS_MAX_SIZE);
  for (uint8_t ep = 1; ep < N_EP; ++ep)
  {
    auto* out = ep_map[ep][OUT_IDX];
    if (out == nullptr)
    {
      continue;
    }
    if (out->GetState() != LibXR::USB::Endpoint::State::BUSY)
    {
      continue;
    }
    (void)out->Transfer(out->MaxTransferSize());
  }
}

void CH32USBDeviceFS::Stop(bool)
{
  LibXR::CH32UsbCanShared::register_usb_irq(nullptr);
  LibXR::CH32UsbCanShared::usb_inited.store(false, std::memory_order_release);

#if defined(EXTEN_USBD_PU_EN)
  EXTEN->EXTEN_CTR &= ~EXTEN_USBD_PU_EN;
#endif

  if (!LibXR::CH32UsbCanShared::can1_active())
  {
    NVIC_DisableIRQ(USB_LP_CAN1_RX0_IRQn);
    NVIC_DisableIRQ(USB_HP_CAN1_TX_IRQn);
  }
  NVIC_DisableIRQ(USBWakeUp_IRQn);

  *usbdev_cntr() = USB_CNTR_FRES;
}

#endif  // defined(RCC_APB1Periph_USB)

// NOLINTEND(cppcoreguidelines-pro-type-cstyle-cast,performance-no-int-to-ptr)
