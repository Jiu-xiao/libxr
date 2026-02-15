#include "esp_uart.hpp"

#if SOC_GDMA_SUPPORTED && SOC_UHCI_SUPPORTED

#include <algorithm>

#include "esp_attr.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_private/periph_ctrl.h"
#include "hal/uhci_ll.h"

namespace
{
constexpr uint32_t kDmaRxNodeCount = 2;

size_t AlignUp(size_t value, size_t align)
{
  if (align <= 1)
  {
    return value;
  }
  return ((value + align - 1) / align) * align;
}
}  // namespace

namespace LibXR
{

bool IRAM_ATTR ESP32UART::DmaTxEofCallback(gdma_channel_handle_t,
                                           gdma_event_data_t*,
                                           void* user_data)
{
  auto* uart = static_cast<ESP32UART*>(user_data);
  if (uart != nullptr)
  {
    uart->OnTxTransferDone(true, ErrorCode::OK);
  }
  return false;
}

bool IRAM_ATTR ESP32UART::DmaTxDescrErrCallback(gdma_channel_handle_t,
                                                gdma_event_data_t*,
                                                void* user_data)
{
  auto* uart = static_cast<ESP32UART*>(user_data);
  if (uart != nullptr)
  {
    uart->HandleDmaTxError();
  }
  return false;
}

bool IRAM_ATTR ESP32UART::DmaRxDoneCallback(gdma_channel_handle_t,
                                            gdma_event_data_t* event_data,
                                            void* user_data)
{
  auto* uart = static_cast<ESP32UART*>(user_data);
  if (uart != nullptr)
  {
    uart->HandleDmaRxDone(event_data);
  }
  return false;
}

bool IRAM_ATTR ESP32UART::DmaRxDescrErrCallback(gdma_channel_handle_t,
                                                gdma_event_data_t*,
                                                void* user_data)
{
  auto* uart = static_cast<ESP32UART*>(user_data);
  if (uart != nullptr)
  {
    uart->HandleDmaRxError();
  }
  return false;
}

ErrorCode ESP32UART::InitDmaBackend()
{
  if (dma_backend_enabled_)
  {
    return ErrorCode::OK;
  }

  periph_module_enable(PERIPH_UHCI0_MODULE);
  periph_module_reset(PERIPH_UHCI0_MODULE);

  uhci_hal_init(&uhci_hal_, 0);
  uhci_ll_attach_uart_port(uhci_hal_.dev, uart_num_);

  uhci_seper_chr_t sep_chr = {};
  sep_chr.sub_chr_en = 0;
  uhci_ll_set_seper_chr(uhci_hal_.dev, &sep_chr);
  uhci_ll_rx_set_eof_mode(uhci_hal_.dev, UHCI_RX_IDLE_EOF);

  gdma_channel_alloc_config_t tx_cfg = {
      .sibling_chan = nullptr,
      .direction = GDMA_CHANNEL_DIRECTION_TX,
      .flags = {},
  };
  if (gdma_new_ahb_channel(&tx_cfg, &tx_dma_channel_) != ESP_OK)
  {
    DeinitDmaBackend();
    return ErrorCode::INIT_ERR;
  }

  if (gdma_connect(tx_dma_channel_, GDMA_MAKE_TRIGGER(GDMA_TRIG_PERIPH_UHCI, 0)) !=
      ESP_OK)
  {
    DeinitDmaBackend();
    return ErrorCode::INIT_ERR;
  }

  gdma_transfer_config_t transfer_cfg = {
      .max_data_burst_size = 0,
      .access_ext_mem = true,
  };
  if (gdma_config_transfer(tx_dma_channel_, &transfer_cfg) != ESP_OK)
  {
    DeinitDmaBackend();
    return ErrorCode::INIT_ERR;
  }

  size_t tx_int_alignment = 1;
  size_t tx_ext_alignment = 1;
  if (gdma_get_alignment_constraints(tx_dma_channel_, &tx_int_alignment,
                                     &tx_ext_alignment) != ESP_OK)
  {
    DeinitDmaBackend();
    return ErrorCode::INIT_ERR;
  }
  tx_dma_alignment_ = std::max<size_t>(1, std::max(tx_int_alignment, tx_ext_alignment));

  gdma_strategy_config_t tx_strategy = {
      .owner_check = true,
      .auto_update_desc = true,
      .eof_till_data_popped = true,
  };
  if (gdma_apply_strategy(tx_dma_channel_, &tx_strategy) != ESP_OK)
  {
    DeinitDmaBackend();
    return ErrorCode::INIT_ERR;
  }

  gdma_link_list_config_t tx_link_cfg = {
      .num_items = 1,
      .item_alignment = 4,
      .flags = {},
  };
  if (gdma_new_link_list(&tx_link_cfg, &tx_dma_link_) != ESP_OK)
  {
    DeinitDmaBackend();
    return ErrorCode::INIT_ERR;
  }

  gdma_tx_event_callbacks_t tx_callbacks = {
      .on_trans_eof = DmaTxEofCallback,
      .on_descr_err = DmaTxDescrErrCallback,
  };
  if (gdma_register_tx_event_callbacks(tx_dma_channel_, &tx_callbacks, this) != ESP_OK)
  {
    DeinitDmaBackend();
    return ErrorCode::INIT_ERR;
  }

  gdma_channel_alloc_config_t rx_cfg = {
      .sibling_chan = nullptr,
      .direction = GDMA_CHANNEL_DIRECTION_RX,
      .flags = {},
  };
  if (gdma_new_ahb_channel(&rx_cfg, &rx_dma_channel_) != ESP_OK)
  {
    DeinitDmaBackend();
    return ErrorCode::INIT_ERR;
  }

  if (gdma_connect(rx_dma_channel_, GDMA_MAKE_TRIGGER(GDMA_TRIG_PERIPH_UHCI, 0)) !=
      ESP_OK)
  {
    DeinitDmaBackend();
    return ErrorCode::INIT_ERR;
  }

  if (gdma_config_transfer(rx_dma_channel_, &transfer_cfg) != ESP_OK)
  {
    DeinitDmaBackend();
    return ErrorCode::INIT_ERR;
  }

  size_t rx_int_alignment = 1;
  size_t rx_ext_alignment = 1;
  if (gdma_get_alignment_constraints(rx_dma_channel_, &rx_int_alignment,
                                     &rx_ext_alignment) != ESP_OK)
  {
    DeinitDmaBackend();
    return ErrorCode::INIT_ERR;
  }
  rx_dma_alignment_ = std::max<size_t>(1, std::max(rx_int_alignment, rx_ext_alignment));

  gdma_link_list_config_t rx_link_cfg = {
      .num_items = kDmaRxNodeCount,
      .item_alignment = 4,
      .flags = {},
  };
  if (gdma_new_link_list(&rx_link_cfg, &rx_dma_link_) != ESP_OK)
  {
    DeinitDmaBackend();
    return ErrorCode::INIT_ERR;
  }

  const size_t rx_chunk_target = std::min<size_t>(rx_isr_buffer_size_, 128);
  rx_dma_chunk_size_ = std::max<size_t>(AlignUp(rx_chunk_target, 4), 32);
  rx_dma_node_count_ = kDmaRxNodeCount;
  const size_t rx_storage_alignment = std::max<size_t>(4, rx_dma_alignment_);
  const size_t rx_storage_bytes =
      AlignUp(rx_dma_chunk_size_ * rx_dma_node_count_, rx_storage_alignment);

  rx_dma_storage_ = static_cast<uint8_t*>(heap_caps_aligned_alloc(
      rx_storage_alignment,
      rx_storage_bytes,
      MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  if (rx_dma_storage_ == nullptr)
  {
    DeinitDmaBackend();
    return ErrorCode::NO_MEM;
  }

  gdma_buffer_mount_config_t rx_mount[kDmaRxNodeCount] = {
      {
          .buffer = rx_dma_storage_,
          .buffer_alignment = rx_dma_alignment_,
          .length = rx_dma_chunk_size_,
          .flags = {
              .mark_eof = 0,
              .mark_final = 0,
              .bypass_buffer_align_check = 0,
          },
      },
      {
          .buffer = rx_dma_storage_ + rx_dma_chunk_size_,
          .buffer_alignment = rx_dma_alignment_,
          .length = rx_dma_chunk_size_,
          .flags = {
              .mark_eof = 0,
              .mark_final = 0,
              .bypass_buffer_align_check = 0,
          },
      },
  };

  if (gdma_link_mount_buffers(rx_dma_link_, 0, rx_mount, kDmaRxNodeCount, nullptr) !=
      ESP_OK)
  {
    DeinitDmaBackend();
    return ErrorCode::INIT_ERR;
  }

  gdma_rx_event_callbacks_t rx_callbacks = {
      .on_recv_eof = nullptr,
      .on_descr_err = DmaRxDescrErrCallback,
      .on_recv_done = DmaRxDoneCallback,
  };
  if (gdma_register_rx_event_callbacks(rx_dma_channel_, &rx_callbacks, this) != ESP_OK)
  {
    DeinitDmaBackend();
    return ErrorCode::INIT_ERR;
  }

  if (gdma_reset(rx_dma_channel_) != ESP_OK)
  {
    DeinitDmaBackend();
    return ErrorCode::INIT_ERR;
  }

  if (gdma_start(rx_dma_channel_, gdma_link_get_head_addr(rx_dma_link_)) != ESP_OK)
  {
    DeinitDmaBackend();
    return ErrorCode::INIT_ERR;
  }

  rx_dma_node_index_ = 0;
  dma_backend_enabled_ = true;
  return ErrorCode::OK;
}

void ESP32UART::DeinitDmaBackend()
{
  if (tx_dma_channel_ != nullptr)
  {
    gdma_stop(tx_dma_channel_);
  }
  if (rx_dma_channel_ != nullptr)
  {
    gdma_stop(rx_dma_channel_);
  }

  if (tx_dma_channel_ != nullptr)
  {
    gdma_disconnect(tx_dma_channel_);
    gdma_del_channel(tx_dma_channel_);
    tx_dma_channel_ = nullptr;
  }
  if (rx_dma_channel_ != nullptr)
  {
    gdma_disconnect(rx_dma_channel_);
    gdma_del_channel(rx_dma_channel_);
    rx_dma_channel_ = nullptr;
  }

  if (tx_dma_link_ != nullptr)
  {
    gdma_del_link_list(tx_dma_link_);
    tx_dma_link_ = nullptr;
  }
  if (rx_dma_link_ != nullptr)
  {
    gdma_del_link_list(rx_dma_link_);
    rx_dma_link_ = nullptr;
  }

  if (rx_dma_storage_ != nullptr)
  {
    heap_caps_free(rx_dma_storage_);
    rx_dma_storage_ = nullptr;
  }

  if (uhci_hal_.dev != nullptr)
  {
    uhci_hal_deinit(&uhci_hal_);
    periph_module_disable(PERIPH_UHCI0_MODULE);
  }

  dma_backend_enabled_ = false;
  tx_dma_alignment_ = 1;
  rx_dma_alignment_ = 1;
  rx_dma_chunk_size_ = 0;
  rx_dma_node_count_ = 0;
  rx_dma_node_index_ = 0;
}

bool ESP32UART::StartDmaTx()
{
  if ((tx_dma_channel_ == nullptr) || (tx_dma_link_ == nullptr) || !tx_active_valid_)
  {
    return false;
  }

  gdma_buffer_mount_config_t tx_mount = {
      .buffer = tx_double_buffer_.ActiveBuffer(),
      .buffer_alignment = tx_dma_alignment_,
      .length = tx_double_buffer_.GetActiveLength(),
      .flags = {
          .mark_eof = 1,
          .mark_final = 1,
          .bypass_buffer_align_check = 0,
      },
  };

  if (gdma_link_mount_buffers(tx_dma_link_, 0, &tx_mount, 1, nullptr) != ESP_OK)
  {
    return false;
  }

  return gdma_start(tx_dma_channel_, gdma_link_get_head_addr(tx_dma_link_)) == ESP_OK;
}

void IRAM_ATTR ESP32UART::PushDmaRxData(size_t recv_size, bool in_isr)
{
  if ((rx_dma_storage_ == nullptr) || (rx_dma_chunk_size_ == 0) || (rx_dma_node_count_ == 0))
  {
    return;
  }

  const size_t max_window = rx_dma_chunk_size_ * rx_dma_node_count_;
  size_t remaining = std::min(recv_size, max_window);

  while (remaining > 0)
  {
    const size_t offset = static_cast<size_t>(rx_dma_node_index_) * rx_dma_chunk_size_;
    const size_t chunk = std::min(remaining, rx_dma_chunk_size_);

    PushRxBytes(rx_dma_storage_ + offset, chunk, in_isr);
    remaining -= chunk;
    rx_dma_node_index_ = (rx_dma_node_index_ + 1U) % rx_dma_node_count_;
  }
}

void IRAM_ATTR ESP32UART::HandleDmaRxDone(gdma_event_data_t* event_data)
{
  if ((rx_dma_storage_ == nullptr) || (rx_dma_chunk_size_ == 0) || (rx_dma_node_count_ == 0))
  {
    return;
  }

  if ((event_data != nullptr) && event_data->flags.abnormal_eof)
  {
    HandleDmaRxError();
    return;
  }

  size_t recv_size = rx_dma_chunk_size_;
  if ((event_data != nullptr) && event_data->flags.normal_eof)
  {
    const size_t eof_size =
        gdma_link_count_buffer_size_till_eof(rx_dma_link_, static_cast<int>(rx_dma_node_index_));
    if (eof_size > 0)
    {
      recv_size = eof_size;
    }
  }

  PushDmaRxData(recv_size, true);
}

void IRAM_ATTR ESP32UART::HandleDmaRxError()
{
  if ((rx_dma_channel_ == nullptr) || (rx_dma_link_ == nullptr))
  {
    return;
  }

  gdma_stop(rx_dma_channel_);
  gdma_reset(rx_dma_channel_);
  rx_dma_node_index_ = 0;
  (void)gdma_start(rx_dma_channel_, gdma_link_get_head_addr(rx_dma_link_));
}

void IRAM_ATTR ESP32UART::HandleDmaTxError()
{
  if (tx_dma_channel_ != nullptr)
  {
    gdma_stop(tx_dma_channel_);
    gdma_reset(tx_dma_channel_);
  }
  OnTxTransferDone(true, ErrorCode::FAILED);
}

}  // namespace LibXR

#endif
