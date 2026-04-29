#include "esp_uart.hpp"

#if SOC_GDMA_SUPPORTED && SOC_UHCI_SUPPORTED

#include <algorithm>
#include <array>

#include "esp_attr.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_private/periph_ctrl.h"
#include "hal/uhci_ll.h"
#include "soc/ext_mem_defs.h"

namespace
{
// RX uses a circular DMA descriptor ring, similar to STM/CH circular RX DMA
// behavior (continuous receive + software consumer index).
constexpr uint32_t DMA_RX_NODE_COUNT = 8;
constexpr size_t DMA_MAX_BUFFER_SIZE_PER_LINK_ITEM = 4095U;

struct GdmaLinkItem
{
  struct
  {
    uint32_t size : 12;
    uint32_t length : 12;
    uint32_t reserved24 : 4;
    uint32_t err_eof : 1;
    uint32_t reserved29 : 1;
    uint32_t suc_eof : 1;
    uint32_t owner : 1;
  } dw0;
  void* buffer;
  GdmaLinkItem* next;
};

constexpr uint32_t GDMA_OWNER_CPU = 0U;
constexpr uint32_t GDMA_OWNER_DMA = 1U;

size_t AlignUp(size_t value, size_t align)
{
  if (align <= 1)
  {
    return value;
  }
  return ((value + align - 1) / align) * align;
}

uintptr_t CacheAddrToNonCache(uintptr_t addr)
{
#if SOC_NON_CACHEABLE_OFFSET
  return addr + SOC_NON_CACHEABLE_OFFSET;
#else
  return addr;
#endif
}

GdmaLinkItem* LinkItemFromHeadAddr(uintptr_t head_addr)
{
  return reinterpret_cast<GdmaLinkItem*>(CacheAddrToNonCache(head_addr));
}

#if SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE || SOC_PSRAM_DMA_CAPABLE
extern "C" esp_err_t esp_cache_msync(void* addr, size_t size, int flags);

constexpr int CACHE_SYNC_FLAG_UNALIGNED = (1 << 1);
constexpr int CACHE_SYNC_FLAG_DIR_C2M = (1 << 2);
constexpr int CACHE_SYNC_FLAG_DIR_M2C = (1 << 3);

bool CacheSyncDmaBuffer(const void* addr, size_t size, bool cache_to_mem)
{
  if ((addr == nullptr) || (size == 0U))
  {
    return true;
  }

#if SOC_PSRAM_DMA_CAPABLE && !SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE
  if (!esp_ptr_external_ram(addr))
  {
    return true;
  }
#endif

  int flags = cache_to_mem ? CACHE_SYNC_FLAG_DIR_C2M : CACHE_SYNC_FLAG_DIR_M2C;
  flags |= CACHE_SYNC_FLAG_UNALIGNED;

  const esp_err_t ret = esp_cache_msync(const_cast<void*>(addr), size, flags);
  // Non-cacheable regions can return ESP_ERR_INVALID_ARG; treat as no-op success.
  return (ret == ESP_OK) || (ret == ESP_ERR_INVALID_ARG);
}
#endif
}  // namespace

