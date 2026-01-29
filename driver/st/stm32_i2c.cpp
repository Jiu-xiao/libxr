#include "stm32_i2c.hpp"

#ifdef HAL_I2C_MODULE_ENABLED

using namespace LibXR;

STM32I2C *STM32I2C::map[STM32_I2C_NUMBER] = {nullptr};

stm32_i2c_id_t STM32_I2C_GetID(I2C_TypeDef *hi2c)
{  // NOLINT
  if (hi2c == nullptr)
  {
    return stm32_i2c_id_t::STM32_I2C_ID_ERROR;
  }
#ifdef I2C1
  else if (hi2c == I2C1)
  {  // NOLINT
    return stm32_i2c_id_t::STM32_I2C1;
  }
#endif
#ifdef I2C2
  else if (hi2c == I2C2)
  {  // NOLINT
    return stm32_i2c_id_t::STM32_I2C2;
  }
#endif
#ifdef I2C3
  else if (hi2c == I2C3)
  {  // NOLINT
    return stm32_i2c_id_t::STM32_I2C3;
  }
#endif
#ifdef I2C4
  else if (hi2c == I2C4)
  {  // NOLINT
    return stm32_i2c_id_t::STM32_I2C4;
  }
#endif
#ifdef I2C5
  else if (hi2c == I2C5)
  {  // NOLINT
    return stm32_i2c_id_t::STM32_I2C5;
  }
#endif
#ifdef I2C6
  else if (hi2c == I2C6)
  {  // NOLINT
    return stm32_i2c_id_t::STM32_I2C6;
  }
#endif
#ifdef I2C7
  else if (hi2c == I2C7)
  {  // NOLINT
    return stm32_i2c_id_t::STM32_I2C7;
  }
#endif
#ifdef I2C8
  else if (hi2c == I2C8)
  {  // NOLINT
    return stm32_i2c_id_t::STM32_I2C8;
  }
#endif
  return stm32_i2c_id_t::STM32_I2C_ID_ERROR;
}

STM32I2C::STM32I2C(I2C_HandleTypeDef *hi2c, RawData dma_buff,
                   uint32_t dma_enable_min_size)
    : I2C(),
      id_(STM32_I2C_GetID(hi2c->Instance)),
      i2c_handle_(hi2c),
      dma_enable_min_size_(dma_enable_min_size),
      dma_buff_(dma_buff)
{
  map[id_] = this;
}

ErrorCode STM32I2C::Read(uint16_t slave_addr, RawData read_data, ReadOperation &op,
                         bool in_isr)
{
  if (i2c_handle_->State != HAL_I2C_STATE_READY)
  {
    return ErrorCode::BUSY;
  }

  read_ = true;

  if (read_data.size_ > dma_enable_min_size_)
  {
    read_op_ = op;
    HAL_I2C_Master_Receive_DMA(i2c_handle_, slave_addr,
                               reinterpret_cast<uint8_t *>(dma_buff_.addr_),
                               read_data.size_);
    read_buff_ = read_data;
    op.MarkAsRunning();
    if (op.type == ReadOperation::OperationType::BLOCK)
    {
      return op.data.sem_info.sem->Wait(op.data.sem_info.timeout);
    }
    return ErrorCode::OK;
  }
  else
  {
    auto ans = HAL_I2C_Master_Receive(i2c_handle_, slave_addr,
                                      reinterpret_cast<uint8_t *>(read_data.addr_),
                                      read_data.size_, 20) == HAL_OK
                   ? ErrorCode::OK
                   : ErrorCode::BUSY;
    op.UpdateStatus(in_isr, std::forward<ErrorCode>(ans));
    if (op.type == ReadOperation::OperationType::BLOCK)
    {
      return op.data.sem_info.sem->Wait(op.data.sem_info.timeout);
    }
    return ans;
  }
}

