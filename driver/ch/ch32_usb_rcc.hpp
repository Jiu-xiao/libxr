#pragma once

#include <cstdint>

#include "libxr_def.hpp"
#include DEF2STR(LIBXR_CH32_CONFIG_FILE)

namespace LibXR::CH32UsbRcc
{

#if defined(LIBXR_CH32_IS_H41X)
extern "C" void CH32H41xUsbHsDebugStage(uint32_t stage);
extern "C" __attribute__((weak)) volatile uint32_t g_v3f_app_stage;

inline void ReportUsbHsDebugStage(uint32_t stage)
{
  if (&g_v3f_app_stage != nullptr)
  {
    g_v3f_app_stage = stage;
  }
  CH32H41xUsbHsDebugStage(stage);
}

inline bool WaitForRccCtlrBits(uint32_t mask, uint32_t timeout)
{
  while (((RCC->CTLR & mask) != mask) && timeout != 0u)
  {
    --timeout;
  }

  return (RCC->CTLR & mask) == mask;
}

inline bool WaitForRccPllCfgr2MaskedValue(uint32_t mask, uint32_t expected,
                                          uint32_t timeout)
{
  while (((RCC->PLLCFGR2 & mask) != expected) && timeout != 0u)
  {
    --timeout;
  }

  return (RCC->PLLCFGR2 & mask) == expected;
}

inline bool TryGetUsbHsCH32H41xRefConfig(bool use_hse, uint32_t& ref_cfg)
{
  if (!use_hse)
  {
#if defined(RCC_USBHSPLLRefer_25M)
    ref_cfg = RCC_USBHSPLLRefer_25M;
    return true;
#else
    return false;
#endif
  }

  switch (static_cast<uint32_t>(HSE_VALUE))
  {
    case 20000000u:
      ref_cfg = RCC_USBHSPLLRefer_20M;
      return true;
    case 24000000u:
      ref_cfg = RCC_USBHSPLLRefer_24M;
      return true;
    case 25000000u:
      ref_cfg = RCC_USBHSPLLRefer_25M;
      return true;
    case 32000000u:
      ref_cfg = RCC_USBHSPLLRefer_32M;
      return true;
    default:
      return false;
  }
}
#endif

inline uint32_t GetSysclkHz()
{
  RCC_ClocksTypeDef clk{};
  RCC_GetClocksFreq(&clk);
  return clk.SYSCLK_Frequency;
}

inline void ConfigureUsb48MFromSysclk()
{
  // 传统 CH32 器件直接从 SYSCLK / PLL 分频得到共享 USB 48 MHz 时钟。
  // Legacy CH32 parts derive the shared USB 48 MHz clock directly from SYSCLK / PLL
  // dividers.
  const uint32_t sysclk_hz = GetSysclkHz();

#if defined(RCC_USBCLKSource_PLLCLK_Div1) && defined(RCC_USBCLKSource_PLLCLK_Div2) && \
    defined(RCC_USBCLKSource_PLLCLK_Div3)
  if (sysclk_hz == 144000000u)
  {
    RCC_USBCLKConfig(RCC_USBCLKSource_PLLCLK_Div3);
    return;
  }
  if (sysclk_hz == 96000000u)
  {
    RCC_USBCLKConfig(RCC_USBCLKSource_PLLCLK_Div2);
    return;
  }
  if (sysclk_hz == 48000000u)
  {
    RCC_USBCLKConfig(RCC_USBCLKSource_PLLCLK_Div1);
    return;
  }
#if defined(RCC_USB5PRE_JUDGE) && defined(RCC_USBCLKSource_PLLCLK_Div5)
  if (sysclk_hz == 240000000u)
  {
    ASSERT(RCC_USB5PRE_JUDGE() == SET);
    RCC_USBCLKConfig(RCC_USBCLKSource_PLLCLK_Div5);
    return;
  }
#endif
#elif defined(RCC_USBCLK48MCLKSource_PLLCLK) && \
    defined(RCC_USBFSCLKSource_PLLCLK_Div1) &&  \
    defined(RCC_USBFSCLKSource_PLLCLK_Div2) && defined(RCC_USBFSCLKSource_PLLCLK_Div3)
  RCC_USBCLK48MConfig(RCC_USBCLK48MCLKSource_PLLCLK);

  if (sysclk_hz == 144000000u)
  {
    RCC_USBFSCLKConfig(RCC_USBFSCLKSource_PLLCLK_Div3);
    return;
  }
  if (sysclk_hz == 96000000u)
  {
    RCC_USBFSCLKConfig(RCC_USBFSCLKSource_PLLCLK_Div2);
    return;
  }
  if (sysclk_hz == 48000000u)
  {
    RCC_USBFSCLKConfig(RCC_USBFSCLKSource_PLLCLK_Div1);
    return;
  }
#endif

  ASSERT(false);
}

inline void ConfigureUsb48MForCH32H41x()
{
#if defined(RCC_USBFSCLKSource_USBHSPLL) && defined(RCC_USBHSPLLSource_HSE) && \
    defined(RCC_USBHSPLLRefer_20M) && \
    defined(RCC_USBHSPLLRefer_24M) && defined(RCC_USBHSPLLRefer_25M) && \
    defined(RCC_USBHSPLLRefer_32M) && defined(RCC_USBHSPLL_IN_Div1) && \
    defined(RCC_USBFS_Div10) && defined(RCC_SYSPLL_SEL) && defined(RCC_SYSPLL_USBHS) && \
    defined(RCC_HSERDY) && defined(RCC_USBHS_PLLRDY)
  if ((RCC->PLLCFGR & RCC_SYSPLL_SEL) != RCC_SYSPLL_USBHS)
  {
    ASSERT((RCC->CTLR & RCC_HSERDY) != 0u);

    uint32_t ref_cfg = 0u;
    switch (static_cast<uint32_t>(HSE_VALUE))
    {
      case 20000000u:
        ref_cfg = RCC_USBHSPLLRefer_20M;
        break;
      case 24000000u:
        ref_cfg = RCC_USBHSPLLRefer_24M;
        break;
      case 25000000u:
        ref_cfg = RCC_USBHSPLLRefer_25M;
        break;
      case 32000000u:
        ref_cfg = RCC_USBHSPLLRefer_32M;
        break;
      default:
        ASSERT(false);
        return;
    }

    RCC_USBHS_PLLCmd(DISABLE);
    RCC_USBHSPLLCLKConfig(RCC_USBHSPLLSource_HSE);
    RCC_USBHSPLLReferConfig(ref_cfg);
    RCC_USBHSPLLClockSourceDivConfig(RCC_USBHSPLL_IN_Div1);
    RCC_USBHS_PLLCmd(ENABLE);
    while ((RCC->CTLR & RCC_USBHS_PLLRDY) == 0u)
    {
    }
  }

  RCC_USBFSCLKConfig(RCC_USBFSCLKSource_USBHSPLL);
  RCC_USBFS48ClockSourceDivConfig(RCC_USBFS_Div10);
#else
  ASSERT(false);
#endif
}

inline void ConfigureUsbHsForCH32H41x()
{
#if defined(RCC_USBHSPLLSource_HSE) && defined(RCC_USBHSPLLRefer_20M) && \
    defined(RCC_USBHSPLLRefer_24M) && \
    defined(RCC_USBHSPLLRefer_25M) && defined(RCC_USBHSPLLRefer_32M) && \
    defined(RCC_USBHSPLL_IN_Div1) && defined(RCC_USBHSPLLSRC) &&        \
    defined(RCC_USBHSPLL_REFSEL) && defined(RCC_USBHSPLL_IN_DIV) &&     \
    defined(RCC_SYSPLL_SEL) && defined(RCC_SYSPLL_USBHS) &&             \
    defined(RCC_HSEON) && defined(RCC_HSERDY) && defined(RCC_USBHS_PLLRDY) && \
    defined(RCC_USBHSPLLSource_HSI)
  ReportUsbHsDebugStage(0x4330u);
  if ((RCC->PLLCFGR & RCC_SYSPLL_SEL) != RCC_SYSPLL_USBHS)
  {
    constexpr uint32_t kHseTimeout = 200000u;
    constexpr uint32_t kCfgTimeout = 200000u;
    constexpr uint32_t kUsbHsPllTimeout = 200000u;

    // USBFS on CH32H41x can already be running from USBHS PLL / 10. Reconfiguring the
    // shared PLL under an active FS runtime is unsafe, so reuse the locked PLL when
    // it is already on.
    // CH32H41x 上 USBFS 可能已经在使用 USBHS PLL / 10；此时再次关闭并重配共享 PLL
    // 会直接干扰正在工作的 FS 运行时，因此 PLL 已锁定时直接复用。
    if ((RCC->CTLR & RCC_USBHS_PLLRDY) != 0u)
    {
      ReportUsbHsDebugStage(0x433Eu);
      ReportUsbHsDebugStage(0x433Fu);
      return;
    }

    bool use_hse = (RCC->CTLR & RCC_HSERDY) != 0u;
    if (!use_hse)
    {
      ReportUsbHsDebugStage(0x4331u);
      RCC->CTLR |= RCC_HSEON;
      use_hse = WaitForRccCtlrBits(RCC_HSERDY, kHseTimeout);
    }
    ReportUsbHsDebugStage(use_hse ? 0x4332u : 0x4333u);

    uint32_t ref_cfg = 0u;
    ASSERT(TryGetUsbHsCH32H41xRefConfig(use_hse, ref_cfg));
    ReportUsbHsDebugStage(0x4334u);

    RCC_USBHS_PLLCmd(DISABLE);
    ReportUsbHsDebugStage(0x4335u);
    RCC_USBHSPLLCLKConfig(use_hse ? RCC_USBHSPLLSource_HSE : RCC_USBHSPLLSource_HSI);
    ReportUsbHsDebugStage(0x4336u);
    ASSERT(WaitForRccPllCfgr2MaskedValue(
        RCC_USBHSPLLSRC,
        use_hse ? RCC_USBHSPLLSource_HSE : RCC_USBHSPLLSource_HSI, kCfgTimeout));
    ReportUsbHsDebugStage(0x433Au);
    RCC_USBHSPLLReferConfig(ref_cfg);
    ReportUsbHsDebugStage(0x4337u);
    ASSERT(WaitForRccPllCfgr2MaskedValue(RCC_USBHSPLL_REFSEL, ref_cfg, kCfgTimeout));
    ReportUsbHsDebugStage(0x433Bu);
    RCC_USBHSPLLClockSourceDivConfig(RCC_USBHSPLL_IN_Div1);
    ReportUsbHsDebugStage(0x4338u);
    ASSERT(WaitForRccPllCfgr2MaskedValue(RCC_USBHSPLL_IN_DIV, RCC_USBHSPLL_IN_Div1,
                                         kCfgTimeout));
    ReportUsbHsDebugStage(0x433Cu);
    RCC_USBHS_PLLCmd(ENABLE);
    ReportUsbHsDebugStage(0x4339u);
    ASSERT(WaitForRccCtlrBits(RCC_USBHS_PLLRDY, kUsbHsPllTimeout));
    ReportUsbHsDebugStage(0x433Du);
  }
  ReportUsbHsDebugStage(0x433Fu);
#else
  ASSERT(false);
#endif
}

#if defined(RCC_HSBHSPLLCLKSource_HSE) && defined(RCC_USBPLL_Div1) &&                   \
    defined(RCC_USBPLL_Div2) && defined(RCC_USBPLL_Div3) && defined(RCC_USBPLL_Div4) && \
    defined(RCC_USBPLL_Div5) && defined(RCC_USBPLL_Div6) && defined(RCC_USBPLL_Div7) && \
    defined(RCC_USBPLL_Div8) && defined(RCC_USBHSPLLCKREFCLK_3M) &&                     \
    defined(RCC_USBHSPLLCKREFCLK_4M) && defined(RCC_USBHSPLLCKREFCLK_5M) &&             \
    defined(RCC_USBHSPLLCKREFCLK_8M)
struct UsbHsPllConfig
{
  uint32_t divider_cfg = 0u;
  uint32_t ref_cfg = 0u;
};

struct UsbHsPllTableEntry
{
  uint32_t hse_hz = 0u;
  uint32_t divider_cfg = 0u;
  uint32_t ref_cfg = 0u;
};

inline bool TryGetUsbHsPllConfigForHse(uint32_t hse_hz, UsbHsPllConfig& cfg)
{
  // 这里保留显式查表，不做动态推导；
  // USBHS 合法参考时钟是离散集合，精确表更容易审计。
  // Keep this as an explicit lookup table instead of inferring combinations dynamically;
  // legal USBHS reference clocks are discrete and easier to audit in table form.
  constexpr UsbHsPllTableEntry table[] = {
      {3000000u, RCC_USBPLL_Div1, RCC_USBHSPLLCKREFCLK_3M},
      {4000000u, RCC_USBPLL_Div1, RCC_USBHSPLLCKREFCLK_4M},
      {5000000u, RCC_USBPLL_Div1, RCC_USBHSPLLCKREFCLK_5M},
      {6000000u, RCC_USBPLL_Div2, RCC_USBHSPLLCKREFCLK_3M},
      {8000000u, RCC_USBPLL_Div2, RCC_USBHSPLLCKREFCLK_4M},
      {9000000u, RCC_USBPLL_Div3, RCC_USBHSPLLCKREFCLK_3M},
      {10000000u, RCC_USBPLL_Div2, RCC_USBHSPLLCKREFCLK_5M},
      {12000000u, RCC_USBPLL_Div3, RCC_USBHSPLLCKREFCLK_4M},
      {15000000u, RCC_USBPLL_Div3, RCC_USBHSPLLCKREFCLK_5M},
      {16000000u, RCC_USBPLL_Div4, RCC_USBHSPLLCKREFCLK_4M},
      {18000000u, RCC_USBPLL_Div6, RCC_USBHSPLLCKREFCLK_3M},
      {20000000u, RCC_USBPLL_Div5, RCC_USBHSPLLCKREFCLK_4M},
      {21000000u, RCC_USBPLL_Div7, RCC_USBHSPLLCKREFCLK_3M},
      {24000000u, RCC_USBPLL_Div6, RCC_USBHSPLLCKREFCLK_4M},
      {25000000u, RCC_USBPLL_Div5, RCC_USBHSPLLCKREFCLK_5M},
      {28000000u, RCC_USBPLL_Div7, RCC_USBHSPLLCKREFCLK_4M},
      {30000000u, RCC_USBPLL_Div6, RCC_USBHSPLLCKREFCLK_5M},
      {32000000u, RCC_USBPLL_Div8, RCC_USBHSPLLCKREFCLK_4M},
      {35000000u, RCC_USBPLL_Div7, RCC_USBHSPLLCKREFCLK_5M},
      {40000000u, RCC_USBPLL_Div8, RCC_USBHSPLLCKREFCLK_5M},
      {48000000u, RCC_USBPLL_Div6, RCC_USBHSPLLCKREFCLK_8M},
      {56000000u, RCC_USBPLL_Div7, RCC_USBHSPLLCKREFCLK_8M},
      {64000000u, RCC_USBPLL_Div8, RCC_USBHSPLLCKREFCLK_8M},
  };

  for (const auto& entry : table)
  {
    if (entry.hse_hz != hse_hz)
    {
      continue;
    }
    cfg.divider_cfg = entry.divider_cfg;
    cfg.ref_cfg = entry.ref_cfg;
    return true;
  }

  return false;
}

inline void ConfigureUsbHsPhyFromHse()
{
  // USBHS PHY 路径通过 HSE -> (分频, 参考时钟) 查表，
  // 保证合法组合保持显式且可审计。
  // The USBHS PHY path uses an HSE -> (divider, ref clock) lookup so the
  // legal combinations stay explicit and auditable.
  UsbHsPllConfig cfg = {};
  const uint32_t hse_hz = static_cast<uint32_t>(HSE_VALUE);
  ASSERT(TryGetUsbHsPllConfigForHse(hse_hz, cfg));

  RCC_USBHSPLLCLKConfig(RCC_HSBHSPLLCLKSource_HSE);
  RCC_USBHSConfig(cfg.divider_cfg);
  RCC_USBHSPLLCKREFCLKConfig(cfg.ref_cfg);
  RCC_USBHSPHYPLLALIVEcmd(ENABLE);
}
#endif

inline void ConfigureUsb48M()
{
#if defined(LIBXR_CH32_IS_H41X)
  // CH32H41x routes USBFS 48 MHz from the dedicated USBHS PLL path.
  // CH32H41x 的 USBFS 48 MHz 走专用 USBHS PLL 路径。
  ConfigureUsb48MForCH32H41x();
#elif defined(RCC_USBCLK48MCLKSource_USBPHY) && defined(RCC_HSBHSPLLCLKSource_HSE)
  // 部分 CH32V30x 可以从 USBHS PHY PLL 提供共享 48 MHz USB 时钟；
  // 这种情况下先选 PHY 路径，再让各控制器自己打开总线时钟。
  // Some CH32V30x parts can source the shared 48 MHz USB clock from the USBHS PHY PLL;
  // in that case, select the PHY path first and let each controller enable its own bus clock.
  ConfigureUsbHsPhyFromHse();
  RCC_USBCLK48MConfig(RCC_USBCLK48MCLKSource_USBPHY);
#else
  // 经典路径：直接从 SYSCLK / PLL 分频得到共享 48 MHz 时钟。
  // Classic path: derive the shared 48 MHz clock directly from SYSCLK / PLL dividers.
  ConfigureUsb48MFromSysclk();
#endif
}

}  // namespace LibXR::CH32UsbRcc
