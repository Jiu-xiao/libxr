#include "esp_uart.hpp"

#if LIBXR_ESP_UART_HAS_AHB_GDMA

#include <algorithm>
#include <array>
#include <atomic>

#include "esp_attr.h"
#include "esp_cache.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_private/esp_cache_private.h"
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wliteral-suffix"
#endif
#include "esp_private/periph_ctrl.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include "hal/dma_types.h"
#include "hal/gdma_hal_ahb.h"
#include "hal/gdma_ll.h"
#include "hal/uhci_ll.h"
#include "soc/ext_mem_defs.h"
#include "soc/gdma_periph.h"

#if defined(SOC_RCC_IS_INDEPENDENT) && SOC_RCC_IS_INDEPENDENT
#define LIBXR_ESP_UHCI_RCC_ATOMIC()
#else
#define LIBXR_ESP_UHCI_RCC_ATOMIC() PERIPH_RCC_ATOMIC()
#endif

namespace
{
// RX uses a circular DMA descriptor ring, similar to STM/CH circular RX DMA
// behavior (continuous receive + software consumer index).
// RX 使用循环 DMA 描述符环，语义接近 STM/CH 的循环 RX DMA：持续接收，
// 软件侧维护消费索引。
constexpr uint32_t DMA_RX_NODE_COUNT = 8;

// Current ESP GDMA link items cannot describe more than 4095 bytes in one node.
// 当前 ESP GDMA link item 单节点最多只能描述 4095 字节。
constexpr size_t DMA_MAX_BUFFER_SIZE_PER_LINK_ITEM = DMA_DESCRIPTOR_BUFFER_MAX_SIZE;

// UHCI0 is chip-wide and retained for the process lifetime. A later DMA-UART
// instance must fail before accessing any UHCI register.
std::atomic<uint32_t> uhci0_lifetime_claimed{0U};

bool TryClaimUhci0ForLifetime()
{
  uint32_t expected = 0U;
  return uhci0_lifetime_claimed.compare_exchange_strong(
      expected, 1U, std::memory_order_acq_rel, std::memory_order_relaxed);
}

// Minimal local view of the GDMA link descriptor layout used for in-place patching.
// 为就地修改描述符长度而保留的 GDMA link descriptor 最小本地视图。
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
dma_descriptor_t* DescriptorFromHeadAddr(uintptr_t head_addr)
{
  return reinterpret_cast<dma_descriptor_t*>(CacheAddrToNonCache(head_addr));
}

// Synchronize one DMA window when the active memory region is cacheable.
// 当当前内存区域可缓存时，同步一个 DMA 窗口。
#if SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE
bool CacheSyncDmaBuffer(const void* addr, size_t size, bool cache_to_mem,
                        size_t cache_line_size)
{
  if ((addr == nullptr) || (size == 0U))
  {
    return true;
  }

  size_t sync_size = size;
  int flags = ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED;
  if (!cache_to_mem)
  {
    if ((cache_line_size == 0U) ||
        ((reinterpret_cast<uintptr_t>(addr) % cache_line_size) != 0U) ||
        (size > (SIZE_MAX - (cache_line_size - 1U))))
    {
      return false;
    }
    sync_size = AlignUp(size, cache_line_size);
    flags = ESP_CACHE_MSYNC_FLAG_DIR_M2C;
  }
  return esp_cache_msync(const_cast<void*>(addr), sync_size, flags) == ESP_OK;
}
#endif
}  // namespace