ErrorCode STM32I2C::Write(uint16_t slave_addr, ConstRawData write_data,
                          WriteOperation &op, bool in_isr)
{
  if (i2c_handle_->State != HAL_I2C_STATE_READY)
  {
    return ErrorCode::BUSY;
  }

  read_ = false;

  Memory::FastCopy(dma_buff_.addr_, write_data.addr_, write_data.size_);

  if (write_data.size_ > dma_enable_min_size_)
  {
    write_op_ = op;
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    SCB_CleanDCache_by_Addr(reinterpret_cast<uint32_t *>(dma_buff_.addr_),
                            write_data.size_);
#endif
    HAL_I2C_Master_Transmit_DMA(i2c_handle_, slave_addr,
                                reinterpret_cast<uint8_t *>(dma_buff_.addr_),
                                write_data.size_);
    op.MarkAsRunning();
    if (op.type == WriteOperation::OperationType::BLOCK)
    {
      return op.data.sem_info.sem->Wait(op.data.sem_info.timeout);
    }
    return ErrorCode::OK;
  }
  else
  {
    auto ans = HAL_I2C_Master_Transmit(i2c_handle_, slave_addr,
                                       reinterpret_cast<uint8_t *>(dma_buff_.addr_),
                                       write_data.size_, 20) == HAL_OK
                   ? ErrorCode::OK
                   : ErrorCode::BUSY;
    op.UpdateStatus(in_isr, std::forward<ErrorCode>(ans));
    if (op.type == WriteOperation::OperationType::BLOCK)
    {
      return op.data.sem_info.sem->Wait(op.data.sem_info.timeout);
    }
    return ans;
  }
}

ErrorCode STM32I2C::MemRead(uint16_t slave_addr, uint16_t mem_addr, RawData read_data,
                            ReadOperation &op, MemAddrLength mem_addr_size, bool in_isr)
{
  ASSERT(read_data.size_ <= dma_buff_.size_);

  if (i2c_handle_->State != HAL_I2C_STATE_READY)
  {
    return ErrorCode::BUSY;
  }

  read_ = true;

  if (read_data.size_ > dma_enable_min_size_)
  {
    read_op_ = op;
    HAL_I2C_Mem_Read_DMA(i2c_handle_, slave_addr, mem_addr,
                         mem_addr_size == MemAddrLength::BYTE_8 ? I2C_MEMADD_SIZE_8BIT
                                                                : I2C_MEMADD_SIZE_16BIT,
                         reinterpret_cast<uint8_t *>(dma_buff_.addr_), read_data.size_);
    read_buff_ = read_data;
    op.MarkAsRunning();
    if (op.type == ReadOperation::OperationType::BLOCK)
    {
      return op.data.sem_info.sem->Wait(op.data.sem_info.timeout);
    }
    return ErrorCode::OK;
  }
  else
  {
    auto ans =
        HAL_I2C_Mem_Read(i2c_handle_, slave_addr, mem_addr,
                         mem_addr_size == MemAddrLength::BYTE_8 ? I2C_MEMADD_SIZE_8BIT
                                                                : I2C_MEMADD_SIZE_16BIT,
                         reinterpret_cast<uint8_t *>(read_data.addr_), read_data.size_,
                         20) == HAL_OK
            ? ErrorCode::OK
            : ErrorCode::BUSY;

    op.UpdateStatus(in_isr, std::forward<ErrorCode>(ans));
    if (op.type == ReadOperation::OperationType::BLOCK)
    {
      return op.data.sem_info.sem->Wait(op.data.sem_info.timeout);
    }
    return ans;
  }
}

