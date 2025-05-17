#pragma once

#include "main.h"

#if defined(HAL_PCD_MODULE_ENABLED) && defined(LIBXR_SYSTEM_ThreadX)

#include "app_usbx_device.h"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "mutex.hpp"
#include "semaphore.hpp"
#include "uart.hpp"
#include "ux_api.h"
#include "ux_device_class_cdc_acm.h"
#include "ux_device_stack.h"
#include "tx_api.h"

namespace LibXR
{

class STM32VirtualUART : public UART
{
 public:
  STM32VirtualUART(PCD_HandleTypeDef *hpcd, ULONG tx_stack_size, UINT tx_priority,
                   ULONG rx_stack_size, UINT rx_priority, uint32_t rx_queue_size = 5,
                   uint32_t tx_queue_size = 5, size_t buffer_size = 512);

  ~STM32VirtualUART();

  ErrorCode SetConfig(UART::Configuration config) override;

  static ErrorCode ReadFun(ReadPort &port);
  static ErrorCode WriteFun(WritePort &port);

  static STM32VirtualUART *instance_;

  UX_SLAVE_CLASS_CDC_ACM *cdc_acm_ = nullptr;

 private:
  void RxLoop();
  void TxLoop();

  static void RxThreadEntry(ULONG arg);
  static void TxThreadEntry(ULONG arg);

  TX_THREAD tx_thread_;
  TX_THREAD rx_thread_;
  void *tx_stack_mem_ = nullptr;
  void *rx_stack_mem_ = nullptr;
  ULONG tx_stack_size_;
  ULONG rx_stack_size_;
  UINT tx_priority_;
  UINT rx_priority_;
  size_t buffer_size_;

  uint8_t *rx_buff_;
  uint8_t *tx_buff_;

  Semaphore write_sem_;
  Mutex read_mutex_;
};

}  // namespace LibXR

#endif
