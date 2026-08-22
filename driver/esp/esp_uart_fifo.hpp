#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_def.hpp"
#include "esp_intr_alloc.h"
#if defined(CONFIG_PM_ENABLE) && CONFIG_PM_ENABLE
#include "esp_pm.h"
#endif
#include "esp_uart_execution_policy.hpp"
#include "hal/uart_hal.h"
#include "hal/uart_types.h"
#include "uart.hpp"
#include "uart/uart_rx_config_gate.hpp"

namespace LibXR
{

class ESP32UartFifo;

namespace Detail
{

/**
 * @brief 在软件 RX 队列释放空间后恢复 FIFO 接收的 ReadPort / ReadPort that resumes FIFO
 * reception after software RX space is released
 */
class ESP32UartFifoReadPort : public ReadPort
{
 public:
  /**
   * @brief 绑定 FIFO UART owner / Bind the FIFO UART owner
   * @param size 软件 RX 队列容量 / Software RX queue capacity
   * @param owner 本端口可调用期间必须保持有效的非 owning FIFO UART / Non-owning FIFO UART
   * that must remain valid while this port is callable
   */
  ESP32UartFifoReadPort(size_t size, ESP32UartFifo& owner) : ReadPort(size), owner_(owner)
  {
  }

  /**
   * @brief 出队后恢复因背压关闭的 RX 中断 / Resume RX interrupts disabled by
   * backpressure after a dequeue
   * @param in_isr 当前出队是否位于 ISR / Whether the dequeue occurs in an ISR
   */
  void OnRxDequeue(bool in_isr) override;

 private:
  ESP32UartFifo& owner_;
};

}  // namespace Detail

/**
 * @brief 将排队记录直接送入硬件 FIFO 的 ESP UART 后端 / ESP UART backend that streams
 * queued records directly into the hardware FIFO
 *
 * 应用必须显式选择本类型。它不分配 DMA 存储区，也不保留持久 READY block；每个 owner
 * scope 最多把 front 和 next 直接送入 FIFO。一条记录的全部字节进入硬件 FIFO 后，write
 * 即完成；只有 CONFIG 需要 framing boundary 时才另外等待物理线路空闲。 / This class is
 * selected explicitly by the application. It allocates no DMA storage and retains no
 * persistent READY block; each owner scope feeds at most front plus next directly into
 * the FIFO. A write completes after all bytes of that record have entered the hardware
 * FIFO; physical line idle is observed separately only when CONFIG needs a framing
 * boundary.
 *
 * @note 每个 UART 外设最多构造一个 process-lifetime 后端对象 / Construct at most one
 * process-lifetime backend object for each UART peripheral
 * @note SMP 目标上必须从固定到单一核心的 task 构造，使 ESP-IDF 将 non-shared UART IRQ
 * 分配到该核心 / On SMP targets, construct from a task pinned to exactly one core so
 * ESP-IDF allocates its non-shared UART interrupt on that fixed core
 */
class ESP32UartFifo : public UART
{
  friend class Detail::ESP32UartFifoReadPort;
  friend class Detail::ESP32UartIrqAdapter<ESP32UartFifo>;

 public:
  /**
   * @brief 保持既有 GPIO 路由不变的引脚值 / Pin value that preserves the existing GPIO
   * route
   */
  static constexpr int PIN_NO_CHANGE = -1;

  /**
   * @brief 构造并接管一个 ESP FIFO UART / Construct and take ownership of one ESP FIFO
   * UART
   * @param uart_num UART 外设编号 / UART peripheral number
   * @param tx_pin TX GPIO，或 `PIN_NO_CHANGE` / TX GPIO or `PIN_NO_CHANGE`
   * @param rx_pin RX GPIO，或 `PIN_NO_CHANGE` / RX GPIO or `PIN_NO_CHANGE`
   * @param rts_pin RTS GPIO，或 `PIN_NO_CHANGE` / RTS GPIO or `PIN_NO_CHANGE`
   * @param cts_pin CTS GPIO，或 `PIN_NO_CHANGE` / CTS GPIO or `PIN_NO_CHANGE`
   * @param rx_buffer_size 软件 RX 队列容量 / Software RX queue capacity
   * @param tx_buffer_size 单条 TX 记录的最大字节数 / Maximum bytes in one TX record
   * @param tx_queue_size 待发送记录队列深度 / Pending TX record queue depth
   * @param config 初始 UART 帧格式和波特率 / Initial UART framing and baud rate
   */
  ESP32UartFifo(uart_port_t uart_num, int tx_pin, int rx_pin, int rts_pin = PIN_NO_CHANGE,
                int cts_pin = PIN_NO_CHANGE, size_t rx_buffer_size = 1024,
                size_t tx_buffer_size = 512, uint32_t tx_queue_size = 5,
                UART::Configuration config = {115200, UART::Parity::NO_PARITY, 8, 1});

