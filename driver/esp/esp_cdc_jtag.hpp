#pragma once

#include "esp_def.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "esp_intr_alloc.h"
#include "flag.hpp"
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


  ErrorCode SetConfig(UART::Configuration config) override;

  static ErrorCode WriteFun(WritePort& port, bool in_isr);

  static ErrorCode ReadFun(ReadPort& port, bool in_isr);

 private:
  static void IsrEntry(void* arg);

  ErrorCode InitHardware();


  void HandleInterrupt();

  ErrorCode TryStartTx(bool in_isr);
  bool LoadActiveTxFromQueue(bool in_isr);
  bool LoadPendingTxFromQueue(bool in_isr);
  bool DequeueTxToSlot(uint8_t* slot, size_t& size, WriteInfoBlock& info, bool in_isr);
  bool StartActiveTransfer(bool in_isr);
  bool StartAndReportActive(bool in_isr);
  void StopTxTransfer();
  void OnTxTransferDone(bool in_isr, ErrorCode result);
  bool PumpTx(bool in_isr);
  void PushRxBytes(const uint8_t* data, size_t size, bool in_isr);
  void ClearActiveTx();
  void ClearPendingTx();
  void ResetTxState(bool in_isr);

  UART::Configuration config_;
  uint8_t* tx_slot_storage_ = nullptr;
  size_t tx_slot_size_ = 0;
  uint8_t* tx_slot_a_ = nullptr;
  uint8_t* tx_slot_b_ = nullptr;
  intr_handle_t intr_handle_ = nullptr;
  bool intr_installed_ = false;
  bool hw_inited_ = false;
  const uint8_t* tx_active_ptr_ = nullptr;
  size_t tx_active_size_ = 0;
  size_t tx_active_offset_ = 0;
  WriteInfoBlock tx_active_info_ = {};
  bool tx_active_valid_ = false;
  const uint8_t* tx_pending_ptr_ = nullptr;
  size_t tx_pending_size_ = 0;
  bool tx_pending_valid_ = false;
  WriteInfoBlock tx_pending_info_ = {};
  std::atomic<bool> tx_busy_{false};
  Flag::Plain in_tx_isr_;

  ReadPort _read_port;
  WritePort _write_port;
};

}  // namespace LibXR

#endif
