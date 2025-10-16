#include "ch32_spi.hpp"

#include <cstring>

#include "ch32_dma.hpp"
#include "ch32_gpio.hpp"

using namespace LibXR;

CH32SPI *CH32SPI::map[ch32_spi_id_t::CH32_SPI_NUMBER] = {nullptr};

CH32SPI::CH32SPI(ch32_spi_id_t id, RawData dma_rx, RawData dma_tx, GPIO_TypeDef *sck_port,
                 uint16_t sck_pin, GPIO_TypeDef *miso_port, uint16_t miso_pin,
                 GPIO_TypeDef *mosi_port, uint16_t mosi_pin, uint32_t pin_remap,
                 bool master_mode, bool firstbit_msb, uint16_t prescaler,
                 uint32_t dma_enable_min_size, SPI::Configuration config)
    : SPI(),
      dma_buff_rx_(dma_rx),
      dma_buff_tx_(dma_tx),
      instance_(CH32_SPI_GetInstanceID(id)),
      dma_rx_channel_(CH32_SPI_RX_DMA_CHANNEL_MAP[id]),
      dma_tx_channel_(CH32_SPI_TX_DMA_CHANNEL_MAP[id]),
      id_(id),
      dma_enable_min_size_(dma_enable_min_size),
      mode_(master_mode ? SPI_Mode_Master : SPI_Mode_Slave),
      datasize_(SPI_DataSize_8b),
      firstbit_(firstbit_msb ? SPI_FirstBit_MSB : SPI_FirstBit_LSB),
      prescaler_(prescaler),
      sck_port_(sck_port),
      sck_pin_(sck_pin),
      miso_port_(miso_port),
      miso_pin_(miso_pin),
      mosi_port_(mosi_port),
      mosi_pin_(mosi_pin)
{
  ASSERT(instance_ != nullptr);
  ASSERT(dma_buff_tx_.size_ >= 1);
  // 读至少给 1 字节，便于 MemRead 的“命令 + N 字节” DMA 长度
  ASSERT(dma_buff_rx_.size_ >= 1);

  map[id] = this;

  // === 时钟 ===
  if (CH32_SPI_APB_MAP[id] == 1)
  {
    RCC_APB1PeriphClockCmd(CH32_SPI_RCC_PERIPH_MAP[id], ENABLE);
  }
  else if (CH32_SPI_APB_MAP[id] == 2)
  {
    RCC_APB2PeriphClockCmd(CH32_SPI_RCC_PERIPH_MAP[id], ENABLE);
  }
  else
  {
    ASSERT(false);
  }
  RCC_AHBPeriphClockCmd(CH32_SPI_RCC_PERIPH_MAP_DMA[id], ENABLE);

  // === GPIO ===
  {
    GPIO_InitTypeDef gpio = {};
    gpio.GPIO_Speed = GPIO_Speed_50MHz;

    // SCK
    RCC_APB2PeriphClockCmd(CH32GetGPIOPeriph(sck_port_), ENABLE);
    if (mode_ == SPI_Mode_Master)
    {
      gpio.GPIO_Pin = sck_pin_;
      gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    }
    else
    {
      gpio.GPIO_Pin = sck_pin_;
      gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    }
    GPIO_Init(sck_port_, &gpio);

    // MISO
    RCC_APB2PeriphClockCmd(CH32GetGPIOPeriph(miso_port_), ENABLE);
    if (mode_ == SPI_Mode_Master)
    {
      gpio.GPIO_Pin = miso_pin_;
      gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    }
    else
    {
      gpio.GPIO_Pin = miso_pin_;
      gpio.GPIO_Mode = GPIO_Mode_AF_PP;  // 从机输出
    }
    GPIO_Init(miso_port_, &gpio);

    // MOSI
    RCC_APB2PeriphClockCmd(CH32GetGPIOPeriph(mosi_port_), ENABLE);
    if (mode_ == SPI_Mode_Master)
    {
      gpio.GPIO_Pin = mosi_pin_;
      gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    }
    else
    {
      gpio.GPIO_Pin = mosi_pin_;
      gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    }
    GPIO_Init(mosi_port_, &gpio);

    if (pin_remap != 0)
    {
      RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
      GPIO_PinRemapConfig(pin_remap, ENABLE);
    }
  }

  // === SPI 基本配置 ===
  {
    SPI_InitTypeDef init = {};
    init.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    init.SPI_Mode = mode_;
    init.SPI_DataSize = datasize_;
    init.SPI_CPOL =
        (config.clock_polarity == SPI::ClockPolarity::LOW) ? SPI_CPOL_Low : SPI_CPOL_High;
    init.SPI_CPHA =
        (config.clock_phase == SPI::ClockPhase::EDGE_1) ? SPI_CPHA_1Edge : SPI_CPHA_2Edge;
    init.SPI_NSS = nss_;
    init.SPI_BaudRatePrescaler = prescaler_;
    init.SPI_FirstBit = firstbit_;
    init.SPI_CRCPolynomial = 7;

    SPI_Init(instance_, &init);
    SPI_Cmd(instance_, ENABLE);
  }

  // === DMA 通道基础配置 & 回调注册（启动传输时再设定 MADDR/CNTR） ===
  {
    // RX
    {
      ch32_dma_callback_t cb = [](void *arg)
      { reinterpret_cast<CH32SPI *>(arg)->RxDmaIRQHandler(); };
      CH32_DMA_RegisterCallback(CH32_DMA_GetID(dma_rx_channel_), cb, this);

      DMA_InitTypeDef di = {};
      DMA_DeInit(dma_rx_channel_);
      di.DMA_PeripheralBaseAddr = (uint32_t)&instance_->DATAR;
      di.DMA_MemoryBaseAddr = (uint32_t)dma_buff_rx_.addr_;
      di.DMA_DIR = DMA_DIR_PeripheralSRC;
      di.DMA_BufferSize = 0;  // run-time 填
      di.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
      di.DMA_MemoryInc = DMA_MemoryInc_Enable;
      di.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
      di.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
      di.DMA_Mode = DMA_Mode_Normal;
      di.DMA_Priority = DMA_Priority_High;
      di.DMA_M2M = DMA_M2M_Disable;
      DMA_Init(dma_rx_channel_, &di);
      DMA_ITConfig(dma_rx_channel_, DMA_IT_TC, ENABLE);
      NVIC_EnableIRQ(CH32_DMA_IRQ_MAP[CH32_DMA_GetID(dma_rx_channel_)]);
    }
    // TX
    {
      ch32_dma_callback_t cb = [](void *arg)
      { reinterpret_cast<CH32SPI *>(arg)->TxDmaIRQHandler(); };
      CH32_DMA_RegisterCallback(CH32_DMA_GetID(dma_tx_channel_), cb, this);

      DMA_InitTypeDef di = {};
      DMA_DeInit(dma_tx_channel_);
      di.DMA_PeripheralBaseAddr = (uint32_t)&instance_->DATAR;
      di.DMA_MemoryBaseAddr = 0;  // run-time 填
      di.DMA_DIR = DMA_DIR_PeripheralDST;
      di.DMA_BufferSize = 0;  // run-time 填
      di.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
      di.DMA_MemoryInc = DMA_MemoryInc_Enable;
      di.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
      di.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
      di.DMA_Mode = DMA_Mode_Normal;
      di.DMA_Priority = DMA_Priority_VeryHigh;
      di.DMA_M2M = DMA_M2M_Disable;
      DMA_Init(dma_tx_channel_, &di);
      DMA_ITConfig(dma_tx_channel_, DMA_IT_TC, ENABLE);
      NVIC_EnableIRQ(CH32_DMA_IRQ_MAP[CH32_DMA_GetID(dma_tx_channel_)]);
    }
  }
}

