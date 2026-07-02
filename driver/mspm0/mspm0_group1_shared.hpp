#pragma once

#include <ti/driverlib/m0p/dl_interrupt.h>

namespace LibXR::MSPM0Group1Shared
{
using IrqFn = void (*)();

#if defined(GPIOA_BASE)
inline constexpr bool K_HAS_GPIOA = true;
#else
inline constexpr bool K_HAS_GPIOA = false;
#endif

#if defined(GPIOB_BASE)
inline constexpr bool K_HAS_GPIOB = true;
#else
inline constexpr bool K_HAS_GPIOB = false;
#endif

#if defined(GPIOC_BASE)
inline constexpr bool K_HAS_GPIOC = true;
#else
inline constexpr bool K_HAS_GPIOC = false;
#endif

#if defined(COMP0_BASE)
inline constexpr bool K_HAS_COMP0 = true;
#else
inline constexpr bool K_HAS_COMP0 = false;
#endif

#if defined(COMP1_BASE)
inline constexpr bool K_HAS_COMP1 = true;
#else
inline constexpr bool K_HAS_COMP1 = false;
#endif

#if defined(COMP2_BASE)
inline constexpr bool K_HAS_COMP2 = true;
#else
inline constexpr bool K_HAS_COMP2 = false;
#endif

#if defined(TRNG_BASE)
inline constexpr bool K_HAS_TRNG = true;
#else
inline constexpr bool K_HAS_TRNG = false;
#endif

inline constexpr bool K_GROUP1_SHARED =
    K_HAS_GPIOA || K_HAS_GPIOB || K_HAS_GPIOC || K_HAS_COMP0 || K_HAS_COMP1 ||
    K_HAS_COMP2 || K_HAS_TRNG;

inline IrqFn gpioa_irq_cb{nullptr};
inline IrqFn gpiob_irq_cb{nullptr};
inline IrqFn gpioc_irq_cb{nullptr};
inline IrqFn comp0_irq_cb{nullptr};
inline IrqFn comp1_irq_cb{nullptr};
inline IrqFn comp2_irq_cb{nullptr};
inline IrqFn trng_irq_cb{nullptr};

// NOLINTNEXTLINE(readability-identifier-naming)
void EnableGroup1IRQ();

// NOLINTNEXTLINE(readability-identifier-naming)
inline void RegisterGPIOA(IrqFn fn)
{
  if constexpr (K_HAS_GPIOA)
  {
    gpioa_irq_cb = fn;
    if (fn != nullptr)
    {
      EnableGroup1IRQ();
    }
  }
  else
  {
    (void)fn;
  }
}

// NOLINTNEXTLINE(readability-identifier-naming)
inline void RegisterGPIOB(IrqFn fn)
{
  if constexpr (K_HAS_GPIOB)
  {
    gpiob_irq_cb = fn;
    if (fn != nullptr)
    {
      EnableGroup1IRQ();
    }
  }
  else
  {
    (void)fn;
  }
}

// NOLINTNEXTLINE(readability-identifier-naming)
inline void RegisterGPIOC(IrqFn fn)
{
  if constexpr (K_HAS_GPIOC)
  {
    gpioc_irq_cb = fn;
    if (fn != nullptr)
    {
      EnableGroup1IRQ();
    }
  }
  else
  {
    (void)fn;
  }
}

// NOLINTNEXTLINE(readability-identifier-naming)
inline void RegisterCOMP0(IrqFn fn)
{
  if constexpr (K_HAS_COMP0)
  {
    comp0_irq_cb = fn;
    if (fn != nullptr)
    {
      EnableGroup1IRQ();
    }
  }
  else
  {
    (void)fn;
  }
}

// NOLINTNEXTLINE(readability-identifier-naming)
inline void RegisterCOMP1(IrqFn fn)
{
  if constexpr (K_HAS_COMP1)
  {
    comp1_irq_cb = fn;
    if (fn != nullptr)
    {
      EnableGroup1IRQ();
    }
  }
  else
  {
    (void)fn;
  }
}

// NOLINTNEXTLINE(readability-identifier-naming)
inline void RegisterCOMP2(IrqFn fn)
{
  if constexpr (K_HAS_COMP2)
  {
    comp2_irq_cb = fn;
    if (fn != nullptr)
    {
      EnableGroup1IRQ();
    }
  }
  else
  {
    (void)fn;
  }
}

// NOLINTNEXTLINE(readability-identifier-naming)
inline void RegisterTRNG(IrqFn fn)
{
  if constexpr (K_HAS_TRNG)
  {
    trng_irq_cb = fn;
    if (fn != nullptr)
    {
      EnableGroup1IRQ();
    }
  }
  else
  {
    (void)fn;
  }
}
}  // namespace LibXR::MSPM0Group1Shared
