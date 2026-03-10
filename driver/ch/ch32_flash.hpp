#pragma once

#include <cstdint>
#include <cstring>

#include "flash.hpp"
#include "libxr_def.hpp"
#include "libxr_type.hpp"
#include DEF2STR(LIBXR_CH32_CONFIG_FILE)

namespace LibXR
{

struct FlashSector
{
  uint32_t address;  ///< 扇区起始地址 / Sector base address
  uint32_t size;     ///< 扇区大小（字节） / Sector size in bytes
};

/**
 * @brief CH32 闪存驱动实现 / CH32 flash driver implementation
 */
class CH32Flash : public Flash
{
 public:
  /**
   * @brief 构造闪存对象 / Construct flash object
   */
  CH32Flash(const FlashSector* sectors, size_t sector_count, size_t start_sector);
  /**
   * @brief 构造并使用默认起始扇区 / Construct with default start sector
   */
  CH32Flash(const FlashSector* sectors, size_t sector_count)
      : CH32Flash(sectors, sector_count, sector_count - 1)
  {
  }

  ErrorCode Erase(size_t offset, size_t size) override;
  ErrorCode Write(size_t offset, ConstRawData data) override;

  static constexpr size_t MinWriteSize()
  {
    return 2;
  }  ///< 最小写入粒度（半字） / Minimum write size (half-word)

  static constexpr uint32_t PageSize()
  {
    return 256;
  }  ///< Page erase size in fast erase mode / 快速擦除页大小

 private:
  const FlashSector* sectors_;
  uint32_t base_address_;
  size_t sector_count_;

  bool IsInRange(uint32_t addr, size_t size) const;
  static inline void ClearFlashFlagsOnce();
};

}  // namespace LibXR
