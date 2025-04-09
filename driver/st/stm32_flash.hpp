#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "flash.hpp"
#include "libxr_type.hpp"
#include "main.h"

namespace LibXR
{

struct FlashSector
{
  uint32_t address;
  uint32_t size;
};

/**
 * @brief STM32Flash 通用类，构造时传入扇区列表，自动判断编程粒度。
 * @tparam SECTOR_COUNT 扇区数量
 * @tparam START_SECTOR 起始扇区索引（用于擦除编号）
 */
template <size_t SECTOR_COUNT, size_t START_SECTOR>
class STM32Flash : public Flash
{
 public:
  STM32Flash(const FlashSector (&sectors)[SECTOR_COUNT])
      : Flash(sectors[START_SECTOR - 1].size, DetermineMinWriteSize(),
              {reinterpret_cast<void*>(sectors[START_SECTOR - 1].address),
               sectors[SECTOR_COUNT - 1].address - sectors[START_SECTOR - 1].address +
                   sectors[SECTOR_COUNT - 1].size}),
        sectors_(sectors),
        base_address_(sectors[START_SECTOR - 1].address),
        program_type_(DetermineProgramType())
  {
  }

  ErrorCode Erase(size_t offset, size_t size) override
  {
    if (size == 0)
    {
      return ErrorCode::ARG_ERR;
    }

    uint32_t start_addr = base_address_ + offset;
    uint32_t end_addr = start_addr + size;

    HAL_FLASH_Unlock();

    for (size_t i = 0; i < SECTOR_COUNT; ++i)
    {
      const auto& sector = sectors_[i];
      if (sector.address + sector.size <= start_addr)
      {
        continue;
      }
      if (sector.address >= end_addr)
      {
        break;
      }
      FLASH_EraseInitTypeDef erase_init = {};

#if defined(FLASH_TYPEERASE_PAGES)  // STM32F1/G4... series
      erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
      erase_init.Page = i;
      erase_init.NbPages = 1;
      erase_init.Banks = FLASH_BANK_1;
#elif defined(FLASH_TYPEERASE_SECTORS)  // STM32F4/F7/H7... series
      erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
      erase_init.Sector = static_cast<uint32_t>(i);
      erase_init.NbSectors = 1;
      erase_init.Banks = FLASH_BANK_1;
#else
      return ErrorCode::NOT_SUPPORT;
#endif

      uint32_t error = 0;
      HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&erase_init, &error);
      if (status != HAL_OK || error != 0xFFFFFFFFU)
      {
        HAL_FLASH_Lock();
        return ErrorCode::FAILED;
      }
    }

    HAL_FLASH_Lock();
    return ErrorCode::OK;
  }

  ErrorCode Write(size_t offset, ConstRawData data) override
  {
    if (!data.addr_ || data.size_ == 0)
    {
      return ErrorCode::ARG_ERR;
    }

    uint32_t addr = base_address_ + offset;
    if (!IsInRange(addr, data.size_))
    {
      return ErrorCode::OUT_OF_RANGE;
    }

    HAL_FLASH_Unlock();

    const uint8_t* src = reinterpret_cast<const uint8_t*>(data.addr_);
    size_t written = 0;

    while (written < data.size_)
    {
      uint64_t word = 0xFFFFFFFFFFFFFFFF;
      size_t chunk_size = std::min<size_t>(min_write_size_, data.size_ - written);
      std::memcpy(&word, src + written, chunk_size);

      if (HAL_FLASH_Program(program_type_, addr + written, word) != HAL_OK)
      {
        HAL_FLASH_Lock();
        return ErrorCode::FAILED;
      }

      written += chunk_size;
    }

    HAL_FLASH_Lock();
    return ErrorCode::OK;
  }

 private:
  const FlashSector* sectors_;
  uint32_t base_address_;
  uint32_t program_type_;

  static constexpr uint32_t DetermineProgramType()
  {
#ifdef FLASH_TYPEPROGRAM_BYTE
    return FLASH_TYPEPROGRAM_BYTE;
#elif defined(FLASH_TYPEPROGRAM_HALFWORD)
    return FLASH_TYPEPROGRAM_HALFWORD;
#elif defined(FLASH_TYPEPROGRAM_WORD)
    return FLASH_TYPEPROGRAM_WORD;
#elif defined(FLASH_TYPEPROGRAM_DOUBLEWORD)
    return FLASH_TYPEPROGRAM_DOUBLEWORD;
#elif defined(FLASH_TYPEPROGRAM_FLASHWORD)
    return FLASH_TYPEPROGRAM_FLASHWORD;
#else
#error "No supported FLASH_TYPEPROGRAM_xxx defined"
#endif
  }

  static constexpr size_t DetermineMinWriteSize()
  {
#ifdef FLASH_TYPEPROGRAM_BYTE
    return 1;
#elif defined(FLASH_TYPEPROGRAM_HALFWORD)
    return 2;
#elif defined(FLASH_TYPEPROGRAM_WORD)
    return 4;
#elif defined(FLASH_TYPEPROGRAM_DOUBLEWORD)
    return 8;
#elif defined(FLASH_TYPEPROGRAM_FLASHWORD)
    return 32;
#else
#error "No supported FLASH_TYPEPROGRAM_xxx defined"
#endif
  }

  bool IsInRange(uint32_t addr, size_t size) const
  {
    uint32_t end = addr + size;
    for (size_t i = 0; i < SECTOR_COUNT; ++i)
    {
      const auto& s = sectors_[i];
      if (addr >= s.address && end <= s.address + s.size)
      {
        return true;
      }
    }
    return false;
  }
};

}  // namespace LibXR
