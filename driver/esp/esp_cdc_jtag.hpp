#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_def.hpp"
#include "esp_intr_alloc.h"
#include "esp_uart_execution_policy.hpp"
#include "soc/soc_caps.h"
#include "uart.hpp"

#if SOC_USB_SERIAL_JTAG_SUPPORTED &&                                      \
    ((defined(CONFIG_IDF_TARGET_ESP32C3) && CONFIG_IDF_TARGET_ESP32C3) || \
     (defined(CONFIG_IDF_TARGET_ESP32C6) && CONFIG_IDF_TARGET_ESP32C6))

namespace LibXR
{

class ESP32CDCJtag;

/**
 * @brief ESP32 USB Serial/JTAG 读端口 / ESP32 USB Serial/JTAG read port
 *
 * 该读端口在软件队列出队后，回调所属 CDC/JTAG 后端继续尝试排空硬件 RX FIFO。
 * This read port calls back into the owning CDC/JTAG backend after software
 * dequeues so the hardware RX FIFO can be drained again.
 */
class ESP32CDCJtagReadPort : public ReadPort
{
 public:
  /**
   * @brief 构造读端口 / Construct the read port
   * @param size RX 队列容量（字节） / RX queue capacity in bytes
   * @param owner 所属 CDC/JTAG 后端 / Owning CDC/JTAG backend
   */
  explicit ESP32CDCJtagReadPort(size_t size, ESP32CDCJtag& owner)
      : ReadPort(size), owner_(owner)
  {
  }

  /** @brief 软件队列出队后的回调 / Callback after software RX dequeue */
  void OnRxDequeue(bool in_isr) override;

 private:
  ESP32CDCJtag& owner_;  ///< 所属 CDC/JTAG 后端 / Owning CDC/JTAG backend
};

#if (defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG) &&           \
     CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG) ||                   \
    (defined(CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG) && \
     CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG)
static_assert(false,
              "ESP32CDCJtag requires exclusive USB Serial/JTAG ownership. Disable "
              "both the primary and secondary ESP-IDF USB Serial/JTAG console.");
#endif

/**
 * @brief ESP32 USB Serial/JTAG 后端实现 / ESP32 USB Serial/JTAG backend implementation
 *
 * TX payload 仅存放在 `WritePort` 字节队列中。每个 service owner scope 最多把 front 和
 * next 直接送入 64 字节硬件 FIFO；一条记录的最后一个字节被硬件接受后，该 write 即完成。
 * TX payload remains only in the `WritePort` byte queue. Each service-owner scope feeds
 * at most front plus next directly into the 64-byte hardware FIFO; a write completes
 * when the hardware accepts the final byte of that record.
 *
 * @note 每个 USB Serial/JTAG 外设最多构造一个 process-lifetime 后端对象 / Construct at
 * most one process-lifetime backend object for each USB Serial/JTAG peripheral
 * @note 该对象存活期间不得安装 ESP-IDF USB Serial/JTAG driver / Do not install the
 * ESP-IDF USB Serial/JTAG driver while this object is alive
 */
class ESP32CDCJtag : public UART
{
  friend class ESP32CDCJtagReadPort;
  friend class Detail::ESP32UartIrqAdapter<ESP32CDCJtag>;

 public:
  /**
   * @brief 构造并初始化 USB Serial/JTAG 后端 / Construct and initialize the USB
   * Serial/JTAG backend
   * @param rx_buffer_size RX 软件队列容量 / RX software queue capacity
   * @param tx_buffer_size TX 软件字节队列容量，也是单条 TX 记录的最大字节数 / TX
   * software byte-queue capacity and maximum size of one TX record
   * @param tx_queue_size TX 请求队列深度 / Pending TX record queue depth
   * @param config 初始 UART 帧格式配置 / Initial UART framing configuration
   */
  explicit ESP32CDCJtag(size_t rx_buffer_size = 1024, size_t tx_buffer_size = 512,
                        uint32_t tx_queue_size = 5,
                        UART::Configuration config = {115200, UART::Parity::NO_PARITY, 8,
                                                      1});

  /**
   * @brief 校验固定 8N1 配置 / Validate the fixed 8N1 configuration
   */
  ErrorCode SetConfig(UART::Configuration config) override;

  /** @brief WritePort TX 推进 doorbell / WritePort TX progress doorbell */
  static void WriteFun(WritePort& port, bool in_isr);

 private:
  using ExecutionPolicy = Detail::ESP32UartExecutionPolicy<ESP32CDCJtag>;

  enum class Event : uint32_t
  {
    WRITE = 1U << 0U,
    TX_EMPTY = 1U << 1U,
    RX_DATA = 1U << 2U,
    RX_SPACE = 1U << 3U,
  };

  static constexpr uint32_t EventMask(Event event)
  {
    return static_cast<uint32_t>(event);
  }

  static void IsrEntry(void* arg);
  ErrorCode InitHardware();

  void SetIrqDomainEnabled(bool enabled) noexcept;
  void SetIrqDomainEnabledLocked(bool enabled) noexcept;
  void SetOwnedInterruptsEnabled(uint32_t mask, bool enabled) noexcept;
  void ArmTxEmptyInterrupt();
  void DisarmTxEmptyInterrupt();

  uint32_t ServiceIrqSource(bool in_isr) noexcept;
  uint32_t ServiceEvents(uint32_t events, bool in_isr,
                         ReadPort::ReadQueue& queue) noexcept;
  void HandleInterrupt();

  void ResumeRx(bool in_isr);
  uint32_t ServiceRx(bool in_isr, ReadPort::ReadQueue& queue);
  void DrainRxToQueue(ReadPort::ReadQueue& queue, bool in_isr);
  void PushRxBytes(ReadPort::ReadQueue& queue, const uint8_t* data, size_t size,
                   bool in_isr);

  void ProgressTx(bool in_isr);
  size_t FillTxFifo(WritePort::WriteQueue& queue, bool in_isr);

  portMUX_TYPE irq_domain_lock_ = portMUX_INITIALIZER_UNLOCKED;
  bool irq_domain_masked_ = true;
  ExecutionPolicy execution_policy_;

  intr_handle_t intr_handle_ = nullptr;
  bool hw_inited_ = false;
  bool tx_empty_interrupt_armed_ = false;
  bool tx_flush_pending_ = false;

  ESP32CDCJtagReadPort _read_port;
  WritePort _write_port;
};

}  // namespace LibXR

#endif
