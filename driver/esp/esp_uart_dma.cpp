#include "esp_uart.hpp"

#if SOC_GDMA_SUPPORTED && SOC_UHCI_SUPPORTED

#include <algorithm>
#include <array>

#include "esp_attr.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_private/periph_ctrl.h"
#include "hal/gdma_hal_ahb.h"
#include "hal/gdma_ll.h"
#include "hal/uhci_ll.h"
#include "soc/ext_mem_defs.h"
#include "soc/gdma_periph.h"

namespace
{
// RX uses a circular DMA descriptor ring, similar to STM/CH circular RX DMA
// behavior (continuous receive + software consumer index).
// RX 使用循环 DMA 描述符环，语义接近 STM/CH 的循环 RX DMA：持续接收，
// 软件侧维护消费索引。
constexpr uint32_t DMA_RX_NODE_COUNT = 8;

// Current ESP GDMA link items cannot describe more than 4095 bytes in one node.
// 当前 ESP GDMA link item 单节点最多只能描述 4095 字节。
constexpr size_t DMA_MAX_BUFFER_SIZE_PER_LINK_ITEM = 4095U;

// Minimal local view of the GDMA link descriptor layout used for in-place patching.
// 为就地修改描述符长度而保留的 GDMA link descriptor 最小本地视图。
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

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 4)
constexpr gdma_final_node_link_type_t DMA_FINAL_LINK_TO_DEFAULT =
    GDMA_FINAL_LINK_TO_DEFAULT;
constexpr gdma_final_node_link_type_t DMA_FINAL_LINK_TO_NULL = GDMA_FINAL_LINK_TO_NULL;
#else
constexpr uint32_t DMA_FINAL_LINK_TO_DEFAULT = 0U;
constexpr uint32_t DMA_FINAL_LINK_TO_NULL = 1U;
#endif

// Helper used for DMA storage and node-size alignment calculations.
// 用于 DMA storage 和 node 大小对齐计算的辅助函数。
size_t AlignUp(size_t value, size_t align)
{
  if (align <= 1)
  {
    return value;
  }
  return ((value + align - 1) / align) * align;
}

// Convert the cached address returned by ESP-IDF into the non-cache alias used
// by the GDMA descriptors when the target SoC exposes one.
// 当目标 SoC 提供 non-cache alias 时，把 ESP-IDF 返回的 cache 地址转换成
// GDMA 描述符使用的 non-cache 地址。
uintptr_t CacheAddrToNonCache(uintptr_t addr)
{
#if SOC_NON_CACHEABLE_OFFSET
  return addr + SOC_NON_CACHEABLE_OFFSET;
#else
  return addr;
#endif
}

// Recover the first link item from a GDMA list head address.
// 从 GDMA list head 地址恢复首个 link item。
GdmaLinkItem* LinkItemFromHeadAddr(uintptr_t head_addr)
{
  return reinterpret_cast<GdmaLinkItem*>(CacheAddrToNonCache(head_addr));
}

#if SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE || SOC_PSRAM_DMA_CAPABLE
extern "C" esp_err_t esp_cache_msync(void* addr, size_t size, int flags);

constexpr int CACHE_SYNC_FLAG_UNALIGNED = (1 << 1);
constexpr int CACHE_SYNC_FLAG_DIR_C2M = (1 << 2);
constexpr int CACHE_SYNC_FLAG_DIR_M2C = (1 << 3);

// Synchronize one DMA window when the active memory region is cacheable.
// 当当前内存区域可缓存时，同步一个 DMA 窗口。
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

void IRAM_ATTR ESP32UART::DmaTxIsrEntry(void* arg)
{
  auto* uart = static_cast<ESP32UART*>(arg);
  if (uart == nullptr)
  {
    return;
  }
  UartHardwareGate::OwnerContext hardware_context;
  if (!uart->hardware_gate_.TryEnterIrq(hardware_context))
  {
    uart->DeferDmaInterrupt(true);
    return;
  }

  uart->ServiceDmaTxStatus(true, hardware_context);
#if defined(GDMA_LL_AHB_TX_RX_SHARE_INTERRUPT) && GDMA_LL_AHB_TX_RX_SHARE_INTERRUPT
  uart->ServiceDmaRxStatus(true);
#endif
  uart->DispatchHardwareActions(uart->hardware_gate_.LeaveIrq(hardware_context), true);
}

