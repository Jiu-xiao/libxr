#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

#include "driver/gpio.h"
#include "esp_def.hpp"
#include "esp_idf_version.h"
#include "esp_intr_alloc.h"
#if defined(CONFIG_PM_ENABLE) && CONFIG_PM_ENABLE
#include "esp_pm.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "hal/uart_hal.h"
#include "hal/uart_types.h"
#include "model/uart_dma_model.hpp"
#include "model/uart_execution_policy.hpp"
#include "soc/soc_caps.h"
#include "uart.hpp"

#if defined(SOC_AHB_GDMA_SUPPORTED) && SOC_AHB_GDMA_SUPPORTED && \
    defined(SOC_UHCI_SUPPORTED) && SOC_UHCI_SUPPORTED
#define LIBXR_ESP_UART_HAS_AHB_GDMA 1
#include "esp_private/gdma.h"
#include "esp_private/gdma_link.h"
#include "hal/dma_types.h"
#include "hal/gdma_hal.h"
#include "hal/uhci_hal.h"
#else
#define LIBXR_ESP_UART_HAS_AHB_GDMA 0
#endif

namespace LibXR
{

#if LIBXR_ESP_UART_HAS_AHB_GDMA

class ESP32UartDma;

#if (SOC_CPU_CORES_NUM > 1) && \
    (!defined(CONFIG_FREERTOS_UNICORE) || !CONFIG_FREERTOS_UNICORE)
inline constexpr bool ESP_UART_DMA_USES_IRQ_SERIALIZATION = true;
#else
inline constexpr bool ESP_UART_DMA_USES_IRQ_SERIALIZATION = false;
#endif

#if defined(CONFIG_APPTRACE_SV_ENABLE) && CONFIG_APPTRACE_SV_ENABLE
static_assert(!ESP_UART_DMA_USES_IRQ_SERIALIZATION,
              "ESP UART DMA SMP IRQ serialization is incompatible with the ESP-IDF "
              "SystemView interrupt wrapper");
#endif

class ESP32UartDmaIrqAdapter
{
 public:
  explicit ESP32UartDmaIrqAdapter(ESP32UartDma& owner) : owner_(owner) {}

  void LockAndMaskIrqDomain() noexcept;
  void UnlockIrqDomain() noexcept;
  void LockIrqDomain() noexcept;
  void RestoreAndUnlockIrqDomain() noexcept;

 private:
  ESP32UartDma& owner_;
};

template <bool UseIrqSerialization>
class ESP32UartDmaExecutionPolicyStorage;

template <>
class ESP32UartDmaExecutionPolicyStorage<false>
{
 public:
  explicit ESP32UartDmaExecutionPolicyStorage(ESP32UartDma&) {}

  template <typename Handler>
  bool Invoke(uint32_t events, Handler&& handler) noexcept
  {
    return policy_.Invoke(events, std::forward<Handler>(handler));
  }

  template <typename Source, typename Handler>
  bool InvokeIrq(Source&& source, Handler&& handler) noexcept
  {
    return policy_.InvokeIrq(std::forward<Source>(source),
                             std::forward<Handler>(handler));
  }

 private:
  UartDirectPolicy policy_{};
};

template <>
class ESP32UartDmaExecutionPolicyStorage<true>
{
 public:
  explicit ESP32UartDmaExecutionPolicyStorage(ESP32UartDma& owner)
      : adapter_(owner), policy_(adapter_)
  {
  }

  template <typename Handler>
  bool Invoke(uint32_t events, Handler&& handler) noexcept
  {
    return policy_.Invoke(events, std::forward<Handler>(handler));
  }

  template <typename Source, typename Handler>
  bool InvokeIrq(Source&& source, Handler&& handler) noexcept
  {
    return policy_.InvokeIrq(std::forward<Source>(source),
                             std::forward<Handler>(handler));
  }

 private:
  ESP32UartDmaIrqAdapter adapter_;
  UartIrqSerializedPolicy<ESP32UartDmaIrqAdapter> policy_;
};

using ESP32UartDmaExecutionPolicy =
    ESP32UartDmaExecutionPolicyStorage<ESP_UART_DMA_USES_IRQ_SERIALIZATION>;

/**
 * @brief ESP UART backend backed exclusively by UHCI/AHB-GDMA.
 *
 * This type exists only on targets that expose both AHB-GDMA and UHCI. TX uses the
 * common retained double-buffer model; RX uses a linked descriptor ring. Interrupts
 * are registered as non-IRAM handlers because the complete LibXR service and callback
 * chain is not required to remain executable while flash cache is disabled.
 */
class ESP32UartDma : public UART
{
  friend class ESP32UartDmaIrqAdapter;
  friend class UartDmaModel<ESP32UartDma, ESP32UartDmaExecutionPolicy>;

 public:
  static constexpr int PIN_NO_CHANGE = -1;

  ESP32UartDma(uart_port_t uart_num, int tx_pin, int rx_pin, int rts_pin = PIN_NO_CHANGE,
               int cts_pin = PIN_NO_CHANGE, size_t rx_buffer_size = 1024,
               size_t tx_buffer_size = 512, uint32_t tx_queue_size = 5,
               UART::Configuration config = {115200, UART::Parity::NO_PARITY, 8, 1});

  /**
   * @brief Apply one serialized framing and baud configuration.
   * @return `BUSY` while an earlier configuration request is outstanding.
   * @warning On single-core DirectPolicy targets, do not call this method from a
   *          higher-priority ISR that can preempt a related UART/GDMA ISR after its
   *          hardware-status read, or from inside that unfinished raw ISR path.
   */
  ErrorCode SetConfig(UART::Configuration config) override;