ErrorCode STM32I2C::MemWrite(uint16_t slave_addr, uint16_t mem_addr,
                             ConstRawData write_data, WriteOperation &op,
                             MemAddrLength mem_addr_size, bool in_isr)
{
  ASSERT(write_data.size_ <= dma_buff_.size_);

  if (i2c_handle_->State != HAL_I2C_STATE_READY)
  {
    return ErrorCode::BUSY;
  }

  read_ = false;

  Memory::FastCopy(dma_buff_.addr_, write_data.addr_, write_data.size_);

  if (write_data.size_ > dma_enable_min_size_)
  {
    write_op_ = op;
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    SCB_CleanDCache_by_Addr(reinterpret_cast<uint32_t *>(dma_buff_.addr_),
                            write_data.size_);
#endif
    HAL_I2C_Mem_Write_DMA(i2c_handle_, slave_addr, mem_addr,
                          mem_addr_size == MemAddrLength::BYTE_8 ? I2C_MEMADD_SIZE_8BIT
                                                                 : I2C_MEMADD_SIZE_16BIT,
                          reinterpret_cast<uint8_t *>(dma_buff_.addr_), write_data.size_);
    op.MarkAsRunning();
    if (op.type == WriteOperation::OperationType::BLOCK)
    {
      return op.data.sem_info.sem->Wait(op.data.sem_info.timeout);
    }
    return ErrorCode::OK;
  }
  else
  {
    auto ans =
        HAL_I2C_Mem_Write(i2c_handle_, slave_addr, mem_addr,
                          mem_addr_size == MemAddrLength::BYTE_8 ? I2C_MEMADD_SIZE_8BIT
                                                                 : I2C_MEMADD_SIZE_16BIT,
                          reinterpret_cast<uint8_t *>(dma_buff_.addr_), write_data.size_,
                          20) == HAL_OK
            ? ErrorCode::OK
            : ErrorCode::BUSY;

    op.UpdateStatus(in_isr, std::forward<ErrorCode>(ans));
    if (op.type == WriteOperation::OperationType::BLOCK)
    {
      return op.data.sem_info.sem->Wait(op.data.sem_info.timeout);
    }
    return ans;
  }
}

ErrorCode STM32I2C::SetConfig(Configuration config)
{
  if (HasClockSpeed<decltype(i2c_handle_)>::value)
  {
    SetClockSpeed<decltype(i2c_handle_)>(i2c_handle_, config);
  }
  else
  {
    return ErrorCode::NOT_SUPPORT;
  }

  if (HAL_I2C_Init(i2c_handle_) != HAL_OK)
  {
    return ErrorCode::INIT_ERR;
  }
  return ErrorCode::OK;
}

extern "C" void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
  STM32I2C *i2c = STM32I2C::map[STM32_I2C_GetID(hi2c->Instance)];
  if (i2c)
  {
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    SCB_InvalidateDCache_by_Addr(i2c->dma_buff_.addr_, i2c->read_buff_.size_);
#endif
    Memory::FastCopy(i2c->read_buff_.addr_, i2c->dma_buff_.addr_, i2c->read_buff_.size_);
    i2c->read_op_.UpdateStatus(true, ErrorCode::OK);
  }
}

extern "C" void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
  STM32I2C *i2c = STM32I2C::map[STM32_I2C_GetID(hi2c->Instance)];
  if (i2c)
  {
    i2c->write_op_.UpdateStatus(true, ErrorCode::OK);
  }
}

extern "C" void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
  STM32I2C *i2c = STM32I2C::map[STM32_I2C_GetID(hi2c->Instance)];
  if (i2c)
  {
    i2c->write_op_.UpdateStatus(true, ErrorCode::OK);
  }
}

extern "C" void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
  STM32I2C *i2c = STM32I2C::map[STM32_I2C_GetID(hi2c->Instance)];
  if (i2c)
  {
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    SCB_InvalidateDCache_by_Addr(i2c->dma_buff_.addr_, i2c->read_buff_.size_);
#endif
    Memory::FastCopy(i2c->read_buff_.addr_, i2c->dma_buff_.addr_, i2c->read_buff_.size_);
    i2c->read_op_.UpdateStatus(true, ErrorCode::OK);
  }
}

extern "C" void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
  STM32I2C *i2c = STM32I2C::map[STM32_I2C_GetID(hi2c->Instance)];

  if (i2c)
  {
    if (i2c->read_)
    {
      i2c->read_op_.UpdateStatus(true, ErrorCode::FAILED);
    }
    else
    {
      i2c->write_op_.UpdateStatus(true, ErrorCode::FAILED);
    }
  }
}

#endif