void IRAM_ATTR ESP32UART::DmaRxIsrEntry(void* arg)
{
  auto* uart = static_cast<ESP32UART*>(arg);
  if (uart == nullptr)
  {
    return;
  }
  UartHardwareGate::OwnerContext hardware_context;
  if (!uart->hardware_gate_.TryEnterIrq(hardware_context))
  {
    uart->DeferDmaInterrupt(true);
    return;
  }

  uart->ServiceDmaRxStatus(true);
  uart->DispatchHardwareActions(uart->hardware_gate_.LeaveIrq(hardware_context), true);
}

void IRAM_ATTR ESP32UART::MaskDmaInterrupts(bool in_isr)
{
  if (tx_gdma_intr_handle_ != nullptr)
  {
    DEV_ASSERT_FROM_CALLBACK(esp_intr_disable(tx_gdma_intr_handle_) == ESP_OK, in_isr);
  }
  if (rx_gdma_intr_handle_ != nullptr)
  {
    DEV_ASSERT_FROM_CALLBACK(esp_intr_disable(rx_gdma_intr_handle_) == ESP_OK, in_isr);
  }
  std::atomic_thread_fence(std::memory_order_seq_cst);
}

void IRAM_ATTR ESP32UART::UnmaskDmaInterrupts(bool in_isr)
{
  std::atomic_thread_fence(std::memory_order_seq_cst);
  if (tx_gdma_intr_handle_ != nullptr)
  {
    DEV_ASSERT_FROM_CALLBACK(esp_intr_enable(tx_gdma_intr_handle_) == ESP_OK, in_isr);
  }
  if (rx_gdma_intr_handle_ != nullptr)
  {
    DEV_ASSERT_FROM_CALLBACK(esp_intr_enable(rx_gdma_intr_handle_) == ESP_OK, in_isr);
  }
}

void IRAM_ATTR ESP32UART::DeferDmaInterrupt(bool in_isr)
{
  // A same-core level IRQ can otherwise immediately retrigger and starve the task that
  // owns TX_START. Publish only after the complete interrupt domain is masked.
  MaskDmaInterrupts(in_isr);
  hardware_gate_.MarkIrqDeferred();

  if (hardware_gate_.ConfigRequested())
  {
    tx_dma_model_.ResumeConfig(in_isr);
  }
  ServiceDeferredDmaInterrupts(in_isr);
}

void IRAM_ATTR ESP32UART::ServiceDmaTxStatus(
    bool in_isr, UartHardwareGate::OwnerContext& hardware_context)
{
  const uint32_t status = gdma_hal_read_intr_status(&tx_gdma_hal_, tx_gdma_channel_id_,
                                                    GDMA_CHANNEL_DIRECTION_TX, false);
  gdma_hal_clear_intr(&tx_gdma_hal_, tx_gdma_channel_id_, GDMA_CHANNEL_DIRECTION_TX,
                      status);

  if ((status & GDMA_LL_EVENT_TX_DESC_ERROR) != 0U)
  {
    HandleDmaTxError(in_isr, hardware_context);
  }
  else if ((status & GDMA_LL_EVENT_TX_EOF) != 0U)
  {
    tx_dma_model_.OnTransferDone(in_isr, hardware_context);
  }
}

void IRAM_ATTR ESP32UART::ServiceDmaRxStatus(bool in_isr)
{
  const uint32_t status = gdma_hal_read_intr_status(&rx_gdma_hal_, rx_gdma_channel_id_,
                                                    GDMA_CHANNEL_DIRECTION_RX, true);
  gdma_hal_clear_intr(&rx_gdma_hal_, rx_gdma_channel_id_, GDMA_CHANNEL_DIRECTION_RX,
                      status);

  if ((status & (GDMA_LL_EVENT_RX_DESC_ERROR | GDMA_LL_EVENT_RX_ERR_EOF)) != 0U)
  {
    HandleDmaRxError(in_isr);
  }
  else if ((status & GDMA_LL_EVENT_RX_DONE) != 0U)
  {
    DrainCompletedDmaRxDescriptors(in_isr);
  }
}

