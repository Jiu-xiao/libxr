#pragma once

#include <atomic>

#include "can.hpp"
#include "ti_msp_dl_config.h"

#if defined(__MSPM0_HAS_MCAN__)

#include "dl_mcan.h"

namespace LibXR
{

class MSPM0CAN : public CAN
{
 public:
  struct Resources
  {
    MCAN_Regs* instance;
    IRQn_Type irqn;
    uint8_t index;
  };

  MSPM0CAN(Resources res, uint32_t tx_pool_size = 8);

  ErrorCode SetConfig(const CAN::Configuration& cfg) override;

  uint32_t GetClockFreq() const override;

  static uint32_t ResolveClockFreq(MCAN_Regs* instance);

  ErrorCode Init();

  ErrorCode AddMessage(const ClassicPack& pack) override;

  ErrorCode GetErrorState(CAN::ErrorState& state) const override;

  static void OnInterrupt(uint8_t index);

  static constexpr uint8_t ResolveIndex(IRQn_Type irqn)
  {
    switch (irqn)
    {
#if defined(CANFD0_BASE)
      case CANFD0_INT_IRQn:
        return 0;
#endif
#if defined(CANFD1_BASE)
      case CANFD1_INT_IRQn:
        return 1;
#endif
      default:
        return INVALID_INSTANCE_INDEX;
    }
  }

 private:
  static constexpr uint8_t MAX_CAN_INSTANCES = 2;
  static constexpr uint8_t INVALID_INSTANCE_INDEX = 0xFF;
  static constexpr uint32_t INIT_TIMEOUT = 300000U;

  ErrorCode SendImmediate(const ClassicPack& pack);

  void ProcessTxInterrupt();

  void ProcessErrorInterrupt(uint32_t intr_status);

  void ProcessRxFIFO(uint32_t fifo_num);

  void HandleMcanLineInterrupt(DL_MCAN_INTR_SRC_MCAN line);

  void HandleInterrupt();

  Resources res_;
  LockFreePool<ClassicPack> tx_pool_;
  std::atomic<uint32_t> tx_lock_{0};
  std::atomic<uint32_t> tx_pend_{0};

  static MSPM0CAN* instance_map_[MAX_CAN_INSTANCES];
};

#define MSPM0_CAN_INIT(name, tx_pool_size)                                             \
  ::LibXR::MSPM0CAN::Resources{name##_INST, name##_INST_INT_IRQN,                      \
                               ::LibXR::MSPM0CAN::ResolveIndex(name##_INST_INT_IRQN)}, \
      (tx_pool_size)

}  // namespace LibXR

#endif