  /**
   * @brief 提交一次串行化帧格式和波特率配置 / Submit one serialized framing and baud
   * configuration
   * @param config 新的帧格式和波特率 / New framing and baud rate
   * @param in_isr 是否从 ISR 上下文调用；普通任务上下文可省略，默认值为 false / Whether
   * called from ISR context; ordinary task context may omit it and defaults to false
   * @return 请求被接纳时返回 `OK`，前一个配置仍未完成时返回 `BUSY`；硬件应用可在返回后
   * 完成 / `OK` when admitted; `BUSY` while an earlier configuration request is
   * outstanding. Hardware application may complete after return.
   * @warning 在单核 DirectPolicy 目标上，不得从能在本 UART ISR 读取硬件状态后抢占它的
   * 高优先级 ISR，也不得从该尚未退出的 raw ISR 路径内部调用 / On single-core
   * DirectPolicy targets, do not call this method from a higher-priority ISR that can
   * preempt this UART ISR after its hardware-status read, or from inside that unfinished
   * raw ISR path
   */
  ErrorCode SetConfig(UART::Configuration config, bool in_isr = false) override;

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
   * @brief WritePort TX 推进 doorbell / WritePort TX progress doorbell
   * @param port 请求推进的写端口 / Write port requesting progress
   * @param in_isr 当前调用是否位于 ISR / Whether the call is in an ISR
   */
  static void WriteFun(WritePort& port, bool in_isr);

 private:
  using ExecutionPolicy = Detail::ESP32UartExecutionPolicy<ESP32UartFifo>;

  enum class Event : uint32_t
  {
    WRITE = 1U << 0U,
    TX_SPACE = 1U << 1U,
    TX_IDLE = 1U << 2U,
    CONFIG = 1U << 3U,
    RX_DATA = 1U << 4U,
    RX_SPACE = 1U << 5U,
    RX_ERROR = 1U << 6U,
  };

  enum class ConfigState : uint8_t
  {
    NORMAL = 0,
    CONFIGURING,
  };

  static constexpr uint32_t EventMask(Event event)
  {
    return static_cast<uint32_t>(event);
  }

  [[nodiscard]] ErrorCode ValidateConfig(UART::Configuration config) const;
  bool ApplyConfigPayload(UART::Configuration config);
  ErrorCode InitPowerManagement();
  ErrorCode InitUartHardware(int tx_pin, int rx_pin, int rts_pin, int cts_pin);
  ErrorCode InstallUartIsr();

  void ConfigureRxInterruptPath();
  void SetOwnedInterruptsEnabled(uint32_t mask, bool enabled) noexcept;
  void SetRxInterruptPathEnabled(bool enabled);
  void ArmTxSpaceInterrupt();
  void DisarmTxSpaceInterrupt();
  void ArmConfigTxIdleInterrupt();
  void DisarmConfigTxIdleInterrupt();

  void SetIrqDomainEnabled(bool enabled) noexcept;
  void SetIrqDomainEnabledLocked(bool enabled) noexcept;

  static void UartIsrEntry(void* arg);
  uint32_t ServiceIrqSource(bool in_isr) noexcept;
  uint32_t ServiceEvents(uint32_t events, bool in_isr,
                         ReadPort::ReadQueue& queue) noexcept;

  void ResumeRx(bool in_isr);
  uint32_t ServiceRx(uint32_t events, bool in_isr, ReadPort::ReadQueue& queue);
  void DrainRxFifo(ReadPort::ReadQueue& queue, bool in_isr);

  void BeginConfiguration();
  uint32_t ContinueConfiguration(bool in_isr);

  void ProgressTx(bool in_isr);
  size_t FillTxFifo(WritePort::WriteQueue& queue, size_t limit, bool in_isr);

  uart_port_t uart_num_;

  UART::Configuration requested_config_{};
  uint32_t uart_sclk_hz_ = 0U;

  portMUX_TYPE irq_domain_lock_ = portMUX_INITIALIZER_UNLOCKED;
  bool irq_domain_masked_ = true;
  ExecutionPolicy execution_policy_;
  UartRxConfigGate rx_config_gate_{};

  ConfigState config_state_ = ConfigState::NORMAL;
  bool tx_front_partial_ = false;
  bool tx_space_interrupt_armed_ = false;
  bool config_tx_idle_interrupt_armed_ = false;
  bool rx_interrupt_path_enabled_ = false;

  bool uart_hw_enabled_ = false;
  uart_hal_context_t uart_hal_ = {};
  intr_handle_t uart_intr_handle_ = nullptr;
  bool uart_isr_installed_ = false;
#if defined(CONFIG_PM_ENABLE) && CONFIG_PM_ENABLE
  esp_pm_lock_handle_t pm_lock_ = nullptr;
#endif

  Detail::ESP32UartFifoReadPort _read_port;
  WritePort _write_port;
};

}  // namespace LibXR