void IRAM_ATTR ESP32UART::ServiceDeferredDmaInterrupts(bool in_isr)
{
  UartHardwareGate::OwnerContext hardware_context;
  if (!hardware_gate_.TryEnterDeferredIrq(hardware_context))
  {
    return;
  }

  // Another losing vector may have raced an earlier restore, so re-mask before the
  // authoritative complete-domain scan required by UartHardwareGate.
  MaskDmaInterrupts(in_isr);
  ServiceDmaTxStatus(in_isr, hardware_context);
  ServiceDmaRxStatus(in_isr);
  UnmaskDmaInterrupts(in_isr);
  DispatchHardwareActions(hardware_gate_.LeaveIrq(hardware_context), in_isr);
}

// DMA backend bring-up does three things:
// 1. Bind UHCI to the selected UART.
// 2. Prepare two TX descriptor lists, one per double-buffer half.
// 3. Prepare one circular RX descriptor ring.
// DMA 后端初始化做三件事：
// 1. 把 UHCI 绑定到选定 UART。
// 2. 为双缓冲两半各准备一条 TX 描述符链。
// 3. 准备一条循环 RX 描述符环。
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
      .flags = {.reserve_sibling = 1, .isr_cache_safe = 0},
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
  const size_t tx_dma_alignment =
      std::max<size_t>(1, std::max(tx_int_alignment, tx_ext_alignment));

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
  gdma_link_list_handle_t tx_dma_links[2] = {nullptr, nullptr};

  for (int i = 0; i < 2; ++i)
  {
    if (gdma_new_link_list(&tx_link_cfg, &tx_dma_links[i]) != ESP_OK)
    {
      return ErrorCode::INIT_ERR;
    }

    gdma_buffer_mount_config_t tx_mount = {
        .buffer = tx_dma_model_.Buffer(i),
        .buffer_alignment = tx_dma_alignment,
        .length = 1,
        .flags =
            {
                .mark_eof = 1,
                .mark_final = DMA_FINAL_LINK_TO_NULL,
                .bypass_buffer_align_check = 0,
            },
    };

    if (gdma_link_mount_buffers(tx_dma_links[i], 0, &tx_mount, 1, nullptr) != ESP_OK)
    {
      return ErrorCode::INIT_ERR;
    }

    tx_dma_head_addr_[i] = gdma_link_get_head_addr(tx_dma_links[i]);
    if (tx_dma_head_addr_[i] == 0U)
    {
      return ErrorCode::INIT_ERR;
    }
  }

  if (gdma_get_group_channel_id(tx_dma_channel_, &tx_gdma_group_id_,
                                &tx_gdma_channel_id_) != ESP_OK)
  {
    return ErrorCode::INIT_ERR;
  }
  const gdma_hal_config_t tx_hal_config = {.group_id = tx_gdma_group_id_};
  gdma_ahb_hal_init(&tx_gdma_hal_, &tx_hal_config);

  const int tx_irq_source =
      gdma_periph_signals.groups[tx_gdma_group_id_].pairs[tx_gdma_channel_id_].tx_irq_id;
#if defined(GDMA_LL_AHB_TX_RX_SHARE_INTERRUPT) && GDMA_LL_AHB_TX_RX_SHARE_INTERRUPT
  // One unfiltered handler owns the pair's shared source. This avoids a restore window
  // where the source is routed but only the non-matching direction handler is enabled.
  const uint32_t tx_status_register = 0U;
  const uint32_t tx_status_mask = 0U;
#else
  const uint32_t tx_status_register = gdma_hal_get_intr_status_reg(
      &tx_gdma_hal_, tx_gdma_channel_id_, GDMA_CHANNEL_DIRECTION_TX);
  const uint32_t tx_status_mask = GDMA_LL_TX_EVENT_MASK;
