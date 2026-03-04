#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "driver/gpio.h"
#include "esp_intr_alloc.h"
#include "flag.hpp"
#include "hal/uart_hal.h"
#include "hal/uart_types.h"
#include "soc/periph_defs.h"
#include "soc/soc_caps.h"
#include "uart.hpp"

#if SOC_GDMA_SUPPORTED && SOC_UHCI_SUPPORTED
#include "esp_private/gdma.h"
#include "esp_private/gdma_link.h"
#include "hal/uhci_hal.h"
#endif

namespace LibXR
{

class ESP32UART : public UART
{
 public:
  static constexpr int PIN_NO_CHANGE = -1;

  ESP32UART(uart_port_t uart_num, int tx_pin, int rx_pin,
            int rts_pin = PIN_NO_CHANGE, int cts_pin = PIN_NO_CHANGE,
            size_t rx_buffer_size = 1024, size_t tx_buffer_size = 512,
            uint32_t tx_queue_size = 5,
            UART::Configuration config = {115200, UART::Parity::NO_PARITY, 8, 1},
            bool enable_dma = true);

  ~ESP32UART();

  ErrorCode SetConfig(UART::Configuration config) override;

  ErrorCode SetLoopback(bool enable);

  static ErrorCode WriteFun(WritePort& port, bool in_isr);

  static ErrorCode ReadFun(ReadPort& port, bool in_isr);

 private:
  static uint8_t* AllocateTxStorage(size_t size);

  static void FreeTxStorage(uint8_t* storage);

  static ErrorCode ResolveUartPeriph(uart_port_t uart_num, periph_module_t& out);

  static bool ResolveWordLength(uint8_t data_bits, uart_word_length_t& out);

  static bool ResolveStopBits(uint8_t stop_bits, uart_stop_bits_t& out);

  static uart_parity_t ResolveParity(UART::Parity parity);

  static void UartIsrEntry(void* arg);

#if SOC_GDMA_SUPPORTED && SOC_UHCI_SUPPORTED
  static bool DmaTxEofCallback(gdma_channel_handle_t dma_chan,
                               gdma_event_data_t* event_data,
                               void* user_data);

  static bool DmaTxDescrErrCallback(gdma_channel_handle_t dma_chan,
                                    gdma_event_data_t* event_data,
                                    void* user_data);

  static bool DmaRxDoneCallback(gdma_channel_handle_t dma_chan,
                                gdma_event_data_t* event_data,
                                void* user_data);

  static bool DmaRxDescrErrCallback(gdma_channel_handle_t dma_chan,
                                    gdma_event_data_t* event_data,
                                    void* user_data);
#endif

  ErrorCode InitUartHardware();

  void DeinitUartHardware();

  ErrorCode ConfigurePins();

  ErrorCode InstallUartIsr();

  void RemoveUartIsr();

  void ConfigureRxInterruptPath();

#if SOC_GDMA_SUPPORTED && SOC_UHCI_SUPPORTED
  ErrorCode InitDmaBackend();

  void DeinitDmaBackend();

  bool StartDmaTx();

  void HandleDmaRxDone(gdma_event_data_t* event_data);

  void HandleDmaRxError();

  void HandleDmaTxError();

  void PushDmaRxData(size_t recv_size, bool in_isr);
#endif

  ErrorCode TryStartTx(bool in_isr);

  bool LoadActiveTxFromQueue(bool in_isr);

  bool LoadPendingTxFromQueue(bool in_isr);

  bool DequeueTxToBuffer(uint8_t* buffer, size_t& size, WriteInfoBlock& info,
                         bool in_isr);

  bool StartActiveTransfer(bool in_isr);
  bool StartAndReportActive(bool in_isr);
  void ClearActiveTx();
  void ClearPendingTx();

  void FillTxFifo(bool in_isr);

  void PushRxBytes(const uint8_t* data, size_t size, bool in_isr);

  void DrainRxFifoFromIsr();

  void HandleRxInterrupt(uint32_t uart_intr_status);

  void HandleTxInterrupt(uint32_t uart_intr_status);

  void HandleUartInterrupt();

  void OnTxTransferDone(bool in_isr, ErrorCode result);

  uart_port_t uart_num_;
  int tx_pin_;
  int rx_pin_;
  int rts_pin_;
  int cts_pin_;

  UART::Configuration config_;

  uint8_t* rx_isr_buffer_ = nullptr;
  size_t rx_isr_buffer_size_ = 0;

  uint8_t* tx_storage_ = nullptr;
  size_t tx_storage_size_ = 0;
  size_t tx_buffer_size_ = 0;
  uint8_t* tx_active_buffer_ = nullptr;
  uint8_t* tx_pending_buffer_ = nullptr;
  size_t tx_active_length_ = 0;
  size_t tx_pending_length_ = 0;
  size_t tx_active_offset_ = 0;

  WriteInfoBlock tx_active_info_ = {};
  WriteInfoBlock tx_pending_info_ = {};
  Flag::Plain tx_busy_;
  Flag::Plain in_tx_isr_;
  bool tx_active_valid_ = false;
  bool tx_active_reported_ = false;
  bool tx_pending_valid_ = false;

  bool uart_hw_enabled_ = false;
  uart_hal_context_t uart_hal_ = {};
  intr_handle_t uart_intr_handle_ = nullptr;
  bool uart_isr_installed_ = false;
  bool dma_requested_ = true;

  ReadPort _read_port;
  WritePort _write_port;

#if SOC_GDMA_SUPPORTED && SOC_UHCI_SUPPORTED
  bool dma_backend_enabled_ = false;
  uhci_hal_context_t uhci_hal_ = {};
  gdma_channel_handle_t tx_dma_channel_ = nullptr;
  gdma_channel_handle_t rx_dma_channel_ = nullptr;
  gdma_link_list_handle_t tx_dma_links_[2] = {nullptr, nullptr};
  uintptr_t tx_dma_head_addr_[2] = {0U, 0U};
  uint8_t* tx_dma_buffer_addr_[2] = {nullptr, nullptr};
  gdma_link_list_handle_t rx_dma_link_ = nullptr;
  size_t tx_dma_alignment_ = 1;
  size_t rx_dma_alignment_ = 1;
  uint8_t* rx_dma_storage_ = nullptr;
  size_t rx_dma_chunk_size_ = 0;
  uint32_t rx_dma_node_count_ = 0;
  uint32_t rx_dma_node_index_ = 0;
#endif
};

}  // namespace LibXR
