// ch32_i2c.hpp
#pragma once

#include "libxr.hpp"
#include DEF2STR(LIBXR_CH32_CONFIG_FILE)

#ifdef I2C
#undef I2C
#endif

#include "ch32_i2c_def.hpp"
#include "i2c.hpp"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "timebase.hpp"

namespace LibXR
{

class CH32I2C : public I2C
{
 public:
  // 统一地址语义：
  // - 7-bit 模式：slave_addr 传原始 7-bit (0x00..0x7F)，不带 R/W 位
  // - 10-bit 模式：slave_addr 传原始 10-bit (0x000..0x3FF)，不带 R/W 位
  // ten_bit_addr 必须是最后一个参数（按你的要求）
  CH32I2C(ch32_i2c_id_t id, RawData dma_buff, GPIO_TypeDef* scl_port, uint16_t scl_pin,
          GPIO_TypeDef* sda_port, uint16_t sda_pin, uint32_t pin_remap = 0,
          uint32_t dma_enable_min_size = 3, uint32_t default_clock_hz = 400000,
          bool ten_bit_addr = false);

  ErrorCode Read(uint16_t slave_addr, RawData read_data, ReadOperation& op,
                 bool in_isr) override;

  ErrorCode Write(uint16_t slave_addr, ConstRawData write_data, WriteOperation& op,
                  bool in_isr) override;

  ErrorCode MemRead(uint16_t slave_addr, uint16_t mem_addr, RawData read_data,
                    ReadOperation& op, MemAddrLength mem_addr_size, bool in_isr) override;

  ErrorCode MemWrite(uint16_t slave_addr, uint16_t mem_addr, ConstRawData write_data,
                     WriteOperation& op, MemAddrLength mem_addr_size,
                     bool in_isr) override;

  ErrorCode SetConfig(Configuration config) override;

  // DMA 回调（由 ch32_dma 模块转发）
  void RxDmaIRQHandler();
  void TxDmaIRQHandler();

  // I2C ER 中断回调
  void ErrorIRQHandler();

  static CH32I2C* map[CH32_I2C_NUMBER];

 private:
  // 超时：按“时间”而不是“循环次数”
  static constexpr uint32_t kDefaultTimeoutUs = 20000;  // 20ms

  inline bool DmaBusy() const
  {
    return (dma_rx_channel_->CNTR != 0) || (dma_tx_channel_->CNTR != 0) || busy_;
  }

  static inline uint8_t Addr7ToAddr8(uint16_t addr7)
  {
    // WCH SPL: I2C_Send7bitAddress 仅改 bit0(R/W)，不做 <<1
    ASSERT(addr7 <= 0x7F);
    return static_cast<uint8_t>(((addr7 & 0x7F) << 1) & 0xFE);
  }

  static inline uint16_t Addr10Clamp(uint16_t addr10)
  {
    ASSERT(addr10 <= 0x3FF);
    return static_cast<uint16_t>(addr10 & 0x3FF);
  }

  bool WaitEvent(uint32_t evt, uint32_t timeout_us = kDefaultTimeoutUs);
  bool WaitFlag(uint32_t flag, FlagStatus st, uint32_t timeout_us = kDefaultTimeoutUs);

  void ClearAddrFlag();

  ErrorCode MasterStartAndAddress(uint16_t slave_addr, uint8_t dir);

  // 10-bit 序列：先用写方向完成地址阶段；若最终是读，再重复起始并发读头
  ErrorCode MasterStartAndAddress10Bit(uint16_t addr10, uint8_t final_dir);

  ErrorCode SendMemAddr(uint16_t mem_addr, MemAddrLength len);

  ErrorCode PollingWriteBytes(const uint8_t* data, uint32_t len);
  ErrorCode PollingReadBytes(uint8_t* data, uint32_t len);

  void StartTxDma(uint32_t len);
  void StartRxDma(uint32_t len);

  void AbortTransfer(ErrorCode ec);

 public:
  I2C_TypeDef* instance_;
  DMA_Channel_TypeDef* dma_rx_channel_;
  DMA_Channel_TypeDef* dma_tx_channel_;
  ch32_i2c_id_t id_;
  uint32_t dma_enable_min_size_;

  RawData dma_buff_;

  ReadOperation read_op_;
  WriteOperation write_op_;
  RawData read_buff_;
  bool read_ = false;
  bool busy_ = false;

  GPIO_TypeDef* scl_port_;
  uint16_t scl_pin_;
  GPIO_TypeDef* sda_port_;
  uint16_t sda_pin_;

  Configuration cfg_{400000};

  bool ten_bit_addr_ = false;
};

}  // namespace LibXR