namespace LibXR
{

void IRAM_ATTR ESP32UartDma::DmaTxIsrEntry(void* arg)
{
  auto* uart = static_cast<ESP32UartDma*>(arg);
  if (uart == nullptr)
  {
    return;
  }
#if defined(GDMA_LL_AHB_TX_RX_SHARE_INTERRUPT) && GDMA_LL_AHB_TX_RX_SHARE_INTERRUPT
  bool pushed_any = false;
  (void)uart->dma_model_.InvokeIrq(
      [uart, &pushed_any]() noexcept
      { return uart->ServiceDmaTxStatus(true) | uart->ServiceDmaRxStatus(pushed_any); },
      true);
  if (pushed_any)
  {
    uart->read_port_->ProcessPendingReads(true);
  }
#else
  (void)uart->dma_model_.InvokeIrq(
      [uart]() noexcept { return uart->ServiceDmaTxStatus(true); }, true);
#endif
}

void IRAM_ATTR ESP32UartDma::DmaRxIsrEntry(void* arg)
{
  auto* uart = static_cast<ESP32UartDma*>(arg);
  if (uart == nullptr)
  {
    return;
  }
  bool pushed_any = false;
  (void)uart->dma_model_.InvokeIrq([uart, &pushed_any]() noexcept
                                   { return uart->ServiceDmaRxStatus(pushed_any); },
                                   true);
  if (pushed_any)
  {
    uart->read_port_->ProcessPendingReads(true);
  }
}

uint32_t IRAM_ATTR ESP32UartDma::ServiceDmaTxStatus(bool in_isr)
{
  const uint32_t status = gdma_hal_read_intr_status(&tx_gdma_hal_, tx_gdma_channel_id_,
                                                    GDMA_CHANNEL_DIRECTION_TX, false);
  gdma_hal_clear_intr(&tx_gdma_hal_, tx_gdma_channel_id_, GDMA_CHANNEL_DIRECTION_TX,
                      status);

  uint32_t events = 0U;
  using Model = UartDmaModel<ESP32UartDma, ExecutionPolicy>;
  if ((status & GDMA_LL_EVENT_TX_DESC_ERROR) != 0U)
  {
    events |= Model::EventMask(UartDmaEvent::ERROR);
  }
  else if ((status & GDMA_LL_EVENT_TX_EOF) != 0U)
  {
    events |= Model::EventMask(UartDmaEvent::COMPLETE);
  }
  (void)in_isr;
  return events;
}

uint32_t IRAM_ATTR ESP32UartDma::ServiceDmaRxStatus(bool& pushed_any)
{
  const uint32_t status = gdma_hal_read_intr_status(&rx_gdma_hal_, rx_gdma_channel_id_,
                                                    GDMA_CHANNEL_DIRECTION_RX, true);
  gdma_hal_clear_intr(&rx_gdma_hal_, rx_gdma_channel_id_, GDMA_CHANNEL_DIRECTION_RX,
                      status);

  using Model = UartDmaModel<ESP32UartDma, ExecutionPolicy>;
  bool error = (status & (GDMA_LL_EVENT_RX_DESC_ERROR | GDMA_LL_EVENT_RX_ERR_EOF)) != 0U;
  uint32_t events = error ? Model::EventMask(UartDmaEvent::ERROR) : 0U;
  if (!error && ((status & GDMA_LL_EVENT_RX_DONE) != 0U))
  {
    (void)dma_model_.ProcessRxInIrqSource(
        events, [this, &pushed_any, &error]()
        { error = !DrainCompletedDmaRxDescriptors(pushed_any); });
  }
  if (error)
  {
    // A separate RX source may observe the error before the TX EOF IRQ runs. Collect
    // the TX terminal fact in this same owner snapshot before recovery clears it.
    events |= ServiceDmaTxStatus(true);
    events |= Model::EventMask(UartDmaEvent::ERROR);
  }
  return events;
}

uint32_t IRAM_ATTR ESP32UartDma::ServiceDmaUartStatus(bool in_isr)
{
  using Model = UartDmaModel<ESP32UartDma, ExecutionPolicy>;
  const uint32_t status = uart_hal_get_intsts_mask(&uart_hal_) &
                          (DMA_UART_ERROR_INTR_MASK | UART_INTR_TX_DONE);
  const uint32_t error_status = status & DMA_UART_ERROR_INTR_MASK;
  if (error_status != 0U)
  {
    uart_hal_clr_intsts_mask(&uart_hal_, error_status);
  }
  uint32_t events = ServiceDmaTxStatus(in_isr);
  if (error_status != 0U)
  {
    events |= Model::EventMask(UartDmaEvent::ERROR);
  }
  if (((status & UART_INTR_TX_DONE) != 0U) && config_waiting_tx_idle_ &&
      uart_hal_is_tx_idle(&uart_hal_))
  {
    DisarmConfigTxIdleInterrupt();
    events |= Model::EventMask(UartDmaEvent::STOP_DONE);
  }
  return events;
}

// DMA backend bring-up does three things:
// 1. Bind UHCI to the selected UART.
// 2. Prepare two TX descriptor lists, one per double-buffer half.
// 3. Prepare one circular RX descriptor ring.
// DMA 后端初始化做三件事：
// 1. 把 UHCI 绑定到选定 UART。
// 2. 为双缓冲两半各准备一条 TX 描述符链。
// 3. 准备一条循环 RX 描述符环。
ErrorCode ESP32UartDma::InitDmaBackend()
{
  if (!TryClaimUhci0ForLifetime())
  {
    return ErrorCode::INIT_ERR;
  }

  LIBXR_ESP_UHCI_RCC_ATOMIC()
  {
    uhci_ll_enable_bus_clock(0, true);
    uhci_ll_reset_register(0);
  }

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
  const size_t tx_dma_alignment = std::max<size_t>(1, tx_int_alignment);
  if ((tx_storage_.block_stride == 0U) ||
      ((tx_storage_.block_stride % tx_dma_alignment) != 0U) ||
      ((reinterpret_cast<uintptr_t>(dma_model_.Buffer(0)) % tx_dma_alignment) != 0U) ||
      ((reinterpret_cast<uintptr_t>(dma_model_.Buffer(1)) % tx_dma_alignment) != 0U))
  {
    return ErrorCode::INIT_ERR;
  }

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
        .buffer = dma_model_.Buffer(i),
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
  constexpr int DMA_IRQ_FLAGS = ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_INTRDISABLED;
  if (esp_intr_alloc(tx_irq_source, DMA_IRQ_FLAGS, DmaTxIsrEntry, this,
                     &tx_gdma_intr_handle_) != ESP_OK)
  {
    return ErrorCode::INIT_ERR;
  }
  gdma_hal_clear_intr(&tx_gdma_hal_, tx_gdma_channel_id_, GDMA_CHANNEL_DIRECTION_TX,
                      UINT32_MAX);
  gdma_hal_enable_intr(&tx_gdma_hal_, tx_gdma_channel_id_, GDMA_CHANNEL_DIRECTION_TX,
                       GDMA_LL_EVENT_TX_EOF | GDMA_LL_EVENT_TX_DESC_ERROR, true);
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
  const size_t rx_dma_alignment = std::max<size_t>(1, rx_int_alignment);
  rx_dma_buffer_alignment_ = rx_dma_alignment;
  if ((esp_cache_get_alignment(MALLOC_CAP_INTERNAL, &rx_cache_line_size_) != ESP_OK) ||
      (rx_cache_line_size_ == 0U))
  {
    return ErrorCode::INIT_ERR;
  }

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
      std::max<size_t>(32, read_port_->queue_data_->MaxSize() / DMA_RX_NODE_COUNT), 512);
  const size_t rx_storage_alignment =
      std::max<size_t>(4, std::max(rx_dma_buffer_alignment_, rx_cache_line_size_));
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
  rx_dma_head_addr_ = gdma_link_get_head_addr(rx_dma_link_);
  rx_dma_descriptors_ = DescriptorFromHeadAddr(rx_dma_head_addr_);
  if ((rx_dma_head_addr_ == 0U) || (rx_dma_descriptors_ == nullptr))
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
  rx_gdma_intr_handle_ = tx_gdma_intr_handle_;
#else
  if (esp_intr_alloc(rx_irq_source, DMA_IRQ_FLAGS, DmaRxIsrEntry, this,
                     &rx_gdma_intr_handle_) != ESP_OK)
  {
    return ErrorCode::INIT_ERR;
  }
  DEV_ASSERT(esp_intr_get_cpu(tx_gdma_intr_handle_) ==
             esp_intr_get_cpu(rx_gdma_intr_handle_));
#endif
  gdma_hal_clear_intr(&rx_gdma_hal_, rx_gdma_channel_id_, GDMA_CHANNEL_DIRECTION_RX,
                      UINT32_MAX);
  gdma_hal_enable_intr(
      &rx_gdma_hal_, rx_gdma_channel_id_, GDMA_CHANNEL_DIRECTION_RX,
      GDMA_LL_EVENT_RX_DONE | GDMA_LL_EVENT_RX_DESC_ERROR | GDMA_LL_EVENT_RX_ERR_EOF,
      true);
  if (gdma_reset(rx_dma_channel_) != ESP_OK)
  {
    return ErrorCode::INIT_ERR;
  }

