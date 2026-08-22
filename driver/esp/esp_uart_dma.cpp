#include "esp_uart_dma.hpp"

#if LIBXR_ESP_UART_HAS_AHB_GDMA

#include <algorithm>

#include "esp_attr.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_private/esp_cache_private.h"
#include "esp_uart_hal.hpp"

namespace LibXR
{

ErrorCode ESP32UartDma::InitPowerManagement()
{
#if defined(CONFIG_PM_ENABLE) && CONFIG_PM_ENABLE
  if (pm_lock_ != nullptr)
  {
    return ErrorCode::STATE_ERR;
  }

  const esp_pm_lock_type_t lock_type = Detail::ESP_UART_CLOCK_REQUIRES_APB_LOCK
                                           ? ESP_PM_APB_FREQ_MAX
                                           : ESP_PM_NO_LIGHT_SLEEP;
  if (esp_pm_lock_create(lock_type, 0, "libxr_uart", &pm_lock_) != ESP_OK)
  {
    return ErrorCode::INIT_ERR;
  }
  if (esp_pm_lock_acquire(pm_lock_) != ESP_OK)
  {
    (void)esp_pm_lock_delete(pm_lock_);
    pm_lock_ = nullptr;
    return ErrorCode::INIT_ERR;
  }
#else
  (void)Detail::ESP_UART_CLOCK_REQUIRES_APB_LOCK;
#endif
  return ErrorCode::OK;
}

void IRAM_ATTR ESP32UartDma::SetIrqDomainEnabled(bool enabled) noexcept
{
  portENTER_CRITICAL_SAFE(&irq_domain_lock_);
  SetIrqDomainEnabledLocked(enabled);
  portEXIT_CRITICAL_SAFE(&irq_domain_lock_);
}

void IRAM_ATTR ESP32UartDma::SetIrqDomainEnabledLocked(bool enabled) noexcept
{
  const bool masked = !enabled;
  if (irq_domain_masked_ == masked)
  {
    return;
  }

  const bool in_isr = xPortInIsrContext() != pdFALSE;
  bool success = true;
  const auto set_handle = [enabled, &success](intr_handle_t handle)
  {
    if (handle == nullptr)
    {
      return;
    }
    const esp_err_t result = enabled ? esp_intr_enable(handle) : esp_intr_disable(handle);
    success = (result == ESP_OK) && success;
  };

  set_handle(uart_intr_handle_);
  set_handle(tx_gdma_intr_handle_);
  if (rx_gdma_intr_handle_ != tx_gdma_intr_handle_)
  {
    set_handle(rx_gdma_intr_handle_);
  }
  REQUIRE_FROM_CALLBACK(success, in_isr);
  irq_domain_masked_ = masked;
}

ESP32UartDma::TxStorage ESP32UartDma::AllocateTxStorage(size_t block_size)
{
  REQUIRE(block_size > 0U && block_size <= DMA_LINK_ITEM_MAX_SIZE);
  TxStorage storage{};

  size_t cache_line_size = 1U;
  if (esp_cache_get_alignment(MALLOC_CAP_INTERNAL, &cache_line_size) != ESP_OK)
  {
    return storage;
  }
  cache_line_size = std::max<size_t>(cache_line_size, 1U);
  const size_t alignment = std::max<size_t>(alignof(size_t), cache_line_size);
  if (((alignment & (alignment - 1U)) != 0U) ||
      (block_size > (SIZE_MAX - (alignment - 1U))))
  {
    return storage;
  }

  const size_t block_stride = ((block_size + alignment - 1U) / alignment) * alignment;
  if ((block_stride == 0U) || (block_stride > (SIZE_MAX / 2U)))
  {
    return storage;
  }

  const size_t size = block_stride * 2U;
  auto* data = static_cast<uint8_t*>(heap_caps_aligned_alloc(
      alignment, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  if (data == nullptr)
  {
    return storage;
  }

  storage.data_ = data;
  storage.size_ = size;
  storage.block_stride_ = block_stride;
  storage.cache_line_size_ = cache_line_size;
  return storage;
}

ESP32UartDma::ESP32UartDma(uart_port_t uart_num, int tx_pin, int rx_pin, int rts_pin,
                           int cts_pin, size_t rx_buffer_size, size_t tx_buffer_size,
                           uint32_t tx_queue_size, UART::Configuration config)
    : UART(&_read_port, &_write_port),
      uart_num_(uart_num),
      tx_pin_(tx_pin),
      rx_pin_(rx_pin),
      rts_pin_(rts_pin),
      cts_pin_(cts_pin),
      config_(config),
      execution_policy_(*this),
      tx_storage_(AllocateTxStorage(tx_buffer_size)),
      _read_port(rx_buffer_size),
      _write_port(tx_queue_size, tx_buffer_size),
      dma_model_(*this, execution_policy_, _write_port,
                 RawData(tx_storage_.data_, tx_storage_.size_))
{
  REQUIRE(!Detail::IsEspConsoleUartInUse(uart_num_));
  REQUIRE(uart_num_ < UART_NUM_MAX);
  REQUIRE(uart_num_ < SOC_UART_HP_NUM);
  REQUIRE(rx_buffer_size > 0U);
  REQUIRE(tx_queue_size > 0U);
  if constexpr (Detail::ESP_UART_USES_IRQ_SERIALIZATION)
  {
    REQUIRE(Detail::IsCurrentTaskPinnedToOneCore());
  }

  _write_port = WriteFun;

  REQUIRE(InitUartHardware() == ErrorCode::OK);
  REQUIRE(InitPowerManagement() == ErrorCode::OK);
  REQUIRE(InitDmaBackend() == ErrorCode::OK);
  REQUIRE(InstallUartIsr() == ErrorCode::OK);
  ConfigureDmaErrorInterruptPath();
  DEV_ASSERT(esp_intr_get_cpu(uart_intr_handle_) ==
             esp_intr_get_cpu(tx_gdma_intr_handle_));
  DEV_ASSERT(esp_intr_get_cpu(uart_intr_handle_) ==
             esp_intr_get_cpu(rx_gdma_intr_handle_));
  SetIrqDomainEnabled(true);
}

void IRAM_ATTR ESP32UartDma::UartIsrEntry(void* arg)
{
  auto* self = static_cast<ESP32UartDma*>(arg);
  if (self == nullptr)
  {
    return;
  }
  (void)self->dma_model_.InvokeIrq(
      [self]() noexcept { return self->ServiceDmaUartStatus(true); }, true);
}

ErrorCode ESP32UartDma::InstallUartIsr()
{
  if (uart_isr_installed_)
  {
    return ErrorCode::OK;
  }

  constexpr int UART_INTR_FLAGS = ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_INTRDISABLED;
  const esp_err_t result =
      esp_intr_alloc(Detail::GetEspUartInterruptSource(uart_num_), UART_INTR_FLAGS,
                     UartIsrEntry, this, &uart_intr_handle_);
  if (result != ESP_OK)
  {
    return ErrorCode::INIT_ERR;
  }
  uart_isr_installed_ = true;
  return ErrorCode::OK;
}

void ESP32UartDma::ConfigureDmaErrorInterruptPath()
{
  uart_hal_clr_intsts_mask(&uart_hal_, DMA_UART_ERROR_INTR_MASK);
  uart_hal_ena_intr_mask(&uart_hal_, DMA_UART_ERROR_INTR_MASK);
}

void ESP32UartDma::ArmConfigTxIdleInterrupt()
{
  if (config_tx_idle_interrupt_armed_)
  {
    return;
  }

  // TX_DONE belongs to the current TX generation: StartDmaTx() cleared any stale raw
  // bit before launching it. Publish the armed state before enabling the source so an
  // immediate IRQ can retain the valid carrier when the UART FSM is not idle yet.
  config_tx_idle_interrupt_armed_ = true;
  uart_hal_ena_intr_mask(&uart_hal_, UART_INTR_TX_DONE);
}

void ESP32UartDma::DisarmConfigTxIdleInterrupt()
{
  if (!config_tx_idle_interrupt_armed_)
  {
    return;
  }
  uart_hal_disable_intr_mask(&uart_hal_, UART_INTR_TX_DONE);
  uart_hal_clr_intsts_mask(&uart_hal_, UART_INTR_TX_DONE);
  config_tx_idle_interrupt_armed_ = false;
}

ErrorCode ESP32UartDma::SetConfig(UART::Configuration config, bool in_isr)
{
  return dma_model_.SetConfig(config, in_isr);
}

ErrorCode ESP32UartDma::ValidateConfig(UART::Configuration config) const
{
  return Detail::ValidateEspUartConfig(config, uart_sclk_hz_, uart_hw_enabled_);
}

UartDmaControlResult ESP32UartDma::AdvanceConfig(UART::Configuration config,
                                                 bool active_tx, bool in_isr)
{
  if (!config_waiting_tx_idle_)
  {
    config_old_tx_terminal_ = StopAndResetDma(active_tx, in_isr);
    config_waiting_tx_idle_ = true;
  }

  if (!uart_hal_is_tx_idle(&uart_hal_))
  {
    ArmConfigTxIdleInterrupt();
    if (!uart_hal_is_tx_idle(&uart_hal_))
    {
      return UartDmaControlResult::Pending();
    }
  }

  DisarmConfigTxIdleInterrupt();
  const UartOldTxTerminal terminal = config_old_tx_terminal_;
  config_old_tx_terminal_ = UartOldTxTerminal::NONE;
  config_waiting_tx_idle_ = false;
  REQUIRE_FROM_CALLBACK(ApplyConfigPayload(config), in_isr);
  return UartDmaControlResult::Completed(terminal);
}

UartDmaControlProgress ESP32UartDma::CompleteConfig(bool in_isr)
{
  REQUIRE_FROM_CALLBACK(ResetAndRestartRxDma(), in_isr);
  return UartDmaControlProgress::COMPLETED;
}

bool ESP32UartDma::ApplyConfigPayload(UART::Configuration config)
{
  if (!Detail::ApplyEspUartConfig(uart_hal_, config, uart_sclk_hz_))
  {
    return false;
  }

  uart_hal_txfifo_rst(&uart_hal_);
  uart_hal_rxfifo_rst(&uart_hal_);
  uart_hal_clr_intsts_mask(&uart_hal_, UINT32_MAX);

  config_ = config;
  return true;
}

ErrorCode ESP32UartDma::SetLoopback(bool enable)
{
  if (!uart_hw_enabled_)
  {
    return ErrorCode::STATE_ERR;
  }
  Detail::SetEspUartLoopback(uart_hal_, enable);
  return ErrorCode::OK;
}

void IRAM_ATTR ESP32UartDma::WriteFun(WritePort& port, bool in_isr)
{
  auto* uart = LibXR::ContainerOf(&port, &ESP32UartDma::_write_port);
  uart->dma_model_.Submit(in_isr);
}

ErrorCode ESP32UartDma::InitUartHardware()
{
  const ErrorCode init_result =
      Detail::InitEspUartHal(uart_num_, uart_hal_, uart_sclk_hz_);
  if (init_result != ErrorCode::OK)
  {
    return init_result;
  }

  if (!ApplyConfigPayload(config_))
  {
    return ErrorCode::INIT_ERR;
  }
  uart_hw_enabled_ = true;

  if (Detail::ConfigureEspUartPins(uart_num_, tx_pin_, rx_pin_, rts_pin_, cts_pin_) !=
      ErrorCode::OK)
  {
    uart_hw_enabled_ = false;
    return ErrorCode::INIT_ERR;
  }

  uart_hal_txfifo_rst(&uart_hal_);
  uart_hal_rxfifo_rst(&uart_hal_);
  uart_hal_clr_intsts_mask(&uart_hal_, UINT32_MAX);
  uart_hal_disable_intr_mask(&uart_hal_, UINT32_MAX);
  return ErrorCode::OK;
}

void IRAM_ATTR ESP32UartDma::PushRxBytes(ReadPort::ReadQueue& queue, const uint8_t* data,
                                         size_t size)
{
  size_t offset = 0U;
  while (offset < size)
  {
    const size_t free_space = queue.EmptySize();
    if (free_space == 0U)
    {
      break;
    }

    const size_t chunk = std::min(free_space, size - offset);
    if (queue.PushBatch(data + offset, chunk) != ErrorCode::OK)
    {
      break;
    }
    offset += chunk;
  }
}

}  // namespace LibXR

#endif  // LIBXR_ESP_UART_HAS_AHB_GDMA
