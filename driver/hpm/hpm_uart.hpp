#pragma once

#include <cstddef>
#include <cstdint>

#include "double_buffer.hpp"
#include "flag.hpp"
#include "hpm_clock_drv.h"
#include "hpm_dmamux_drv.h"
#include "hpm_dmav2_drv.h"
#include "hpm_soc.h"
#include "hpm_uart_drv.h"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "serialized_service.hpp"
#include "uart.hpp"

#if __has_include("hpm_dma_mgr.h")
#include "hpm_dma_mgr.h"
#define LIBXR_HPM_UART_HAS_DMA_MGR 1
#else
#define LIBXR_HPM_UART_HAS_DMA_MGR 0
#endif

namespace LibXR
{

/**
 * @brief HPM UART with DMA staging and serialized RX/TX/configuration.
 * Writes complete when copied into an owned TX half, not at physical line idle.
 * Configuration never replays an active transfer; staged data is retained.
 * BSP initializes instances serially. Without the SDK DMA manager it exclusively
 * assigns channels; with the manager, res_ records the allocated channels.
 * All handlers and calls execute on one core. Buffers and objects outlive use.
 * RX is a circular DMA stream: service it before a full-buffer lap; excess data
 * beyond software queue space is discarded. Cached buffers must occupy exclusive
 * cache lines; noncacheable placement is preferred. No flow-control guarantee.
 */
class HPMUART : public UART
{
 public:
  static constexpr uint8_t MAX_UART_INSTANCES = 9;
  static constexpr uint8_t INVALID_INSTANCE_INDEX = 0xFFu;
  static constexpr uint32_t INVALID_IRQ = 0xFFFFFFFFu;
  static constexpr uint8_t INVALID_DMA_CHANNEL = 0xFFu;
  static constexpr uint8_t INVALID_DMA_REQ = 0xFFu;
  static constexpr clock_name_t INVALID_CLOCK = static_cast<clock_name_t>(0xFFFFFFFFu);

  struct Resources
  {
    UART_Type* instance = nullptr;
    uint32_t irq = INVALID_IRQ;
    clock_name_t clock = INVALID_CLOCK;
    DMAV2_Type* dma = nullptr;
    DMAMUX_Type* dmamux = nullptr;
    uint32_t dma_irq = INVALID_IRQ;
    uint8_t rx_dma_channel = 0;
    uint8_t tx_dma_channel = 1;
    uint8_t rx_dma_req = INVALID_DMA_REQ;
    uint8_t tx_dma_req = INVALID_DMA_REQ;
    uint8_t index = INVALID_INSTANCE_INDEX;
  };

  HPMUART(Resources res, RawData dma_buff_rx, RawData dma_buff_tx,
          uint32_t tx_queue_size = 5,
          UART::Configuration config = {115200, UART::Parity::NO_PARITY, 8, 1},
          bool auto_board_init = true);

  HPMUART(UART_Type* instance, clock_name_t clock, uint32_t irq, RawData dma_buff_rx,
          RawData dma_buff_tx, uint8_t rx_dma_channel = 0, uint8_t tx_dma_channel = 1,
          uint32_t tx_queue_size = 5,
          UART::Configuration config = {115200, UART::Parity::NO_PARITY, 8, 1},
          bool auto_board_init = true);

  static Resources MakeResources(UART_Type* instance, clock_name_t clock = INVALID_CLOCK,
                                 uint32_t irq = INVALID_IRQ, uint8_t rx_dma_channel = 0,
                                 uint8_t tx_dma_channel = 1, DMAV2_Type* dma = nullptr,
                                 DMAMUX_Type* dmamux = nullptr,
                                 uint32_t dma_irq = INVALID_IRQ,
                                 uint8_t index = INVALID_INSTANCE_INDEX);

  static Resources NormalizeResources(Resources res);

  static uint8_t ResolveIndex(UART_Type* instance);
  static uint8_t ResolveIndexByIrq(uint32_t irq);
  static uint32_t ResolveIrq(UART_Type* instance);
  static clock_name_t ResolveClock(UART_Type* instance);
  static uint8_t ResolveRxDmaReq(UART_Type* instance);
  static uint8_t ResolveTxDmaReq(UART_Type* instance);

  static void WriteFun(WritePort& port, bool in_isr);

  // Config is published nonblockingly and applied after active TX reaches line idle.
  ErrorCode SetConfig(UART::Configuration config, bool in_isr = false) override;

  void SetRxDMA();
  void ProcessRxDMA(bool in_isr = true);
  void HandleUartInterrupt();
  void HandleDmaInterrupt(uint32_t status, uint8_t channel);
  void HandleTxDmaComplete(ErrorCode result);
  void HandleRxDmaEvent(uint32_t status);
  void HandleErrorInterrupt();

  static void OnInterrupt(uint8_t index);
  static void OnDmaInterrupt(DMAV2_Type* dma);
#if LIBXR_HPM_UART_HAS_DMA_MGR
  static void OnDmaMgrTcCallback(DMA_Type* base, uint32_t channel, void* user_data);
  static void OnDmaMgrHalfTcCallback(DMA_Type* base, uint32_t channel, void* user_data);
  static void OnDmaMgrErrorCallback(DMA_Type* base, uint32_t channel, void* user_data);
  static void OnDmaMgrAbortCallback(DMA_Type* base, uint32_t channel, void* user_data);
#endif

  ReadPort _read_port;
  WritePort _write_port;

  RawData dma_buff_rx_;
  DoubleBuffer dma_buff_tx_;

  size_t last_rx_pos_ = 0;
  Resources res_;

  Flag::Plain tx_busy_;

  uint32_t clock_hz_ = 0;
  size_t rx_drop_count_ = 0;

  static HPMUART* instances_[MAX_UART_INSTANCES];
  static HPMUART* rx_dma_map_[DMA_SOC_CHANNEL_NUM];
  static HPMUART* tx_dma_map_[DMA_SOC_CHANNEL_NUM];

 private:
  enum class ConfigState : uint32_t
  {
    EMPTY,
    RESERVED,
    PUBLISHED
  };
  static constexpr uint32_t EVENT_WRITE = 1U;
  static constexpr uint32_t EVENT_TX_DONE = 2U;
  static constexpr uint32_t EVENT_RX = 4U;
  static constexpr uint32_t EVENT_RX_ERROR = 8U;
  static constexpr uint32_t EVENT_CONFIG = 16U;
  static constexpr uint32_t EVENT_TX_IDLE = 32U;
  static constexpr uint32_t EVENT_TX_ERROR = 64U;
  static constexpr uint32_t EVENT_RX_IDLE = 128U;

  void Invoke(uint32_t events, bool in_isr);
  void HandleService(uint32_t events, bool in_isr);
  void FillTx(bool in_isr);
  void DrainRx(bool in_isr, bool idle_event);
  ErrorCode ValidateConfig(UART::Configuration config) const;
  ErrorCode ApplyConfig(UART::Configuration config);
  void TryApplyConfig(bool in_isr);
  SerializedService service_;
  std::atomic<ConfigState> config_state_{ConfigState::EMPTY};
  UART::Configuration pending_config_{};
  uint32_t ResolveClockHz();
  ErrorCode PrepareDmaChannels();
  ErrorCode StartTxDMA(size_t length);
  ErrorCode StartRxDMA();

  bool auto_board_init_;
#if LIBXR_HPM_UART_HAS_DMA_MGR
  bool dma_mgr_ready_ = false;
  dma_resource_t rx_dma_resource_{};
  dma_resource_t tx_dma_resource_{};
#endif
};

}  // namespace LibXR