// === SetConfig：仅更新极性/相位，保持其它参数 ===
ErrorCode CH32SPI::SetConfig(SPI::Configuration config)
{
  SPI_Cmd(instance_, DISABLE);
  SPI_I2S_DeInit(instance_);

  SPI_InitTypeDef init = {};
  init.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
  init.SPI_Mode = mode_;
  init.SPI_DataSize = datasize_;
  init.SPI_CPOL =
      (config.clock_polarity == SPI::ClockPolarity::LOW) ? SPI_CPOL_Low : SPI_CPOL_High;
  init.SPI_CPHA =
      (config.clock_phase == SPI::ClockPhase::EDGE_1) ? SPI_CPHA_1Edge : SPI_CPHA_2Edge;
  init.SPI_NSS = nss_;
  init.SPI_BaudRatePrescaler = prescaler_;
  init.SPI_FirstBit = firstbit_;
  init.SPI_CRCPolynomial = 7;
  SPI_Init(instance_, &init);
  SPI_Cmd(instance_, ENABLE);
  return ErrorCode::OK;
}

ErrorCode CH32SPI::PollingTransfer(uint8_t *rx, const uint8_t *tx, uint32_t len)
{
  for (uint32_t i = 0; i < len; ++i)
  {
    // 等待 TXE
    while (SPI_I2S_GetFlagStatus(instance_, SPI_I2S_FLAG_TXE) == RESET)
    {
    }
    SPI_I2S_SendData(instance_, tx ? tx[i] : 0xFF);

    // 等待 RXNE
    while (SPI_I2S_GetFlagStatus(instance_, SPI_I2S_FLAG_RXNE) == RESET)
    {
    }
    uint16_t d = SPI_I2S_ReceiveData(instance_);
    if (rx) rx[i] = static_cast<uint8_t>(d & 0xFF);
  }
  return ErrorCode::OK;
}

