#pragma once

#include "dl_uart_main.h"
#include "ti_msp_dl_config.h"
#include "uart.hpp"

namespace LibXR
{

class MSPM0UART : public UART
{
 public:
  struct Resources
  {
    UART_Regs* instance;
    IRQn_Type irqn;
    uint32_t clock_freq;
  };

  MSPM0UART(Resources res, RawData rx_stage_buffer, uint32_t tx_queue_size = 5,
            uint32_t tx_buffer_size = 128,
            UART::Configuration config = {115200, UART::Parity::NO_PARITY, 8, 1});

  ErrorCode SetConfig(UART::Configuration config) override;

  static ErrorCode WriteFun(WritePort& port);

  static ErrorCode ReadFun(ReadPort& port);

  static void OnInterrupt(UART_Regs* instance);

 private:
  ReadPort read_port_impl_;
  WritePort write_port_impl_;

  static constexpr uint8_t MAX_UART_INSTANCES = 8;

  static int GetInstanceIndex(UART_Regs* instance);

  void HandleInterrupt();

  void HandleRxInterrupt();

  void HandleTxInterrupt(bool in_isr);

  void HandleErrorInterrupt(DL_UART_IIDX iidx);

  ErrorCode TryStartTx(bool in_isr);

  void DisableTxInterrupt();

  Resources res_;
  WriteInfoBlock tx_active_info_;
  bool tx_active_valid_ = false;
  size_t tx_active_remaining_ = 0;
  size_t tx_active_total_ = 0;
  uint32_t rx_drop_count_ = 0;
  uint32_t rx_error_count_ = 0;

  static MSPM0UART* instance_map_[MAX_UART_INSTANCES];
};

// Helper macro to initialize MSPM0UART from SysConfig in one shot
#define MSPM0_UART_INIT(name, rx_stage_addr, rx_stage_size, tx_queue_size,  \
                        tx_buffer_size)                                     \
  ::LibXR::MSPM0UART::Resources{name##_INST, name##_INST_INT_IRQN,          \
                                name##_INST_FREQUENCY},                     \
      ::LibXR::RawData{(rx_stage_addr), (rx_stage_size)}, (tx_queue_size),  \
      (tx_buffer_size),                                                     \
      ::LibXR::UART::Configuration{static_cast<uint32_t>(name##_BAUD_RATE), \
                                   ::LibXR::UART::Parity::NO_PARITY, 8, 1}

}  // namespace LibXR
