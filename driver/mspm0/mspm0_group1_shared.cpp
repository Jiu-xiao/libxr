#include "mspm0_group1_shared.hpp"

namespace LibXR::MSPM0Group1Shared
{
void EnableGroup1IRQ()
{
#if defined(GPIOA_BASE)
  NVIC_EnableIRQ(GPIOA_INT_IRQn);
#elif defined(GPIOB_BASE)
  NVIC_EnableIRQ(GPIOB_INT_IRQn);
#elif defined(GPIOC_BASE)
  NVIC_EnableIRQ(GPIOC_INT_IRQn);
#elif defined(COMP0_BASE)
  NVIC_EnableIRQ(COMP0_INT_IRQn);
#elif defined(COMP1_BASE)
  NVIC_EnableIRQ(COMP1_INT_IRQn);
#elif defined(COMP2_BASE)
  NVIC_EnableIRQ(COMP2_INT_IRQn);
#elif defined(TRNG_BASE)
  NVIC_EnableIRQ(TRNG_INT_IRQn);
#endif
}
}  // namespace LibXR::MSPM0Group1Shared

// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void GROUP1_IRQHandler(void)
{
  uint32_t iidx = DL_Interrupt_getPendingGroup(DL_INTERRUPT_GROUP_1);
  using namespace LibXR::MSPM0Group1Shared;

  switch (iidx)
  {
#if defined(GPIOA_BASE)
    case DL_INTERRUPT_GROUP1_IIDX_GPIOA:
      if (auto fn = gpioa_irq_cb)
      {
        fn();
      }
      break;
#endif
#if defined(GPIOB_BASE)
    case DL_INTERRUPT_GROUP1_IIDX_GPIOB:
      if (auto fn = gpiob_irq_cb)
      {
        fn();
      }
      break;
#endif
#if defined(COMP0_BASE)
    case DL_INTERRUPT_GROUP1_IIDX_COMP0:
      if (auto fn = comp0_irq_cb)
      {
        fn();
      }
      break;
#endif
#if defined(COMP1_BASE)
    case DL_INTERRUPT_GROUP1_IIDX_COMP1:
      if (auto fn = comp1_irq_cb)
      {
        fn();
      }
      break;
#endif
#if defined(COMP2_BASE)
    case DL_INTERRUPT_GROUP1_IIDX_COMP2:
      if (auto fn = comp2_irq_cb)
      {
        fn();
      }
      break;
#endif
#if defined(TRNG_BASE)
    case DL_INTERRUPT_GROUP1_IIDX_TRNG:
      if (auto fn = trng_irq_cb)
      {
        fn();
      }
      break;
#endif
#if defined(GPIOC_BASE)
    case DL_INTERRUPT_GROUP1_IIDX_GPIOC:
      if (auto fn = gpioc_irq_cb)
      {
        fn();
      }
      break;
#endif
    default:
      break;
  }
}
