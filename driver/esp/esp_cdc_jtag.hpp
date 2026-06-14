#pragma once

#include "esp_def.hpp"

#include <cstddef>
#include <cstdint>

#include "esp_intr_alloc.h"
#include "esp_tx_double_buffer.hpp"
#include "flag.hpp"
#include "soc/soc_caps.h"
#include "uart.hpp"

#if SOC_USB_SERIAL_JTAG_SUPPORTED &&                                              \
    ((defined(CONFIG_IDF_TARGET_ESP32C3) && CONFIG_IDF_TARGET_ESP32C3) ||         \
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
   *
   * @param size RX 队列容量（字节） / RX queue capacity in bytes
   * @param owner 所属 CDC/JTAG 后端 / Owning CDC/JTAG backend
   */
  explicit ESP32CDCJtagReadPort(size_t size, ESP32CDCJtag& owner)
      : ReadPort(size), owner_(owner)
  {
  }

  /**
   * @brief 软件队列出队后的回调 / Callback after software RX dequeue
   */
  void OnRxDequeue(bool in_isr) override;

  ESP32CDCJtagReadPort& operator=(ReadFun fun)
  {
    ReadPort::operator=(fun);
    return *this;
  }

 private:
  ESP32CDCJtag& owner_;  ///< 所属 CDC/JTAG 后端 / Owning CDC/JTAG backend
};

#if defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG) && CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
static_assert(false,
              "ESP32CDCJtag conflicts with ESP-IDF primary USB Serial/JTAG console. "
              "Set CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=n or disable this backend.");
#endif

/**
 * @brief ESP32 USB Serial/JTAG 后端实现 / ESP32 USB Serial/JTAG backend implementation
 *
 * 该类持有 USB Serial/JTAG 中断源、TX 双缓冲辅助器，以及 UART 基类使用的
 * 队列驱动读写桥。
 * This class owns the USB Serial/JTAG interrupt source, the TX double-buffer
 * helper, and the queue-driven read/write bridge used by the UART base class.
 */
class ESP32CDCJtag : public UART
{
  friend class ESP32CDCJtagReadPort;

 public:
  /**
   * @brief 构造并初始化 USB Serial/JTAG 后端状态 / Construct and initialize the USB Serial/JTAG backend state
   *
   * @param rx_buffer_size RX 队列容量（字节） / RX queue capacity in bytes
   * @param tx_buffer_size TX payload 半缓冲大小（字节） / TX payload half-buffer size in bytes
   * @param tx_queue_size TX 请求队列深度 / Number of queued TX requests
   * @param config 初始 UART 帧格式配置 / Initial UART framing configuration
   */
  explicit ESP32CDCJtag(
      size_t rx_buffer_size = 1024, size_t tx_buffer_size = 512,
      uint32_t tx_queue_size = 5,
      UART::Configuration config = {115200, UART::Parity::NO_PARITY, 8, 1});

  /**
   * @brief 应用 UART 帧格式配置 / Apply a UART framing configuration to the backend
   */
  ErrorCode SetConfig(UART::Configuration config) override;

  /**
   * @brief 用于 TX 启动的 WritePort 跳板函数 / WritePort trampoline for TX startup
   */
  static ErrorCode WriteFun(WritePort& port, bool in_isr);

  /**
   * @brief USB Serial/JTAG RX 路径的 ReadPort 入口 / ReadPort entry point for the USB Serial/JTAG RX path
   */
  static ErrorCode ReadFun(ReadPort& port, bool in_isr);

 private:
  /**
   * @brief USB Serial/JTAG 中断跳板函数 / USB Serial/JTAG ISR trampoline
   */
  static void IsrEntry(void* arg);

  /**
   * @brief 初始化 USB Serial/JTAG 中断和硬件状态 / Initialize the USB Serial/JTAG interrupt and hardware state
   */
  ErrorCode InitHardware();

  /**
   * @brief 分发一批 USB Serial/JTAG 中断 / Dispatch one USB Serial/JTAG interrupt batch
   */
  void HandleInterrupt();

  /**
   * @brief 尝试将硬件 RX FIFO 数据推进软件队列 / Try draining hardware RX FIFO into the software queue
   */
  void DrainRxToQueue(bool in_isr);

  /**
   * @brief 从队列化的 TX 状态启动发送工作 / Start transmit work from the queued TX state
   */
  ErrorCode TryStartTx(bool in_isr);

  /**
   * @brief 从队列装载一个 active TX 请求 / Load one active TX request from the queue
   */
  bool LoadActiveTxFromQueue(bool in_isr);

  /**
   * @brief 从队列装载一个 pending TX 请求 / Load one pending TX request from the queue
   */
  bool LoadPendingTxFromQueue(bool in_isr);

  /**
   * @brief 硬件空闲时把 pending TX 提升为 active 状态 / Promote pending TX into active state if hardware is idle
   */
  bool StartPendingTxIfIdle(bool in_isr);

  /**
   * @brief 将一条排队的 TX payload 拷入选定 slot / Copy one queued TX payload into the selected slot
   */
  bool DequeueTxToSlot(uint8_t* slot, size_t& size, WriteInfoBlock& info, bool in_isr);

  /**
   * @brief 启动当前 active TX 请求 / Start the current active TX request
   */
  bool StartActiveTransfer(bool in_isr);

  /**
   * @brief 启动 active TX 并上报队列所有权交接 / Start active TX and report queue ownership transfer
   */
  bool StartAndReportActive(bool in_isr);

  /**
   * @brief 停止 USB Serial/JTAG TX 传输引擎 / Stop the USB Serial/JTAG TX transfer engine
   */
  void StopTxTransfer();

  /**
   * @brief 收尾一次 TX 传输结果 / Finalize one TX transfer result
   */
  void OnTxTransferDone(bool in_isr, ErrorCode result);

  /**
   * @brief 向硬件 TX FIFO 补料 / Pump bytes into the hardware TX FIFO
   */
  bool PumpTx(bool in_isr);

  /**
   * @brief 将收到的字节推入软件读队列 / Push received bytes into the software read queue
   */
  void PushRxBytes(const uint8_t* data, size_t size, bool in_isr);

  /**
   * @brief 清除 active TX 请求状态 / Clear the active TX request state
   */
  void ClearActiveTx();

  /**
   * @brief 清除 pending TX 请求状态 / Clear the pending TX request state
   */
  void ClearPendingTx();

  /**
   * @brief 重置两个 TX slot 和忙标志 / Reset both TX slots and the busy flag
   */
  void ResetTxState(bool in_isr);

  UART::Configuration config_;  ///< Current UART framing configuration.
  uint8_t* tx_slot_storage_ = nullptr;  ///< Backing storage for the TX helper.
  ESPTxDoubleBuffer tx_double_buffer_;   ///< TX helper for active/pending payloads.
  intr_handle_t intr_handle_ = nullptr;  ///< Registered interrupt handle.
  bool intr_installed_ = false;          ///< Whether the interrupt was installed.
  bool hw_inited_ = false;               ///< Whether hardware initialization completed.
  Flag::Atomic tx_busy_{};               ///< Hardware TX engine busy flag.
  Flag::Atomic rx_draining_{};           ///< RX FIFO draining gate.
  Flag::Plain in_tx_isr_;                ///< Reentry guard while servicing TX IRQs.

  ESP32CDCJtagReadPort _read_port;   ///< Read-side queue bridge exposed to `UART`.
  WritePort _write_port; ///< Write-side queue bridge exposed to `UART`.
};

}  // namespace LibXR

#endif