  if (!ResetAndRestartRxDma())
  {
    return ErrorCode::INIT_ERR;
  }

  return ErrorCode::OK;
}

bool IRAM_ATTR ESP32UartDma::ResetAndRestartRxDma()
{
  if ((rx_dma_channel_ == nullptr) || (rx_dma_descriptors_ == nullptr) ||
      (rx_dma_head_addr_ == 0U) || (rx_dma_storage_ == nullptr) ||
      (rx_dma_chunk_size_ == 0U) ||
      (rx_dma_chunk_size_ > DMA_MAX_BUFFER_SIZE_PER_LINK_ITEM))
  {
    return false;
  }

  auto* cached_descriptors = reinterpret_cast<dma_descriptor_t*>(rx_dma_head_addr_);
  for (uint32_t i = 0; i < DMA_RX_NODE_COUNT; ++i)
  {
    auto& descriptor = rx_dma_descriptors_[i];
    descriptor.dw0.owner = DMA_DESCRIPTOR_BUFFER_OWNER_CPU;
    descriptor.buffer = rx_dma_storage_ + (static_cast<size_t>(i) * rx_dma_chunk_size_);
    descriptor.dw0.size = static_cast<uint32_t>(rx_dma_chunk_size_);
    descriptor.dw0.length = static_cast<uint32_t>(rx_dma_chunk_size_);
    descriptor.dw0.err_eof = 0U;
    descriptor.dw0.suc_eof = 0U;
    descriptor.next = &cached_descriptors[(i + 1U) % DMA_RX_NODE_COUNT];
  }

  std::atomic_thread_fence(std::memory_order_release);
  for (uint32_t i = 0; i < DMA_RX_NODE_COUNT; ++i)
  {
    rx_dma_descriptors_[i].dw0.owner = DMA_DESCRIPTOR_BUFFER_OWNER_DMA;
  }
  std::atomic_thread_fence(std::memory_order_release);

  rx_dma_node_index_ = 0U;
  return gdma_start(rx_dma_channel_, rx_dma_head_addr_) == ESP_OK;
}