ErrorCode CH32SPI::ReadAndWrite(RawData read_data, ConstRawData write_data,
                                OperationRW &op)
{
  const uint32_t rsz = read_data.size_;
  const uint32_t wsz = write_data.size_;
  const uint32_t need = (rsz > wsz) ? rsz : wsz;

  if (need == 0)
  {
    if (op.type != OperationRW::OperationType::BLOCK)
      op.UpdateStatus(false, ErrorCode::OK);
    return ErrorCode::OK;
  }

  if (DmaBusy()) return ErrorCode::BUSY;

  if (need > dma_enable_min_size_)
  {
    ASSERT(dma_buff_tx_.size_ >= need);
    ASSERT(dma_buff_rx_.size_ >= need);

    mem_read_ = false;
    read_buff_ = read_data;
    rw_op_ = op;
    busy_ = true;

    PrepareTxBuffer(write_data, need);

    // 配置 DMA 长度与地址并启动
    StartDmaDuplex(need);

    op.MarkAsRunning();
    if (op.type == OperationRW::OperationType::BLOCK)
    {
      return op.data.sem_info.sem->Wait(op.data.sem_info.timeout);
    }
    return ErrorCode::OK;
  }
  else
  {
    // 轮询路径
    uint8_t *rx = static_cast<uint8_t *>(dma_buff_rx_.addr_);
    uint8_t *tx = static_cast<uint8_t *>(dma_buff_tx_.addr_);

    // 准备 tx
    if (wsz)
    {
      memcpy(tx, write_data.addr_, wsz);
      if (need > wsz) memset(tx + wsz, 0xFF, need - wsz);
    }
    else
    {
      memset(tx, 0xFF, need);
    }

    ErrorCode ec = PollingTransfer(rsz ? rx : nullptr, tx, need);

    if (rsz)
    {
      memcpy(read_data.addr_, rx, rsz);
    }

    if (op.type == OperationRW::OperationType::BLOCK)
    {
      return ec;
    }

    op.UpdateStatus(false, ec);

    return ec;
  }
}

ErrorCode CH32SPI::MemRead(uint16_t reg, RawData read_data, OperationRW &op)
{
  const uint32_t n = read_data.size_;
  if (n == 0)
  {
    if (op.type != OperationRW::OperationType::BLOCK)
      op.UpdateStatus(false, ErrorCode::OK);
    return ErrorCode::OK;
  }

  if (DmaBusy()) return ErrorCode::BUSY;

  const uint32_t total = n + 1;  // 含首字节命令
  if (total > dma_enable_min_size_)
  {
    ASSERT(dma_buff_tx_.size_ >= total);
    ASSERT(dma_buff_rx_.size_ >= total);

    uint8_t *tx = static_cast<uint8_t *>(dma_buff_tx_.addr_);
    tx[0] = static_cast<uint8_t>(reg | 0x80);
    memset(tx + 1, 0xFF, n);

    mem_read_ = true;
    read_buff_ = read_data;
    rw_op_ = op;
    busy_ = true;

    StartDmaDuplex(total);

    op.MarkAsRunning();
    if (op.type == OperationRW::OperationType::BLOCK)
    {
      return op.data.sem_info.sem->Wait(op.data.sem_info.timeout);
    }
    return ErrorCode::OK;
  }
  else
  {
    // 轮询
    uint8_t *rx = static_cast<uint8_t *>(dma_buff_rx_.addr_);
    uint8_t *tx = static_cast<uint8_t *>(dma_buff_tx_.addr_);
    tx[0] = static_cast<uint8_t>(reg | 0x80);
    memset(tx + 1, 0xFF, n);

    ErrorCode ec = PollingTransfer(rx, tx, n + 1);
    memcpy(read_data.addr_, rx + 1, n);

    if (op.type == OperationRW::OperationType::BLOCK)
    {
      return ec;
    }

    op.UpdateStatus(false, ec);

    return ec;
  }
}

