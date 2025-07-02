#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "flash.hpp"
#include "libxr_def.hpp"
#include "libxr_type.hpp"
#include "main.h"

namespace LibXR
{

/**
 * @brief STM32Flash 通用类，构造时传入扇区列表，自动判断编程粒度。
 *
 */
struct FlashSector
{
  uint32_t address;  //< 扇区起始地址 / Start address of the sector
  uint32_t size;     //< 扇区大小 / Size of the sector
};

#ifndef __DOXYGEN__

template <typename, typename = void>
struct HasFlashPage : std::false_type
{
};

template <typename, typename = void>
struct HasFlashBank : std::false_type
{
};

template <typename T>
struct HasFlashPage<T, std::void_t<decltype(std::declval<T>().Page)>> : std::true_type
{
};

template <typename T>
struct HasFlashBank<T, std::void_t<decltype(std::declval<T>().Banks)>> : std::true_type
{
};

template <typename T>
// NOLINTNEXTLINE
typename std::enable_if<!HasFlashPage<T>::value>::type SetNbPages(T& init, uint32_t addr,
                                                                  uint32_t page)
{
  UNUSED(page);
  init.PageAddress = addr;
}

template <typename T>
// NOLINTNEXTLINE
typename std::enable_if<HasFlashPage<T>::value>::type SetNbPages(T& init, uint32_t addr,
                                                                 uint32_t page)
{
  UNUSED(addr);
  init.Page = page;
}

template <typename T>
// NOLINTNEXTLINE
typename std::enable_if<!HasFlashBank<T>::value>::type SetBanks(T& init)
{
  UNUSED(init);
}

template <typename T>
// NOLINTNEXTLINE
typename std::enable_if<HasFlashBank<T>::value>::type SetBanks(T& init)
{
  init.Banks = 1;
}

#endif

/**
 * @brief STM32Flash 通用类，构造时传入扇区列表，自动判断编程粒度。
 */
class STM32Flash : public Flash
{
 public:
  /**
   * @brief STM32Flash 类，构造时传入扇区列表，自动判断编程粒度。
   * @param sectors 扇区列表
   * @param sector_count 扇区数量
   * @param start_sector 起始扇区
   *
   */
  STM32Flash(const FlashSector* sectors, size_t sector_count, size_t start_sector);

  /**
   * @brief STM32Flash 类，自动取最后两个扇区
   *
   * @param sectors 扇区列表
   * @param sector_count 扇区数量
   */
  STM32Flash(const FlashSector* sectors, size_t sector_count)
      : STM32Flash(sectors, sector_count, sector_count - 1)
  {
  }

  ErrorCode Erase(size_t offset, size_t size) override;

  ErrorCode Write(size_t offset, ConstRawData data) override;

 private:
  const FlashSector* sectors_;
  uint32_t base_address_;
  uint32_t program_type_;
  size_t sector_count_;

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
    return FLASH_NB_32BITWORD_IN_FLASHWORD * 4;
#else
#error "No supported FLASH_TYPEPROGRAM_xxx defined"
#endif
  }

  bool IsInRange(uint32_t addr, size_t size) const;
};

}  // namespace LibXR
