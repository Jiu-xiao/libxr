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
  uint32_t address;  // 扇区起始地址
  uint32_t size;     // 扇区大小
};

class CH32Flash : public Flash
{
 public:
  CH32Flash(const FlashSector* sectors, size_t sector_count, size_t start_sector);
  CH32Flash(const FlashSector* sectors, size_t sector_count)
      : CH32Flash(sectors, sector_count, sector_count - 1)
  {
  }

  ErrorCode Erase(size_t offset, size_t size) override;
  ErrorCode Write(size_t offset, ConstRawData data) override;

  static constexpr size_t MinWriteSize() { return 2; }  // 普通编程：半字

  static constexpr uint32_t PageSize() { return 256; }  // 快速页擦除：256字节

 private:
  const FlashSector* sectors_;
  uint32_t base_address_;
  size_t sector_count_;

  bool IsInRange(uint32_t addr, size_t size) const;
  static inline void ClearFlashFlagsOnce();
};

}  // namespace LibXR