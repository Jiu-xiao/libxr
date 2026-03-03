#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "driver/gpio.h"
#include "esp_intr_alloc.h"
#include "hal/spi_ll.h"
#include "hal/spi_types.h"
#include "soc/soc_caps.h"
#include "spi.hpp"

namespace LibXR
{

class ESP32SPI : public SPI
{
 public:
  ESP32SPI(
      spi_host_device_t host, int sclk_pin, int miso_pin, int mosi_pin, RawData dma_rx,
      RawData dma_tx,
      SPI::Configuration config = {
          SPI::ClockPolarity::LOW,
          SPI::ClockPhase::EDGE_1,
          SPI::Prescaler::DIV_8,
          false,
      },
      uint32_t dma_enable_min_size = 3U, bool enable_dma = true);

  ~ESP32SPI();

  ErrorCode ReadAndWrite(RawData read_data, ConstRawData write_data, OperationRW& op,
                         bool in_isr = false) override;

  ErrorCode SetConfig(SPI::Configuration config) override;

  ErrorCode MemRead(uint16_t reg, RawData read_data, OperationRW& op,
                    bool in_isr = false) override;

  ErrorCode MemWrite(uint16_t reg, ConstRawData write_data, OperationRW& op,
                     bool in_isr = false) override;

  ErrorCode Transfer(size_t size, OperationRW& op, bool in_isr = false) override;

  uint32_t GetMaxBusSpeed() const override;

  Prescaler GetMaxPrescaler() const override;

  RawData GetRxBuffer();

  RawData GetTxBuffer();

 private:
  static constexpr size_t kMaxPollingTransferBytes = SOC_SPI_MAXIMUM_BUFFER_SIZE;
  static constexpr size_t kMaxDmaTransferBytes = SPI_LL_DMA_MAX_BIT_LEN / 8U;

  bool Acquire();

  void Release();

  ErrorCode InitializeHardware();

  void DeinitializeHardware();

  ErrorCode ConfigurePins();

  ErrorCode ResolveClockSource(uint32_t& source_hz);

  ErrorCode InstallInterrupt();

  void RemoveInterrupt();

  ErrorCode InitDmaBackend();

  void DeinitDmaBackend();

  static void SpiIsrEntry(void* arg);

  void HandleInterrupt();

  void FinishAsync(bool in_isr, ErrorCode ec);

  bool CanUseDma(size_t size) const;

  ErrorCode StartAsyncTransfer(const uint8_t* tx, uint8_t* rx, size_t size, bool enable_rx,
                               RawData read_back, bool mem_read, OperationRW& op,
                               bool& started);

  void ConfigureTransferRegisters(size_t size);

  ErrorCode ExecuteChunk(const uint8_t* tx, uint8_t* rx, size_t size, bool enable_rx);

  ErrorCode ExecuteTransfer(const uint8_t* tx, uint8_t* rx, size_t size, bool enable_rx);

  bool UseLocalDoubleBuffer() const;

  void SwitchBufferLocal();

  spi_host_device_t host_;
  spi_dev_t* hw_ = nullptr;
  int sclk_pin_;
  int miso_pin_;
  int mosi_pin_;
  uint32_t source_clock_hz_ = 0;
  std::atomic<bool> busy_{false};
  bool initialized_ = false;
  uint32_t dma_enable_min_size_ = 3U;
  bool dma_requested_ = true;
  RawData dma_rx_raw_ = {nullptr, 0};
  RawData dma_tx_raw_ = {nullptr, 0};
  size_t dbuf_rx_block_size_ = 0;
  size_t dbuf_tx_block_size_ = 0;
  uint8_t dbuf_active_block_ = 0;
  bool dbuf_enabled_ = false;
  intr_handle_t intr_handle_ = nullptr;
  bool intr_installed_ = false;
  bool dma_enabled_ = false;
  void* dma_ctx_ = nullptr;
  size_t dma_max_transfer_bytes_ = 0;
  bool async_running_ = false;
  size_t async_dma_size_ = 0;
  bool async_dma_rx_enabled_ = false;
  bool mem_read_ = false;
  RawData read_back_ = {nullptr, 0};
  OperationRW rw_op_;
};

}  // namespace LibXR