#endif
  const int tx_irq_flags =
      ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_INTRDISABLED | ESP_INTR_FLAG_SHARED;
  if (esp_intr_alloc_intrstatus(tx_irq_source, tx_irq_flags, tx_status_register,
                                tx_status_mask, DmaTxIsrEntry, this,
                                &tx_gdma_intr_handle_) != ESP_OK)
  {
    return ErrorCode::INIT_ERR;
  }
  gdma_hal_clear_intr(&tx_gdma_hal_, tx_gdma_channel_id_, GDMA_CHANNEL_DIRECTION_TX,
                      UINT32_MAX);
  gdma_hal_enable_intr(&tx_gdma_hal_, tx_gdma_channel_id_, GDMA_CHANNEL_DIRECTION_TX,
                       GDMA_LL_EVENT_TX_EOF | GDMA_LL_EVENT_TX_DESC_ERROR, true);
  if (esp_intr_enable(tx_gdma_intr_handle_) != ESP_OK)
  {
    return ErrorCode::INIT_ERR;
  }

  gdma_channel_alloc_config_t rx_cfg = {
      .sibling_chan = tx_dma_channel_,
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

  gdma_strategy_config_t rx_strategy = {
      .owner_check = true,
      .auto_update_desc = false,
      .eof_till_data_popped = false,
  };
  if (gdma_apply_strategy(rx_dma_channel_, &rx_strategy) != ESP_OK)
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
  const size_t rx_dma_alignment =
      std::max<size_t>(1, std::max(rx_int_alignment, rx_ext_alignment));
  rx_dma_buffer_alignment_ = rx_dma_alignment;

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
  // 保持单个环窗口适度偏大，以降低高波特率下的 ISR 压力。
  const size_t rx_chunk_target = std::min<size_t>(
      std::max<size_t>(32, rx_isr_buffer_size_ / DMA_RX_NODE_COUNT), 512);
  const size_t rx_storage_alignment = std::max<size_t>(4, rx_dma_buffer_alignment_);
  rx_dma_chunk_size_ =
      std::max<size_t>(AlignUp(rx_chunk_target, rx_storage_alignment), 32);
  const size_t rx_storage_bytes =
      AlignUp(rx_dma_chunk_size_ * DMA_RX_NODE_COUNT, rx_storage_alignment);

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
        .buffer_alignment = rx_dma_buffer_alignment_,
        .length = rx_dma_chunk_size_,
        .flags =
            {
                .mark_eof = 0,
                .mark_final = DMA_FINAL_LINK_TO_DEFAULT,
                .bypass_buffer_align_check = 0,
            },
    };
  }

  if (gdma_link_mount_buffers(rx_dma_link_, 0, rx_mount.data(), DMA_RX_NODE_COUNT,
                              nullptr) != ESP_OK)
  {
    return ErrorCode::INIT_ERR;
  }

  if (gdma_get_group_channel_id(rx_dma_channel_, &rx_gdma_group_id_,
                                &rx_gdma_channel_id_) != ESP_OK)
  {
    return ErrorCode::INIT_ERR;
  }
  const gdma_hal_config_t rx_hal_config = {.group_id = rx_gdma_group_id_};
  gdma_ahb_hal_init(&rx_gdma_hal_, &rx_hal_config);

  const int rx_irq_source =
      gdma_periph_signals.groups[rx_gdma_group_id_].pairs[rx_gdma_channel_id_].rx_irq_id;
#if defined(GDMA_LL_AHB_TX_RX_SHARE_INTERRUPT) && GDMA_LL_AHB_TX_RX_SHARE_INTERRUPT
  if (rx_irq_source != tx_irq_source)
  {
    return ErrorCode::INIT_ERR;
  }
#else
  const uint32_t rx_status_register = gdma_hal_get_intr_status_reg(
      &rx_gdma_hal_, rx_gdma_channel_id_, GDMA_CHANNEL_DIRECTION_RX);
  const int rx_irq_flags =
      ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_INTRDISABLED | ESP_INTR_FLAG_SHARED;
  // Bind both directions to one core, priority and CPU interrupt line. This prevents an
  // unpinned constructor task from allocating them on different cores after preemption.
  if (esp_intr_alloc_intrstatus_bind(
          rx_irq_source, rx_irq_flags, rx_status_register, GDMA_LL_RX_EVENT_MASK,
          DmaRxIsrEntry, this, tx_gdma_intr_handle_, &rx_gdma_intr_handle_) != ESP_OK)
  {
    return ErrorCode::INIT_ERR;
  }
  DEV_ASSERT(esp_intr_get_cpu(tx_gdma_intr_handle_) ==
             esp_intr_get_cpu(rx_gdma_intr_handle_));
  DEV_ASSERT(esp_intr_get_intno(tx_gdma_intr_handle_) ==
             esp_intr_get_intno(rx_gdma_intr_handle_));
#endif
  gdma_hal_clear_intr(&rx_gdma_hal_, rx_gdma_channel_id_, GDMA_CHANNEL_DIRECTION_RX,
                      UINT32_MAX);
  gdma_hal_enable_intr(
      &rx_gdma_hal_, rx_gdma_channel_id_, GDMA_CHANNEL_DIRECTION_RX,
      GDMA_LL_EVENT_RX_DONE | GDMA_LL_EVENT_RX_DESC_ERROR | GDMA_LL_EVENT_RX_ERR_EOF,
      true);