  /**
   * @brief Toggle the UART peripheral's internal loopback bit.
   * @warning The caller must quiesce traffic and configuration first.
   */
  ErrorCode SetLoopback(bool enable);

  static ErrorCode WriteFun(WritePort& port, bool in_isr);
  static ErrorCode ReadFun(ReadPort& port, bool in_isr);

 private:
  struct TxStorage
  {
    uint8_t* data = nullptr;
    size_t size = 0U;
    size_t block_stride = 0U;
    size_t cache_line_size = 1U;
  };

  static TxStorage AllocateTxStorage(size_t block_size);
  static bool ResolveWordLength(uint8_t data_bits, uart_word_length_t& out);
  static bool ResolveStopBits(uint8_t stop_bits, uart_stop_bits_t& out);
  static uart_parity_t ResolveParity(UART::Parity parity);
  static bool IsBaudrateRepresentable(uint32_t baudrate, uint32_t source_clock_hz);
  static bool IsCurrentTaskPinned();

  [[nodiscard]] ErrorCode ValidateConfig(UART::Configuration config) const;
  UartDmaControlResult AdvanceConfig(UART::Configuration config, bool active_tx,
                                     bool in_isr);
  UartDmaControlProgress CompleteConfig(bool in_isr);
  UartDmaControlResult AdvanceRecovery(bool active_tx, bool in_isr);
  UartDmaControlProgress CompleteRecovery(bool in_isr);
  UartDmaTxStartResult StartDmaTx(uint8_t* data, size_t size, int block, bool in_isr);

  bool ApplyConfigPayload(UART::Configuration config);
  ErrorCode InitPowerManagement();
  ErrorCode InitUartHardware();
  ErrorCode ConfigurePins();
  ErrorCode InstallUartIsr();
  ErrorCode InitDmaBackend();

  void SetIrqDomainEnabled(bool enabled) noexcept;
  void SetIrqDomainEnabledLocked(bool enabled) noexcept;
  void ConfigureDmaErrorInterruptPath();
  void ArmConfigTxIdleInterrupt();
  void DisarmConfigTxIdleInterrupt();

  static void UartIsrEntry(void* arg);
  static void DmaTxIsrEntry(void* arg);
  static void DmaRxIsrEntry(void* arg);

  static constexpr uint32_t DMA_UART_ERROR_INTR_MASK =
      UART_INTR_PARITY_ERR | UART_INTR_FRAM_ERR | UART_INTR_RXFIFO_OVF;

  uint32_t ServiceDmaTxStatus(bool in_isr);
  uint32_t ServiceDmaRxStatus(bool& pushed_any);
  uint32_t ServiceDmaUartStatus(bool in_isr);
  UartOldTxTerminal StopAndResetDma(bool active_tx, bool in_isr);
  bool DrainCompletedDmaRxDescriptors(bool& pushed_any);
  bool ResetAndRestartRxDma();
  bool PushRxBytes(const uint8_t* data, size_t size);

  uart_port_t uart_num_;
  int tx_pin_;
  int rx_pin_;
  int rts_pin_;
  int cts_pin_;

  UART::Configuration config_;
  uint32_t uart_sclk_hz_ = 0U;
  portMUX_TYPE irq_domain_lock_ = portMUX_INITIALIZER_UNLOCKED;
  bool irq_domain_masked_ = true;
  ESP32UartDmaExecutionPolicy execution_policy_;
  TxStorage tx_storage_{};

  bool config_waiting_tx_idle_ = false;
  bool config_tx_idle_interrupt_armed_ = false;
  UartOldTxTerminal config_old_tx_terminal_ = UartOldTxTerminal::NONE;

  bool uart_hw_enabled_ = false;
  uart_hal_context_t uart_hal_ = {};
  intr_handle_t uart_intr_handle_ = nullptr;
  bool uart_isr_installed_ = false;
#if defined(CONFIG_PM_ENABLE) && CONFIG_PM_ENABLE
  esp_pm_lock_handle_t pm_lock_ = nullptr;
#endif

  ReadPort _read_port;
  WritePort _write_port;
  UartDmaModel<ESP32UartDma, ESP32UartDmaExecutionPolicy> dma_model_;

  uhci_hal_context_t uhci_hal_ = {};
  gdma_channel_handle_t tx_dma_channel_ = nullptr;
  gdma_channel_handle_t rx_dma_channel_ = nullptr;
  uintptr_t tx_dma_head_addr_[2] = {0U, 0U};
  gdma_link_list_handle_t rx_dma_link_ = nullptr;
  uintptr_t rx_dma_head_addr_ = 0U;
  dma_descriptor_t* rx_dma_descriptors_ = nullptr;
  uint8_t* rx_dma_storage_ = nullptr;
  size_t rx_dma_chunk_size_ = 0U;
  size_t rx_dma_buffer_alignment_ = 1U;
  size_t rx_cache_line_size_ = 1U;
  uint32_t rx_dma_node_index_ = 0U;

  gdma_hal_context_t tx_gdma_hal_ = {};
  int tx_gdma_group_id_ = -1;
  int tx_gdma_channel_id_ = -1;
  intr_handle_t tx_gdma_intr_handle_ = nullptr;
  gdma_hal_context_t rx_gdma_hal_ = {};
  int rx_gdma_group_id_ = -1;
  int rx_gdma_channel_id_ = -1;
  intr_handle_t rx_gdma_intr_handle_ = nullptr;
};

#endif  // LIBXR_ESP_UART_HAS_AHB_GDMA

}  // namespace LibXR
