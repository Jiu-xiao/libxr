#pragma once

#include "libxr.hpp"
#include DEF2STR(LIBXR_CH32_CONFIG_FILE)

#include <cstring>

#include "ch32_spi_def.hpp"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "spi.hpp"

namespace LibXR
{

class CH32SPI : public SPI
{
 public:
  CH32SPI(ch32_spi_id_t id, RawData dma_rx, RawData dma_tx, GPIO_TypeDef* sck_port,
          uint16_t sck_pin, GPIO_TypeDef* miso_port, uint16_t miso_pin,
          GPIO_TypeDef* mosi_port, uint16_t mosi_pin, uint32_t pin_remap = 0,
          bool master_mode = true, bool firstbit_msb = true,
          uint16_t prescaler = SPI_BaudRatePrescaler_64, uint32_t dma_enable_min_size = 3,
          SPI::Configuration config = {SPI::ClockPolarity::LOW, SPI::ClockPhase::EDGE_1});

  // === SPI 基类必需接口 ===
  ErrorCode ReadAndWrite(RawData read_data, ConstRawData write_data, OperationRW& op,
                         bool in_isr) override;
  ErrorCode SetConfig(SPI::Configuration config) override;
  ErrorCode MemRead(uint16_t reg, RawData read_data, OperationRW& op,
                    bool in_isr) override;
  ErrorCode MemWrite(uint16_t reg, ConstRawData write_data, OperationRW& op,
                     bool in_isr) override;

  ErrorCode Transfer(size_t size, OperationRW& op, bool in_isr) override;
  uint32_t GetMaxBusSpeed() const override;
  SPI::Prescaler GetMaxPrescaler() const override;

  // 中断回调
  void RxDmaIRQHandler();
  void TxDmaIRQHandler();

  // 低级轮询传输（内部使用）
  ErrorCode PollingTransfer(uint8_t* rx, const uint8_t* tx, uint32_t len);

  static CH32SPI* map[CH32_SPI_NUMBER];

 private:
  inline bool DmaBusy() const
  {
    return (dma_rx_channel_->CNTR != 0) || (dma_tx_channel_->CNTR != 0) || busy_;
  }

  void StartDmaDuplex(uint32_t count);
  void StopDma();

  void PrepareTxBuffer(ConstRawData write_data, uint32_t need_len, uint32_t prefix = 0,
                       uint8_t dummy = 0x00);

  static SPI::Prescaler MapCH32PrescalerToEnum(uint16_t p);
  static bool MapEnumToCH32Prescaler(SPI::Prescaler p, uint16_t& out);

 public:
  SPI_TypeDef* instance_;
  DMA_Channel_TypeDef* dma_rx_channel_;
  DMA_Channel_TypeDef* dma_tx_channel_;
  ch32_spi_id_t id_;
  uint32_t dma_enable_min_size_;

  // 运行时缓存 / 状态
  OperationRW rw_op_;
  RawData read_buff_;
  bool mem_read_ = false;
  bool busy_ = false;

  // SPI 配置缓存
  uint16_t mode_;       // SPI_Mode_Master / Slave
  uint16_t datasize_;   // 默认 SPI_DataSize_8b
  uint16_t firstbit_;   // SPI_FirstBit_MSB/LSB
  uint16_t prescaler_;  // SPI_BaudRatePrescaler_x
  uint16_t nss_ = SPI_NSS_Soft;

  // GPIO
  GPIO_TypeDef* sck_port_;
  uint16_t sck_pin_;
  GPIO_TypeDef* miso_port_;
  uint16_t miso_pin_;
  GPIO_TypeDef* mosi_port_;
  uint16_t mosi_pin_;
};

}  // namespace LibXR
