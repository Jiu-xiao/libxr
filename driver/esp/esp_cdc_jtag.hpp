#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "sdkconfig.h"
#include "esp_intr_alloc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "soc/soc_caps.h"
#include "uart.hpp"

#if SOC_USB_SERIAL_JTAG_SUPPORTED &&                                              \
    ((defined(CONFIG_IDF_TARGET_ESP32C3) && CONFIG_IDF_TARGET_ESP32C3) ||         \
     (defined(CONFIG_IDF_TARGET_ESP32C6) && CONFIG_IDF_TARGET_ESP32C6))

namespace LibXR
{

#if defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG) && CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
static_assert(false,
              "ESP32CDCJtag conflicts with ESP-IDF primary USB Serial/JTAG console. "
              "Set CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=n or disable this backend.");
#endif

class ESP32CDCJtag : public UART
{
 public:
  explicit ESP32CDCJtag(
      size_t rx_buffer_size = 1024, size_t tx_buffer_size = 512,
      uint32_t tx_queue_size = 5,
      UART::Configuration config = {115200, UART::Parity::NO_PARITY, 8, 1});

  ~ESP32CDCJtag();

  ErrorCode SetConfig(UART::Configuration config) override;

  static ErrorCode WriteFun(WritePort& port, bool in_isr);
  static ErrorCode FastWriteFun(WritePort& port, ConstRawData data, WriteOperation& op,
                                bool in_isr);

  static ErrorCode ReadFun(ReadPort& port, bool in_isr);

 private:
  static void IsrEntry(void* arg);

  ErrorCode InitHardware();

  void DeinitHardware();

  void HandleInterrupt();

  ErrorCode WriteBlocking(const uint8_t* data, size_t size, bool in_isr);
  bool PumpTx(bool in_isr);
  void ResetTxState(bool in_isr);

  UART::Configuration config_;
  uint8_t* tx_work_buffer_ = nullptr;
  size_t tx_work_buffer_size_ = 0;
  uint8_t* tx_slot_storage_ = nullptr;
  size_t tx_slot_size_ = 0;
  uint8_t* tx_slot_a_ = nullptr;
  uint8_t* tx_slot_b_ = nullptr;
  intr_handle_t intr_handle_ = nullptr;
  bool intr_installed_ = false;
  bool hw_inited_ = false;
  portMUX_TYPE tx_lock_ = portMUX_INITIALIZER_UNLOCKED;
  const uint8_t* tx_active_ptr_ = nullptr;
  size_t tx_active_size_ = 0;
  size_t tx_active_offset_ = 0;
  const uint8_t* tx_pending_ptr_ = nullptr;
  size_t tx_pending_size_ = 0;
  bool tx_pending_valid_ = false;
  std::atomic<bool> tx_busy_{false};
  std::atomic<bool> tx_source_done_{true};
  std::atomic<uint32_t> tx_event_seq_{0};

  ReadPort _read_port;
  WritePort _write_port;
};

}  // namespace LibXR

#endif