namespace LibXR
{

bool IRAM_ATTR ESP32UART::DmaTxEofCallback(gdma_channel_handle_t, gdma_event_data_t*,
                                           void* user_data)
{
  auto* uart = static_cast<ESP32UART*>(user_data);
  if (uart != nullptr)
  {
    uart->OnTxTransferDone(true, ErrorCode::OK);
  }
  return false;
}

bool IRAM_ATTR ESP32UART::DmaTxDescrErrCallback(gdma_channel_handle_t, gdma_event_data_t*,
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

bool IRAM_ATTR ESP32UART::DmaRxDescrErrCallback(gdma_channel_handle_t, gdma_event_data_t*,
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
    return ErrorCode::INIT_ERR;
  }

  if (gdma_connect(tx_dma_channel_, GDMA_MAKE_TRIGGER(GDMA_TRIG_PERIPH_UHCI, 0)) !=
      ESP_OK)
  {
    return ErrorCode::INIT_ERR;
  }

  gdma_transfer_config_t transfer_cfg = {
      .max_data_burst_size = 0,
      .access_ext_mem = true,
  };
  if (gdma_config_transfer(tx_dma_channel_, &transfer_cfg) != ESP_OK)
  {
    return ErrorCode::INIT_ERR;
  }

  size_t tx_int_alignment = 1;
  size_t tx_ext_alignment = 1;
  if (gdma_get_alignment_constraints(tx_dma_channel_, &tx_int_alignment,
                                     &tx_ext_alignment) != ESP_OK)
  {
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
    return ErrorCode::INIT_ERR;
  }

  gdma_link_list_config_t tx_link_cfg = {
      .num_items = 1,
      .item_alignment = 4,
      .flags = {},
  };
  tx_dma_buffer_addr_[0] = tx_active_buffer_;
  tx_dma_buffer_addr_[1] = tx_pending_buffer_;

  for (int i = 0; i < 2; ++i)
  {
    if (gdma_new_link_list(&tx_link_cfg, &tx_dma_links_[i]) != ESP_OK)
    {
      return ErrorCode::INIT_ERR;
    }

    gdma_buffer_mount_config_t tx_mount = {
        .buffer = tx_dma_buffer_addr_[i],
        .buffer_alignment = tx_dma_alignment_,
        .length = 1,
        .flags =
            {
                .mark_eof = 1,
                .mark_final = 1,
                .bypass_buffer_align_check = 0,
            },
    };

    if (gdma_link_mount_buffers(tx_dma_links_[i], 0, &tx_mount, 1, nullptr) != ESP_OK)
    {
      return ErrorCode::INIT_ERR;
    }

    tx_dma_head_addr_[i] = gdma_link_get_head_addr(tx_dma_links_[i]);
    if (tx_dma_head_addr_[i] == 0U)
    {
      return ErrorCode::INIT_ERR;
    }
  }

  gdma_tx_event_callbacks_t tx_callbacks = {
      .on_trans_eof = DmaTxEofCallback,
      .on_descr_err = DmaTxDescrErrCallback,
  };
  if (gdma_register_tx_event_callbacks(tx_dma_channel_, &tx_callbacks, this) != ESP_OK)
  {
    return ErrorCode::INIT_ERR;
  }

  gdma_channel_alloc_config_t rx_cfg = {
      .sibling_chan = nullptr,
      .direction = GDMA_CHANNEL_DIRECTION_RX,
      .flags = {},
  };
  if (gdma_new_ahb_channel(&rx_cfg, &rx_dma_channel_) != ESP_OK)
  {
    return ErrorCode::INIT_ERR;
  }

  if (gdma_connect(rx_dma_channel_, GDMA_MAKE_TRIGGER(GDMA_TRIG_PERIPH_UHCI, 0)) !=
      ESP_OK)
  {
    return ErrorCode::INIT_ERR;
  }

  if (gdma_config_transfer(rx_dma_channel_, &transfer_cfg) != ESP_OK)
  {
    return ErrorCode::INIT_ERR;
  }

  size_t rx_int_alignment = 1;
  size_t rx_ext_alignment = 1;
  if (gdma_get_alignment_constraints(rx_dma_channel_, &rx_int_alignment,
                                     &rx_ext_alignment) != ESP_OK)
  {
    return ErrorCode::INIT_ERR;
  }
  rx_dma_alignment_ = std::max<size_t>(1, std::max(rx_int_alignment, rx_ext_alignment));

  gdma_link_list_config_t rx_link_cfg = {
      .num_items = DMA_RX_NODE_COUNT,
      .item_alignment = 4,
      .flags = {},
  };
  if (gdma_new_link_list(&rx_link_cfg, &rx_dma_link_) != ESP_OK)
  {
    return ErrorCode::INIT_ERR;
  }

  // Keep one ring window reasonably large to lower ISR pressure at high baud.
  const size_t rx_chunk_target = std::min<size_t>(
      std::max<size_t>(32, rx_isr_buffer_size_ / DMA_RX_NODE_COUNT), 512);
  rx_dma_chunk_size_ = std::max<size_t>(AlignUp(rx_chunk_target, 4), 32);
  rx_dma_node_count_ = DMA_RX_NODE_COUNT;
  const size_t rx_storage_alignment = std::max<size_t>(4, rx_dma_alignment_);
  const size_t rx_storage_bytes =
      AlignUp(rx_dma_chunk_size_ * rx_dma_node_count_, rx_storage_alignment);

  rx_dma_storage_ = static_cast<uint8_t*>(
      heap_caps_aligned_alloc(rx_storage_alignment, rx_storage_bytes,
                              MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  if (rx_dma_storage_ == nullptr)
  {
    return ErrorCode::NO_MEM;
  }

  std::array<gdma_buffer_mount_config_t, DMA_RX_NODE_COUNT> rx_mount = {};
  for (uint32_t i = 0; i < DMA_RX_NODE_COUNT; ++i)
  {
    rx_mount[i] = gdma_buffer_mount_config_t{
        .buffer = rx_dma_storage_ + (static_cast<size_t>(i) * rx_dma_chunk_size_),
        .buffer_alignment = rx_dma_alignment_,
        .length = rx_dma_chunk_size_,
        .flags =
            {
                .mark_eof = 0,
                .mark_final = 0,
                .bypass_buffer_align_check = 0,
            },
    };
  }

  if (gdma_link_mount_buffers(rx_dma_link_, 0, rx_mount.data(), DMA_RX_NODE_COUNT,
                              nullptr) != ESP_OK)
  {
    return ErrorCode::INIT_ERR;
  }

  gdma_rx_event_callbacks_t rx_callbacks = {
      .on_recv_eof = nullptr,
      .on_descr_err = DmaRxDescrErrCallback,
      .on_recv_done = DmaRxDoneCallback,
  };
  if (gdma_register_rx_event_callbacks(rx_dma_channel_, &rx_callbacks, this) != ESP_OK)
  {
    return ErrorCode::INIT_ERR;
  }

  if (gdma_reset(rx_dma_channel_) != ESP_OK)
  {
    return ErrorCode::INIT_ERR;
  }

  if (gdma_start(rx_dma_channel_, gdma_link_get_head_addr(rx_dma_link_)) != ESP_OK)
  {
    return ErrorCode::INIT_ERR;
  }

  rx_dma_node_index_ = 0;
  dma_backend_enabled_ = true;
  return ErrorCode::OK;
}

bool IRAM_ATTR ESP32UART::StartDmaTx()
{
  if ((tx_dma_channel_ == nullptr) || !tx_active_valid_)
  {
    return false;
  }

  uint8_t* const active_buffer = tx_active_buffer_;
  const size_t active_len = tx_active_length_;
  if ((active_buffer == nullptr) || (active_len == 0) ||
      (active_len > DMA_MAX_BUFFER_SIZE_PER_LINK_ITEM))
  {
    return false;
  }

  int link_index = -1;
  if (active_buffer == tx_dma_buffer_addr_[0])
  {
    link_index = 0;
  }
  else if (active_buffer == tx_dma_buffer_addr_[1])
  {
    link_index = 1;
  }
  else
  {
    return false;
  }

  if ((tx_dma_links_[link_index] == nullptr) || (tx_dma_head_addr_[link_index] == 0U))
  {
    return false;
  }

  auto* desc = LinkItemFromHeadAddr(tx_dma_head_addr_[link_index]);
  if (desc == nullptr)
  {
    return false;
  }

  // Keep descriptor list pre-mounted and only patch the dynamic transfer length in-place.
  desc->buffer = active_buffer;
  desc->dw0.size = static_cast<uint32_t>(active_len);
  desc->dw0.length = static_cast<uint32_t>(active_len);
  desc->dw0.err_eof = 0U;
  desc->dw0.suc_eof = 1U;
  desc->dw0.owner = GDMA_OWNER_DMA;
  desc->next = nullptr;
  std::atomic_thread_fence(std::memory_order_release);

#if SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE || SOC_PSRAM_DMA_CAPABLE
  if (!CacheSyncDmaBuffer(active_buffer, active_len, true))
  {
    return false;
  }
#endif

  return gdma_start(tx_dma_channel_, tx_dma_head_addr_[link_index]) == ESP_OK;
}

void IRAM_ATTR ESP32UART::PushDmaRxData(size_t recv_size, bool in_isr)
{
  if ((rx_dma_storage_ == nullptr) || (rx_dma_chunk_size_ == 0) ||
      (rx_dma_node_count_ == 0))
  {
    return;
  }

  const size_t max_window = rx_dma_chunk_size_ * rx_dma_node_count_;
  size_t remaining = std::min(recv_size, max_window);

  while (remaining > 0)
  {
    const size_t offset = static_cast<size_t>(rx_dma_node_index_) * rx_dma_chunk_size_;
    const size_t chunk = std::min(remaining, rx_dma_chunk_size_);
    auto* chunk_ptr = rx_dma_storage_ + offset;

#if SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE || SOC_PSRAM_DMA_CAPABLE
    if (!CacheSyncDmaBuffer(chunk_ptr, chunk, false))
    {
      HandleDmaRxError();
      return;
    }
#endif
    PushRxBytes(chunk_ptr, chunk, in_isr);
    remaining -= chunk;
    rx_dma_node_index_ = (rx_dma_node_index_ + 1U) % rx_dma_node_count_;
  }
}

void IRAM_ATTR ESP32UART::HandleDmaRxDone(gdma_event_data_t* event_data)
{
  if ((rx_dma_storage_ == nullptr) || (rx_dma_chunk_size_ == 0) ||
      (rx_dma_node_count_ == 0))
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
    const size_t eof_size = gdma_link_count_buffer_size_till_eof(
        rx_dma_link_, static_cast<int>(rx_dma_node_index_));
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
