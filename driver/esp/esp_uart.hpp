#pragma once

#include <cstddef>
#include <cstdint>

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
#include "soc/soc_caps.h"
#include "uart.hpp"
#include "uart/uart_dma_model.hpp"

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

#if LIBXR_ESP_UART_HAS_AHB_GDMA
#include "esp_uart_execution_policy.hpp"
#endif

namespace LibXR
{

#if LIBXR_ESP_UART_HAS_AHB_GDMA

class ESP32UartDma;

/**
 * @brief 仅使用 UHCI/AHB-GDMA 的 ESP UART 后端 / ESP UART backend backed exclusively
 * by UHCI/AHB-GDMA
 *
 * 本类型仅存在于同时提供 AHB-GDMA 与 UHCI 的目标。TX 使用通用 retained
 * double-buffer 模型，RX 使用 linked descriptor ring。IRQ 注册为 non-IRAM handler，
 * 因为 flash cache 关闭期间无需保证整个 LibXR service 与 callback 链可执行。 / This
 * type exists only on targets that expose both AHB-GDMA and UHCI. TX uses the common
 * retained double-buffer model; RX uses a linked descriptor ring. Interrupts are
 * registered as non-IRAM handlers because the complete LibXR service and callback chain
 * is not required to remain executable while flash cache is disabled.
 *
 * @note SMP 目标上必须从固定到单一核心的 task 构造，使 non-shared UART/GDMA IRQ
 * 固定在同一核心 / On SMP targets, construct from a task pinned to exactly one core so
 * the non-shared UART/GDMA IRQs are fixed to that core
 */
class ESP32UartDma : public UART
{
  using ExecutionPolicy = Detail::ESP32UartExecutionPolicy<ESP32UartDma>;

  friend class Detail::ESP32UartIrqAdapter<ESP32UartDma>;
  friend class UartDmaModel<ESP32UartDma, ExecutionPolicy>;

 public:
  /**
   * @brief 保持既有 GPIO 路由不变的引脚值 / Pin value that preserves the existing GPIO
   * route
   */
  static constexpr int PIN_NO_CHANGE = -1;

  /**
   * @brief 构造并接管一个 ESP UHCI/AHB-GDMA UART / Construct and take ownership of one
   * ESP UHCI/AHB-GDMA UART
   * @param uart_num UART 外设编号 / UART peripheral number
   * @param tx_pin TX GPIO，或 `PIN_NO_CHANGE` / TX GPIO or `PIN_NO_CHANGE`
   * @param rx_pin RX GPIO，或 `PIN_NO_CHANGE` / RX GPIO or `PIN_NO_CHANGE`
   * @param rts_pin RTS GPIO，或 `PIN_NO_CHANGE` / RTS GPIO or `PIN_NO_CHANGE`
   * @param cts_pin CTS GPIO，或 `PIN_NO_CHANGE` / CTS GPIO or `PIN_NO_CHANGE`
   * @param rx_buffer_size RX descriptor ring 的 payload 容量 / RX descriptor-ring
   * payload capacity
   * @param tx_buffer_size 每个 TX 双缓冲 block 的容量 / Capacity of each TX
   * double-buffer block
   * @param tx_queue_size 待发送记录队列深度 / Pending TX record queue depth
   * @param config 初始 UART 帧格式和波特率 / Initial UART framing and baud rate
   */
  ESP32UartDma(uart_port_t uart_num, int tx_pin, int rx_pin, int rts_pin = PIN_NO_CHANGE,
               int cts_pin = PIN_NO_CHANGE, size_t rx_buffer_size = 1024,
               size_t tx_buffer_size = 512, uint32_t tx_queue_size = 5,
               UART::Configuration config = {115200, UART::Parity::NO_PARITY, 8, 1});

  /**
   * @brief 提交一次串行化帧格式和波特率配置 / Apply one serialized framing and baud
   * configuration
   * @param config 新的帧格式和波特率 / New framing and baud rate
   * @return 前一个配置仍未完成时返回 `BUSY` / `BUSY` while an earlier configuration
   * request is outstanding
   * @warning 在单核 DirectPolicy 目标上，不得从能在相关 UART/GDMA ISR 读取硬件状态后
   * 抢占它的高优先级 ISR，也不得从该尚未退出的 raw ISR 路径内部调用 / On single-core
   * DirectPolicy targets, do not call this method from a higher-priority ISR that can
   * preempt a related UART/GDMA ISR after its hardware-status read, or from inside that
   * unfinished raw ISR path
   */
  ErrorCode SetConfig(UART::Configuration config) override;

  /**
   * @brief 切换 UART 外设内部 loopback 位 / Toggle the UART peripheral's internal
   * loopback bit
   * @param enable 是否启用内部 loopback / Whether to enable internal loopback
   * @return UART 硬件尚未启用时返回 `STATE_ERR`，否则返回 `OK` / `STATE_ERR` before
   * UART hardware is enabled; otherwise `OK`
   * @warning 调用者必须先保证数据传输和配置均静止 / The caller must quiesce traffic and
   * configuration first
   */
  ErrorCode SetLoopback(bool enable);

  /**
   * @brief WritePort 提交入口 / WritePort submission entry
   * @param port 发起提交的写端口 / Write port issuing the submission
   * @param in_isr 当前调用是否位于 ISR / Whether the call is in an ISR
   * @return 提交结果 / Submission result
   */
  static ErrorCode WriteFun(WritePort& port, bool in_isr);

  /**
   * @brief ReadPort 读取入口 / ReadPort read entry
   * @param port 发起读取的读端口 / Read port issuing the read
   * @param in_isr 当前调用是否位于 ISR / Whether the call is in an ISR
   * @return 始终为 `PENDING`；RX producer 后续完成读取 / Always `PENDING`; the RX
   * producer completes the read later
   */
  static ErrorCode ReadFun(ReadPort& port, bool in_isr);

 private:
  struct TxStorage
  {
    uint8_t* data_ = nullptr;
    size_t size_ = 0U;
    size_t block_stride_ = 0U;
    size_t cache_line_size_ = 1U;
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
  ExecutionPolicy execution_policy_;
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
  UartDmaModel<ESP32UartDma, ExecutionPolicy> dma_model_;

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