#if !(defined(GDMA_LL_AHB_TX_RX_SHARE_INTERRUPT) && GDMA_LL_AHB_TX_RX_SHARE_INTERRUPT)
  if (esp_intr_enable(rx_gdma_intr_handle_) != ESP_OK)
  {
    return ErrorCode::INIT_ERR;
  }
#endif

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

bool IRAM_ATTR ESP32UART::RemountAndRestartRxDma()
{
  if ((rx_dma_channel_ == nullptr) || (rx_dma_link_ == nullptr) ||
      (rx_dma_storage_ == nullptr) || (rx_dma_chunk_size_ == 0U))
  {
    return false;
  }

  std::array<gdma_buffer_mount_config_t, DMA_RX_NODE_COUNT> mount_configs = {};
  for (uint32_t i = 0; i < DMA_RX_NODE_COUNT; ++i)
  {
    mount_configs[i] = gdma_buffer_mount_config_t{
        .buffer = rx_dma_storage_ + (static_cast<size_t>(i) * rx_dma_chunk_size_),
        .buffer_alignment = rx_dma_buffer_alignment_,
        .length = rx_dma_chunk_size_,
        .flags =
            {
                .mark_eof = 0,
                .mark_final = DMA_FINAL_LINK_TO_DEFAULT,
                .bypass_buffer_align_check = 0,
            },
    };
  }

  if (gdma_link_mount_buffers(rx_dma_link_, 0, mount_configs.data(), DMA_RX_NODE_COUNT,
                              nullptr) != ESP_OK)
  {
    return false;
  }

  rx_dma_node_index_ = 0U;
  return gdma_start(rx_dma_channel_, gdma_link_get_head_addr(rx_dma_link_)) == ESP_OK;
}

// TX DMA start only patches the dynamic fields of the pre-mounted descriptor
// list, so the hot path avoids rebuilding descriptors for every request.
// TX DMA 启动时只修改预挂载描述符链的动态字段，避免每次请求都重建描述符。
UartDmaTxStartResult IRAM_ATTR
ESP32UART::StartDmaTx(uint8_t* data, size_t size, int block,
                      UartHardwareGate::OwnerContext* hardware_context)
{
  if ((tx_dma_channel_ == nullptr) || (data == nullptr) || (size == 0U) ||
      (size > DMA_MAX_BUFFER_SIZE_PER_LINK_ITEM) || ((block != 0) && (block != 1)) ||
      (tx_dma_head_addr_[block] == 0U))
  {
    return UartDmaTxStartResult::FAILED;
  }

  const bool nested_start = hardware_context != nullptr;
  if (nested_start && !hardware_gate_.TryEnterNestedTxStart(*hardware_context))
  {
    return UartDmaTxStartResult::RETRY;
  }
  if (!nested_start && !hardware_gate_.TryEnterTxStart())
  {
    return UartDmaTxStartResult::RETRY;
  }

  const auto finish_start = [&](UartDmaTxStartResult result)
  {
    if (nested_start)
    {
      hardware_gate_.LeaveNestedTxStart(*hardware_context);
    }
    else
    {
      DispatchHardwareActions(hardware_gate_.LeaveTxStart(),
                              xPortInIsrContext() != pdFALSE);
    }
    return result;
  };

  auto* desc = LinkItemFromHeadAddr(tx_dma_head_addr_[block]);
  if (desc == nullptr)
  {
    return finish_start(UartDmaTxStartResult::FAILED);
  }

  desc->buffer = data;
  desc->dw0.size = static_cast<uint32_t>(size);
  desc->dw0.length = static_cast<uint32_t>(size);
  desc->dw0.err_eof = 0U;
  desc->dw0.suc_eof = 1U;
  desc->dw0.owner = GDMA_OWNER_DMA;
  desc->next = nullptr;
  std::atomic_thread_fence(std::memory_order_release);

#if SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE || SOC_PSRAM_DMA_CAPABLE
  if (!CacheSyncDmaBuffer(data, size, true))
  {
    return finish_start(UartDmaTxStartResult::FAILED);
  }
#endif

  return finish_start((gdma_start(tx_dma_channel_, tx_dma_head_addr_[block]) == ESP_OK)
                          ? UartDmaTxStartResult::STARTED
                          : UartDmaTxStartResult::FAILED);
}