// TX DMA start only patches the dynamic fields of the pre-mounted descriptor
// list, so the hot path avoids rebuilding descriptors for every request.
// TX DMA 启动时只修改预挂载描述符链的动态字段，避免每次请求都重建描述符。
UartDmaTxStartResult IRAM_ATTR ESP32UartDma::StartDmaTx(uint8_t* data, size_t size,
                                                        int block, bool)
{
  if ((tx_dma_channel_ == nullptr) || (data == nullptr) || (size == 0U) ||
      (size > DMA_MAX_BUFFER_SIZE_PER_LINK_ITEM) || ((block != 0) && (block != 1)) ||
      (tx_dma_head_addr_[block] == 0U))
  {
    return UartDmaTxStartResult::FAILED;
  }

  auto* desc = DescriptorFromHeadAddr(tx_dma_head_addr_[block]);
  if (desc == nullptr)
  {
    return UartDmaTxStartResult::FAILED;
  }

  desc->dw0.owner = DMA_DESCRIPTOR_BUFFER_OWNER_CPU;
  desc->buffer = data;
  desc->dw0.size = static_cast<uint32_t>(size);
  desc->dw0.length = static_cast<uint32_t>(size);
  desc->dw0.err_eof = 0U;
  desc->dw0.suc_eof = 1U;
  desc->next = nullptr;

#if SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE
  if (!CacheSyncDmaBuffer(data, size, true, tx_storage_.cache_line_size))
  {
    return UartDmaTxStartResult::FAILED;
  }
#endif

  std::atomic_thread_fence(std::memory_order_release);
  desc->dw0.owner = DMA_DESCRIPTOR_BUFFER_OWNER_DMA;

  // Establish the UART TX_DONE generation boundary immediately before launch. CONFIG
  // must preserve any TX_DONE raw bit raised after this point because that bit can be
  // the only carrier that observes the UART FSM becoming idle.
  uart_hal_clr_intsts_mask(&uart_hal_, UART_INTR_TX_DONE);
  return (gdma_start(tx_dma_channel_, tx_dma_head_addr_[block]) == ESP_OK)
             ? UartDmaTxStartResult::STARTED
             : UartDmaTxStartResult::FAILED;
}

// RX_DONE is a level reminder, not an event count. Multiple descriptor completions can
// coalesce into one status bit while the IRQ is masked, so drain consecutive CPU-owned
// descriptors and return each one to DMA after copying its payload. One pass is bounded
// to a full ring; a descriptor completed again during this pass raises another IRQ.
bool IRAM_ATTR ESP32UartDma::DrainCompletedDmaRxDescriptors(bool& pushed_any)
{
  if ((rx_dma_descriptors_ == nullptr) || (rx_dma_chunk_size_ == 0U))
  {
    return false;
  }

  bool success = true;
  for (uint32_t consumed = 0U; consumed < DMA_RX_NODE_COUNT; ++consumed)
  {
    auto& descriptor = rx_dma_descriptors_[rx_dma_node_index_];
    if (descriptor.dw0.owner != DMA_DESCRIPTOR_BUFFER_OWNER_CPU)
    {
      break;
    }
    std::atomic_thread_fence(std::memory_order_acquire);

    auto* buffer = static_cast<uint8_t*>(descriptor.buffer);
    const size_t length = descriptor.dw0.length;
    if ((buffer == nullptr) || (length > rx_dma_chunk_size_))
    {
      success = false;
      break;
    }

#if SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE
    if (!CacheSyncDmaBuffer(buffer, length, false, rx_cache_line_size_))
    {
      success = false;
      break;
    }
#endif

    if (length > 0U)
    {
      pushed_any = PushRxBytes(buffer, length) || pushed_any;
    }

    descriptor.dw0.length = static_cast<uint32_t>(rx_dma_chunk_size_);
    descriptor.dw0.err_eof = 0U;
    descriptor.dw0.suc_eof = 0U;
    std::atomic_thread_fence(std::memory_order_release);
    descriptor.dw0.owner = DMA_DESCRIPTOR_BUFFER_OWNER_DMA;
    rx_dma_node_index_ = (rx_dma_node_index_ + 1U) % DMA_RX_NODE_COUNT;
  }
  return success;
}