ErrorCode CH32SPI::MemWrite(uint16_t reg, ConstRawData write_data, OperationRW &op)
{
  const uint32_t n = write_data.size_;
  if (n == 0)
  {
    if (op.type != OperationRW::OperationType::BLOCK)
      op.UpdateStatus(false, ErrorCode::OK);
    return ErrorCode::OK;
  }

  if (DmaBusy()) return ErrorCode::BUSY;

  const uint32_t total = n + 1;
  if (total > dma_enable_min_size_)
  {
    ASSERT(dma_buff_tx_.size_ >= total);
    ASSERT(dma_buff_rx_.size_ >= total);  // RX 用于丢弃

    uint8_t *tx = static_cast<uint8_t *>(dma_buff_tx_.addr_);
    tx[0] = static_cast<uint8_t>(reg & 0x7F);
    memcpy(tx + 1, write_data.addr_, n);

    mem_read_ = false;
    read_buff_ = {nullptr, 0};
    rw_op_ = op;
    busy_ = true;

    StartDmaDuplex(total);

    op.MarkAsRunning();
    if (op.type == OperationRW::OperationType::BLOCK)
    {
      return op.data.sem_info.sem->Wait(op.data.sem_info.timeout);
    }
    return ErrorCode::OK;
  }
  else
  {
    // 轮询
    uint8_t *rx = static_cast<uint8_t *>(dma_buff_rx_.addr_);
    uint8_t *tx = static_cast<uint8_t *>(dma_buff_tx_.addr_);
    tx[0] = static_cast<uint8_t>(reg & 0x7F);
    memcpy(tx + 1, write_data.addr_, n);

    ErrorCode ec = PollingTransfer(rx, tx, n + 1);

    if (op.type == OperationRW::OperationType::BLOCK)
    {
      return ec;
    }

    op.UpdateStatus(false, ec);

    return ec;
  }
}

void CH32SPI::PrepareTxBuffer(ConstRawData write_data, uint32_t need_len, uint32_t prefix,
                              uint8_t dummy)
{
  uint8_t *tx = static_cast<uint8_t *>(dma_buff_tx_.addr_);
  if (write_data.size_ > 0)
  {
    const uint32_t copy =
        (write_data.size_ > need_len - prefix) ? (need_len - prefix) : write_data.size_;
    memcpy(tx + prefix, write_data.addr_, copy);
    if (prefix + copy < need_len)
    {
      memset(tx + prefix + copy, dummy, need_len - prefix - copy);
    }
  }
  else
  {
    memset(tx + prefix, dummy, need_len - prefix);
  }
}

void CH32SPI::StartDmaDuplex(uint32_t count)
{
  // 配置长度与地址
  dma_rx_channel_->MADDR = reinterpret_cast<uint32_t>(dma_buff_rx_.addr_);
  dma_rx_channel_->CNTR = count;

  dma_tx_channel_->MADDR = reinterpret_cast<uint32_t>(dma_buff_tx_.addr_);
  dma_tx_channel_->CNTR = count;

  SPI_I2S_DMACmd(instance_, SPI_I2S_DMAReq_Rx, ENABLE);
  SPI_I2S_DMACmd(instance_, SPI_I2S_DMAReq_Tx, ENABLE);

  DMA_Cmd(dma_rx_channel_, ENABLE);
  DMA_Cmd(dma_tx_channel_, ENABLE);
}

void CH32SPI::StopDma()
{
  DMA_Cmd(dma_tx_channel_, DISABLE);
  DMA_Cmd(dma_rx_channel_, DISABLE);
}

void CH32SPI::RxDmaIRQHandler()
{
  // 只在 TC 时处理
  if (DMA_GetITStatus(CH32_SPI_RX_DMA_IT_MAP[id_]) == RESET) return;

  DMA_ClearITPendingBit(CH32_SPI_RX_DMA_IT_MAP[id_]);

  SPI_I2S_DMACmd(instance_, SPI_I2S_DMAReq_Rx, DISABLE);
  DMA_Cmd(dma_rx_channel_, DISABLE);

  // 拷贝读数据（若有）
  if (read_buff_.size_ > 0)
  {
    uint8_t *rx = static_cast<uint8_t *>(dma_buff_rx_.addr_);
    if (mem_read_)
    {
      // 丢首字节命令
      memcpy(read_buff_.addr_, rx + 1, read_buff_.size_);
    }
    else
    {
      memcpy(read_buff_.addr_, rx, read_buff_.size_);
    }
  }

  busy_ = false;
  rw_op_.UpdateStatus(true, ErrorCode::OK);
}

void CH32SPI::TxDmaIRQHandler()
{
  if (DMA_GetITStatus(CH32_SPI_TX_DMA_IT_MAP[id_]) == RESET) return;
  DMA_ClearITPendingBit(CH32_SPI_TX_DMA_IT_MAP[id_]);

  SPI_I2S_DMACmd(instance_, SPI_I2S_DMAReq_Tx, DISABLE);
  DMA_Cmd(dma_tx_channel_, DISABLE);
}