// RX_DONE is a level reminder, not an event count. Multiple descriptor completions can
// coalesce into one status bit while the IRQ is masked, so drain consecutive CPU-owned
// descriptors and return each one to DMA after copying its payload. One pass is bounded
// to a full ring; a descriptor completed again during this pass raises another IRQ.
void IRAM_ATTR ESP32UART::DrainCompletedDmaRxDescriptors(bool in_isr)
{
  if ((rx_dma_link_ == nullptr) || (rx_dma_chunk_size_ == 0U))
  {
    return;
  }

  for (uint32_t consumed = 0U; consumed < DMA_RX_NODE_COUNT; ++consumed)
  {
    const int item_index = static_cast<int>(rx_dma_node_index_);
    gdma_lli_owner_t owner = GDMA_LLI_OWNER_DMA;
    if (gdma_link_get_owner(rx_dma_link_, item_index, &owner) != ESP_OK)
    {
      HandleDmaRxError(in_isr);
      return;
    }
    if (owner != GDMA_LLI_OWNER_CPU)
    {
      return;
    }
    std::atomic_thread_fence(std::memory_order_acquire);

    auto* buffer = static_cast<uint8_t*>(gdma_link_get_buffer(rx_dma_link_, item_index));
    const size_t length = gdma_link_get_length(rx_dma_link_, item_index);
    if ((buffer == nullptr) || (length > rx_dma_chunk_size_))
    {
      HandleDmaRxError(in_isr);
      return;
    }

#if SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE || SOC_PSRAM_DMA_CAPABLE
    if (!CacheSyncDmaBuffer(buffer, length, false))
    {
      HandleDmaRxError(in_isr);
      return;
    }
#endif

    if (length > 0U)
    {
      PushRxBytes(buffer, length, in_isr);
    }

    std::atomic_thread_fence(std::memory_order_release);
    if (gdma_link_set_owner(rx_dma_link_, item_index, GDMA_LLI_OWNER_DMA) != ESP_OK)
    {
      HandleDmaRxError(in_isr);
      return;
    }
    rx_dma_node_index_ = (rx_dma_node_index_ + 1U) % DMA_RX_NODE_COUNT;
  }
}

// RX DMA recovery restarts the circular ring from node zero.
// RX DMA 恢复会从节点零重新启动整个环。
void IRAM_ATTR ESP32UART::HandleDmaRxError(bool in_isr)
{
  if ((rx_dma_channel_ == nullptr) || (rx_dma_link_ == nullptr))
  {
    return;
  }

  DEV_ASSERT_FROM_CALLBACK(gdma_stop(rx_dma_channel_) == ESP_OK, in_isr);
  DEV_ASSERT_FROM_CALLBACK(gdma_reset(rx_dma_channel_) == ESP_OK, in_isr);
  gdma_hal_clear_intr(&rx_gdma_hal_, rx_gdma_channel_id_, GDMA_CHANNEL_DIRECTION_RX,
                      UINT32_MAX);
  (void)gdma_hal_read_intr_status(&rx_gdma_hal_, rx_gdma_channel_id_,
                                  GDMA_CHANNEL_DIRECTION_RX, true);
  DEV_ASSERT_FROM_CALLBACK(RemountAndRestartRxDma(), in_isr);
}

// TX DMA recovery aborts the current hardware transfer before the common terminal
// path advances a ready pending request.
// TX DMA 恢复先中止当前硬件传输，再由公共终止路径推进 ready pending 请求。
void IRAM_ATTR
ESP32UART::HandleDmaTxError(bool in_isr, UartHardwareGate::OwnerContext& hardware_context)
{
  if (tx_dma_channel_ != nullptr)
  {
    DEV_ASSERT_FROM_CALLBACK(gdma_stop(tx_dma_channel_) == ESP_OK, in_isr);
    DEV_ASSERT_FROM_CALLBACK(gdma_reset(tx_dma_channel_) == ESP_OK, in_isr);
    gdma_hal_clear_intr(&tx_gdma_hal_, tx_gdma_channel_id_, GDMA_CHANNEL_DIRECTION_TX,
                        UINT32_MAX);
    (void)gdma_hal_read_intr_status(&tx_gdma_hal_, tx_gdma_channel_id_,
                                    GDMA_CHANNEL_DIRECTION_TX, true);
  }
  tx_dma_model_.OnTransferError(in_isr, hardware_context);
}

}  // namespace LibXR

#endif