UartDmaControlResult IRAM_ATTR ESP32UartDma::AdvanceRecovery(bool active_tx, bool in_isr)
{
  const UartOldTxTerminal terminal = StopAndResetDma(active_tx, in_isr);

  uart_hal_rxfifo_rst(&uart_hal_);
  uart_hal_clr_intsts_mask(&uart_hal_, DMA_UART_ERROR_INTR_MASK);
  return UartDmaControlResult::Completed(terminal);
}

UartDmaControlProgress IRAM_ATTR ESP32UartDma::CompleteRecovery(bool in_isr)
{
  REQUIRE_FROM_CALLBACK(ResetAndRestartRxDma(), in_isr);
  return UartDmaControlProgress::COMPLETED;
}

UartOldTxTerminal IRAM_ATTR ESP32UartDma::StopAndResetDma(bool active_tx, bool in_isr)
{
  using Model = UartDmaModel<ESP32UartDma, ExecutionPolicy>;
  REQUIRE_FROM_CALLBACK(tx_dma_channel_ != nullptr, in_isr);
  REQUIRE_FROM_CALLBACK(rx_dma_channel_ != nullptr, in_isr);

  UartOldTxTerminal terminal = UartOldTxTerminal::NONE;
  const auto classify_tx = [&terminal](uint32_t events)
  {
    if ((events & Model::EventMask(UartDmaEvent::ERROR)) != 0U)
    {
      terminal = UartOldTxTerminal::ERROR;
    }
    else if ((events & Model::EventMask(UartDmaEvent::COMPLETE)) != 0U &&
             terminal == UartOldTxTerminal::NONE)
    {
      terminal = UartOldTxTerminal::COMPLETE;
    }
  };

  const uint32_t before_stop = ServiceDmaTxStatus(in_isr);
  if (active_tx)
  {
    classify_tx(before_stop);
  }
  REQUIRE_FROM_CALLBACK(gdma_stop(tx_dma_channel_) == ESP_OK, in_isr);
  REQUIRE_FROM_CALLBACK(gdma_stop(rx_dma_channel_) == ESP_OK, in_isr);
  const uint32_t after_stop = ServiceDmaTxStatus(in_isr);
  if (active_tx)
  {
    classify_tx(after_stop);
  }
  REQUIRE_FROM_CALLBACK(gdma_reset(tx_dma_channel_) == ESP_OK, in_isr);
  REQUIRE_FROM_CALLBACK(gdma_reset(rx_dma_channel_) == ESP_OK, in_isr);

  // gdma_stop() issues the stop command but does not prove that the descriptor FSM has
  // parked before the final status sample. NONE therefore means completion was not
  // proven. The common model retains active and may replay it from byte zero.
  gdma_hal_clear_intr(&tx_gdma_hal_, tx_gdma_channel_id_, GDMA_CHANNEL_DIRECTION_TX,
                      UINT32_MAX);
  (void)gdma_hal_read_intr_status(&tx_gdma_hal_, tx_gdma_channel_id_,
                                  GDMA_CHANNEL_DIRECTION_TX, true);
  gdma_hal_clear_intr(&rx_gdma_hal_, rx_gdma_channel_id_, GDMA_CHANNEL_DIRECTION_RX,
                      UINT32_MAX);
  (void)gdma_hal_read_intr_status(&rx_gdma_hal_, rx_gdma_channel_id_,
                                  GDMA_CHANNEL_DIRECTION_RX, true);
  return terminal;
}

}  // namespace LibXR

#undef LIBXR_ESP_UHCI_RCC_ATOMIC

#endif
