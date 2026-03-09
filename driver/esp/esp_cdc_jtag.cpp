#include "esp_cdc_jtag.hpp"

#if SOC_USB_SERIAL_JTAG_SUPPORTED &&                                      \
    ((defined(CONFIG_IDF_TARGET_ESP32C3) && CONFIG_IDF_TARGET_ESP32C3) || \
     (defined(CONFIG_IDF_TARGET_ESP32C6) && CONFIG_IDF_TARGET_ESP32C6))

#include <algorithm>
#include <cstring>
#include <new>

#include "esp_attr.h"
#include "hal/usb_serial_jtag_ll.h"
#include "soc/interrupts.h"

namespace
{
constexpr uint32_t kTxIntrMask = USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY;
constexpr uint32_t kRxIntrMask = USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT;
constexpr uint32_t kAllIntrMask = kTxIntrMask | kRxIntrMask;
}  // namespace

namespace LibXR
{

ESP32CDCJtag::ESP32CDCJtag(size_t rx_buffer_size, size_t tx_buffer_size,
                           uint32_t tx_queue_size, UART::Configuration config)
    : UART(&_read_port, &_write_port),
      config_(config),
      tx_slot_storage_(new (std::nothrow) uint8_t[tx_buffer_size * 2U]),
      tx_slot_size_(tx_buffer_size),
      _read_port(rx_buffer_size),
      _write_port(tx_queue_size, tx_buffer_size)
{
  ASSERT(tx_slot_storage_ != nullptr);
  ASSERT(tx_slot_size_ > 0U);

  tx_slot_a_ = tx_slot_storage_;
  tx_slot_b_ = tx_slot_storage_ + tx_slot_size_;

  _read_port = ReadFun;
  _write_port = WriteFun;

  if (SetConfig(config_) != ErrorCode::OK)
  {
    ASSERT(false);
    return;
  }

  if (InitHardware() != ErrorCode::OK)
  {
    ASSERT(false);
  }
}

ErrorCode ESP32CDCJtag::SetConfig(UART::Configuration config)
{
  if ((config.data_bits != 8) || (config.stop_bits != 1) ||
      (config.parity != UART::Parity::NO_PARITY))
  {
    return ErrorCode::ARG_ERR;
  }
  config_ = config;
  return ErrorCode::OK;
}

ErrorCode ESP32CDCJtag::InitHardware()
{
  if (hw_inited_)
  {
    return ErrorCode::OK;
  }

  ResetTxState(false);

  const esp_err_t intr_ans = esp_intr_alloc(
      ETS_USB_SERIAL_JTAG_INTR_SOURCE, ESP_INTR_FLAG_IRAM, IsrEntry, this, &intr_handle_);
  intr_installed_ = (intr_ans == ESP_OK);
  if (!intr_installed_)
  {
    intr_handle_ = nullptr;
    return ErrorCode::INIT_ERR;
  }

  usb_serial_jtag_ll_clr_intsts_mask(kAllIntrMask);
  usb_serial_jtag_ll_ena_intr_mask(kRxIntrMask);

  hw_inited_ = true;
  return ErrorCode::OK;
}

void IRAM_ATTR ESP32CDCJtag::IsrEntry(void* arg)
{
  auto* cdc = static_cast<ESP32CDCJtag*>(arg);
  if (cdc != nullptr)
  {
    cdc->HandleInterrupt();
  }
}

ErrorCode IRAM_ATTR ESP32CDCJtag::WriteFun(WritePort& port, bool in_isr)
{
  auto* cdc = CONTAINER_OF(&port, ESP32CDCJtag, _write_port);
  return cdc->TryStartTx(in_isr);
}

ErrorCode ESP32CDCJtag::ReadFun(ReadPort&, bool) { return ErrorCode::PENDING; }

void IRAM_ATTR ESP32CDCJtag::ClearActiveTx()
{
  tx_active_ptr_ = nullptr;
  tx_active_size_ = 0;
  tx_active_offset_ = 0;
  tx_active_info_ = {};
  tx_active_valid_ = false;
  tx_active_reported_ = false;
}

void IRAM_ATTR ESP32CDCJtag::ClearPendingTx()
{
  tx_pending_ptr_ = nullptr;
  tx_pending_size_ = 0;
  tx_pending_info_ = {};
  tx_pending_valid_ = false;
}

void IRAM_ATTR ESP32CDCJtag::ResetTxState(bool)
{
  ClearActiveTx();
  ClearPendingTx();
  tx_busy_.store(false, std::memory_order_release);
}

bool IRAM_ATTR ESP32CDCJtag::DequeueTxToSlot(uint8_t* slot, size_t& size,
                                             WriteInfoBlock& info, bool in_isr)
{
  (void)in_isr;

  WriteInfoBlock peek_info = {};
  if (write_port_->queue_info_->Peek(peek_info) != ErrorCode::OK)
  {
    return false;
  }

  if (peek_info.data.size_ > tx_slot_size_)
  {
    ASSERT(false);
    return false;
  }

  if (write_port_->queue_data_->PopBatch(slot, peek_info.data.size_) != ErrorCode::OK)
  {
    return false;
  }

  if (write_port_->queue_info_->Pop(info) != ErrorCode::OK)
  {
    return false;
  }

  size = peek_info.data.size_;
  return true;
}

bool IRAM_ATTR ESP32CDCJtag::LoadActiveTxFromQueue(bool in_isr)
{
  if (tx_active_valid_)
  {
    return true;
  }

  size_t active_size = 0U;
  if (!DequeueTxToSlot(tx_slot_a_, active_size, tx_active_info_, in_isr))
  {
    return false;
  }

  tx_active_ptr_ = tx_slot_a_;
  tx_active_size_ = active_size;
  tx_active_offset_ = 0;
  tx_active_valid_ = true;
  tx_active_reported_ = false;
  return true;
}

bool IRAM_ATTR ESP32CDCJtag::LoadPendingTxFromQueue(bool in_isr)
{
  if (tx_pending_valid_)
  {
    return false;
  }

  uint8_t* pending_slot = tx_slot_b_;
  if (tx_active_ptr_ == tx_slot_b_)
  {
    pending_slot = tx_slot_a_;
  }

  size_t pending_size = 0U;
  if (!DequeueTxToSlot(pending_slot, pending_size, tx_pending_info_, in_isr))
  {
    return false;
  }

  tx_pending_ptr_ = pending_slot;
  tx_pending_size_ = pending_size;
  tx_pending_valid_ = true;
  return true;
}

bool IRAM_ATTR ESP32CDCJtag::PumpTx(bool)
{
  while (tx_busy_.load(std::memory_order_acquire))
  {
    if (!tx_active_valid_ || (tx_active_ptr_ == nullptr) ||
        (tx_active_offset_ >= tx_active_size_))
    {
      tx_busy_.store(false, std::memory_order_release);
      return true;
    }

    const uint32_t remain = static_cast<uint32_t>(tx_active_size_ - tx_active_offset_);
    const int written =
        usb_serial_jtag_ll_write_txfifo(tx_active_ptr_ + tx_active_offset_, remain);
    if (written <= 0)
    {
      return false;
    }

    tx_active_offset_ += static_cast<size_t>(written);
    if (tx_active_offset_ >= tx_active_size_)
    {
      tx_busy_.store(false, std::memory_order_release);
      return true;
    }
  }

  return false;
}

void IRAM_ATTR ESP32CDCJtag::PushRxBytes(const uint8_t* data, size_t size, bool in_isr)
{
  size_t offset = 0U;
  bool pushed_any = false;

  while (offset < size)
  {
    const size_t free_space = read_port_->queue_data_->EmptySize();
    if (free_space == 0U)
    {
      break;
    }

    const size_t chunk = std::min(free_space, size - offset);
    if (read_port_->queue_data_->PushBatch(data + offset, chunk) != ErrorCode::OK)
    {
      break;
    }

    offset += chunk;
    pushed_any = true;
  }

  if (pushed_any)
  {
    read_port_->ProcessPendingReads(in_isr);
  }
}

bool IRAM_ATTR ESP32CDCJtag::StartActiveTransfer(bool in_isr)
{
  if (!tx_active_valid_ || (tx_active_ptr_ == nullptr) || (tx_active_size_ == 0U))
  {
    return false;
  }

  bool expected = false;
  if (!tx_busy_.compare_exchange_strong(expected, true, std::memory_order_acq_rel,
                                        std::memory_order_acquire))
  {
    return true;
  }

  tx_active_offset_ = 0;
  usb_serial_jtag_ll_ena_intr_mask(kTxIntrMask);
  (void)PumpTx(in_isr);
  return true;
}

bool IRAM_ATTR ESP32CDCJtag::StartAndReportActive(bool in_isr)
{
  if (!StartActiveTransfer(in_isr))
  {
    write_port_->Finish(in_isr, ErrorCode::FAILED, tx_active_info_);
    ClearActiveTx();
    return false;
  }

  // Keep aligned with STM/CH: once next op is kicked to HW, report it finished.
  write_port_->Finish(in_isr, ErrorCode::OK, tx_active_info_);
  tx_active_reported_ = true;
  if (!tx_busy_.load(std::memory_order_acquire) && tx_active_valid_)
  {
    OnTxTransferDone(in_isr, ErrorCode::OK);
  }
  return true;
}

void IRAM_ATTR ESP32CDCJtag::StopTxTransfer()
{
  usb_serial_jtag_ll_txfifo_flush();
  usb_serial_jtag_ll_disable_intr_mask(kTxIntrMask);
}

void IRAM_ATTR ESP32CDCJtag::OnTxTransferDone(bool in_isr, ErrorCode result)
{
  Flag::ScopedRestore tx_flag(in_tx_isr_);
  tx_busy_.store(false, std::memory_order_release);

  if (tx_active_valid_ && !tx_active_reported_)
  {
    write_port_->Finish(in_isr, result, tx_active_info_);
    tx_active_reported_ = true;
  }

  ClearActiveTx();

  if ((result != ErrorCode::OK) && tx_pending_valid_)
  {
    write_port_->Finish(in_isr, ErrorCode::FAILED, tx_pending_info_);
    ClearPendingTx();
  }

  if (result != ErrorCode::OK)
  {
    StopTxTransfer();
    return;
  }

  if (tx_pending_valid_)
  {
    tx_active_ptr_ = tx_pending_ptr_;
    tx_active_size_ = tx_pending_size_;
    tx_active_offset_ = 0;
    tx_active_info_ = tx_pending_info_;
    tx_active_valid_ = true;
    tx_active_reported_ = false;
    ClearPendingTx();
    (void)StartAndReportActive(in_isr);
  }
  else
  {
    if (LoadActiveTxFromQueue(in_isr))
    {
      (void)StartAndReportActive(in_isr);
    }
  }

  if (!tx_pending_valid_)
  {
    (void)LoadPendingTxFromQueue(in_isr);
  }

  if (!tx_busy_.load(std::memory_order_acquire) && !tx_active_valid_ &&
      !tx_pending_valid_)
  {
    StopTxTransfer();
  }
}

ErrorCode IRAM_ATTR ESP32CDCJtag::TryStartTx(bool in_isr)
{
  if (in_tx_isr_.IsSet())
  {
    return ErrorCode::PENDING;
  }

  if (!tx_active_valid_)
  {
    (void)LoadActiveTxFromQueue(in_isr);
  }

  if (!tx_busy_.load(std::memory_order_acquire) && tx_active_valid_)
  {
    const bool report_by_write_port = !tx_active_reported_;
    if (report_by_write_port)
    {
      tx_active_reported_ = true;
    }

    if (!StartActiveTransfer(in_isr))
    {
      ClearActiveTx();
      return ErrorCode::FAILED;
    }

    if (!tx_busy_.load(std::memory_order_acquire) && tx_active_valid_)
    {
      OnTxTransferDone(in_isr, ErrorCode::OK);
    }

    if (report_by_write_port)
    {
      if (!tx_pending_valid_)
      {
        (void)LoadPendingTxFromQueue(in_isr);
      }
      return ErrorCode::OK;
    }
  }

  if (!tx_pending_valid_)
  {
    (void)LoadPendingTxFromQueue(in_isr);
  }

  return ErrorCode::PENDING;
}

void IRAM_ATTR ESP32CDCJtag::HandleInterrupt()
{
  const uint32_t status = usb_serial_jtag_ll_get_intsts_mask();

  const uint32_t rx_status = status & kRxIntrMask;
  if (rx_status != 0U)
  {
    usb_serial_jtag_ll_clr_intsts_mask(rx_status);

    uint8_t rx_tmp[64] = {};
    const int got = usb_serial_jtag_ll_read_rxfifo(rx_tmp, sizeof(rx_tmp));
    if (got > 0)
    {
      PushRxBytes(rx_tmp, static_cast<size_t>(got), true);
    }
  }

  const uint32_t tx_status = status & kTxIntrMask;
  if (tx_status == 0U)
  {
    return;
  }

  usb_serial_jtag_ll_clr_intsts_mask(tx_status);

  Flag::ScopedRestore tx_flag(in_tx_isr_);
  const bool was_busy = tx_busy_.load(std::memory_order_acquire);
  (void)PumpTx(true);
  if (was_busy && !tx_busy_.load(std::memory_order_acquire))
  {
    OnTxTransferDone(true, ErrorCode::OK);
  }
}

}  // namespace LibXR

#endif
