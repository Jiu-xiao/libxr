// NOLINTBEGIN(cppcoreguidelines-pro-type-cstyle-cast,performance-no-int-to-ptr)
#include "ch32_flash.hpp"

using namespace LibXR;

// WCH GCC15 is sensitive to the exact self-programming code shape on CH32V3.
// Keep the erase/write hot loops behind hard noinline boundaries, but leave the
// surrounding unlock/clock/flag choreography in the original call sites.
// WCH GCC15 对 CH32V3 自擦写路径的代码形状很敏感。
// 这里把擦除/写入热循环放到明确的 noinline 边界后面，
// 但解锁、降频、清标志这些外围时序仍留在原来的调用点。
extern "C" __attribute__((noinline)) ErrorCode CH32FlashWriteHotPath(uint32_t start_addr,
                                                                     uint32_t end_addr,
                                                                     const uint8_t* src);

namespace
{
using CH32FlashEraseHotPathFn = ErrorCode (*)(uint32_t erase_begin, uint32_t erase_end);
using CH32FlashWriteHotLoopFn = ErrorCode (*)(uint32_t start_addr, uint32_t end_addr,
                                              const uint8_t* src);
using CH32FlashWritePageFn = ErrorCode (*)(uint32_t start_addr, uint32_t end_addr,
                                           const uint8_t* src);

struct CH32FlashHotPaths
{
  CH32FlashEraseHotPathFn erase = nullptr;
  CH32FlashWriteHotLoopFn write_halfword = nullptr;
  CH32FlashWritePageFn write_page = nullptr;
};

constexpr size_t routine_align = 16u;
constexpr size_t ch32_flash_erase_hot_path_size = 0x78u;
constexpr size_t ch32_flash_write_hot_loop_size = 0xD2u;
constexpr size_t ch32_flash_write_page_size = 0x13Cu;

// Verified CH32V30x hot-path machine code blob kept directly in SRAM.
// `.S` remains in the tree as a readable source/reference for future toolchain
// refreshes, but runtime no longer depends on reassembling and memcpy-ing it.
alignas(routine_align) static uint8_t
    g_ch32_flash_erase_hot[ch32_flash_erase_hot_path_size] = {
        0x63, 0x79, 0xb5, 0x04, 0x01, 0x76, 0x7d, 0x16, 0xb7, 0x26, 0x02, 0x40,
        0x37, 0x08, 0x02, 0x00, 0x93, 0x08, 0x00, 0x02, 0x9c, 0x4a, 0x37, 0x47,
        0x0f, 0x00, 0x13, 0x07, 0x17, 0x24, 0xb3, 0xe7, 0x07, 0x01, 0x9c, 0xca,
        0xc8, 0xca, 0x9c, 0x4a, 0x93, 0xe7, 0x07, 0x04, 0x9c, 0xca, 0x11, 0xa0,
        0x1d, 0xc3, 0xdc, 0x46, 0x7d, 0x17, 0x85, 0x8b, 0xe5, 0xff, 0xdc, 0x46,
        0xc1, 0x8b, 0x9d, 0xe3, 0x9c, 0x4a, 0x13, 0x05, 0x05, 0x10, 0xf1, 0x8f,
        0x9c, 0xca, 0x23, 0xa6, 0x16, 0x01, 0xe3, 0x63, 0xb5, 0xfc, 0x01, 0x45,
        0x82, 0x80, 0x9c, 0x4a, 0x01, 0x77, 0x7d, 0x17, 0xf9, 0x8f, 0x9c, 0xca,
        0x51, 0x55, 0x82, 0x80, 0x9c, 0x4a, 0x01, 0x77, 0x7d, 0x17, 0xf9, 0x8f,
        0x9c, 0xca, 0x93, 0x07, 0x00, 0x03, 0xdc, 0xc6, 0x71, 0x55, 0x82, 0x80};

alignas(routine_align) static uint8_t
    g_ch32_flash_write_hot[ch32_flash_write_hot_loop_size] = {
        0x13, 0x78, 0xe5, 0xff, 0x13, 0x8e, 0x15, 0x00, 0xb3, 0x07, 0xa8, 0x40, 0xc9,
        0x7e, 0x13, 0x7e, 0xee, 0xff, 0x33, 0x03, 0xf6, 0x00, 0x93, 0x68, 0x15, 0x00,
        0x93, 0x8e, 0x7e, 0xcc, 0xb7, 0x26, 0x02, 0x40, 0x13, 0x0f, 0x00, 0x02, 0x63,
        0x72, 0xc8, 0x09, 0x83, 0x57, 0x08, 0x00, 0xc2, 0x07, 0xc1, 0x83, 0x63, 0x6e,
        0xa8, 0x06, 0x63, 0x7c, 0xb8, 0x06, 0x03, 0x47, 0x03, 0x00, 0x13, 0xf6, 0x07,
        0xf0, 0x59, 0x8e, 0x63, 0xfa, 0xb8, 0x00, 0x03, 0x47, 0x13, 0x00, 0x13, 0x76,
        0xf6, 0x0f, 0x13, 0x77, 0xf7, 0x0f, 0x22, 0x07, 0x59, 0x8e, 0x63, 0x04, 0xf6,
        0x04, 0x13, 0xc7, 0xf7, 0xff, 0x71, 0x8f, 0x19, 0xc3, 0xf6, 0x97, 0xb5, 0xe3,
        0x9c, 0x4a, 0x37, 0x47, 0x0f, 0x00, 0x13, 0x07, 0x17, 0x24, 0x93, 0xe7, 0x17,
        0x00, 0x9c, 0xca, 0x23, 0x10, 0xc8, 0x00, 0x11, 0xa0, 0x15, 0xcf, 0xdc, 0x46,
        0x7d, 0x17, 0x85, 0x8b, 0xe5, 0xff, 0x9c, 0x4a, 0xf9, 0x9b, 0x9c, 0xca, 0xdc,
        0x46, 0xc1, 0x8b, 0x8d, 0xeb, 0x83, 0x57, 0x08, 0x00, 0x63, 0x9b, 0xc7, 0x02,
        0x23, 0xa6, 0xe6, 0x01, 0x09, 0x08, 0x89, 0x08, 0x09, 0x03, 0xe3, 0x62, 0xc8,
        0xf9, 0x01, 0x45, 0x82, 0x80, 0xe3, 0xf9, 0xb8, 0xfe, 0xe3, 0xe7, 0xa8, 0xfe,
        0x3e, 0x86, 0x41, 0xbf, 0x9c, 0x4a, 0x51, 0x55, 0xf9, 0x9b, 0x9c, 0xca, 0x82,
        0x80, 0x93, 0x07, 0x00, 0x03, 0xdc, 0xc6, 0x71, 0x55, 0x82, 0x80, 0x69, 0x55,
        0x82, 0x80};

alignas(
    routine_align) static uint8_t g_ch32_flash_write_page[ch32_flash_write_page_size] = {
    0x63, 0x77, 0xb5, 0x12, 0x13, 0x0e, 0x06, 0x10, 0xb7, 0x28, 0x02, 0x40, 0xc1, 0x6e,
    0x83, 0xa7, 0x08, 0x01, 0x37, 0x47, 0x0f, 0x00, 0x13, 0x07, 0x17, 0x24, 0xb3, 0xe7,
    0xd7, 0x01, 0x23, 0xa8, 0xf8, 0x00, 0x11, 0xa0, 0x79, 0xcf, 0x83, 0xa7, 0xc8, 0x00,
    0x7d, 0x17, 0x85, 0x8b, 0xfd, 0xfb, 0x37, 0x47, 0x0f, 0x00, 0x13, 0x07, 0x17, 0x24,
    0xb7, 0x26, 0x02, 0x40, 0x11, 0xa0, 0x5d, 0xcb, 0xdc, 0x46, 0x7d, 0x17, 0x89, 0x8b,
    0xe5, 0xff, 0x32, 0x88, 0xb3, 0x0f, 0xc5, 0x40, 0xb7, 0x26, 0x02, 0x40, 0x83, 0x47,
    0x18, 0x00, 0x03, 0x43, 0x28, 0x00, 0x03, 0x4f, 0x08, 0x00, 0x03, 0x47, 0x38, 0x00,
    0x42, 0x03, 0xa2, 0x07, 0xb3, 0xe7, 0x67, 0x00, 0x62, 0x07, 0xb3, 0xe7, 0xe7, 0x01,
    0xd9, 0x8f, 0x33, 0x83, 0x0f, 0x01, 0x37, 0x47, 0x0f, 0x00, 0x23, 0x20, 0xf3, 0x00,
    0x13, 0x07, 0x17, 0x24, 0x11, 0xa0, 0x25, 0xcb, 0xdc, 0x46, 0x7d, 0x17, 0x89, 0x8b,
    0xe5, 0xff, 0x11, 0x08, 0xe3, 0x11, 0x0e, 0xfd, 0x9c, 0x4a, 0x37, 0x08, 0x20, 0x00,
    0x37, 0x47, 0x0f, 0x00, 0xb3, 0xe7, 0x07, 0x01, 0x9c, 0xca, 0x13, 0x07, 0x17, 0x24,
    0xb7, 0x26, 0x02, 0x40, 0x11, 0xa0, 0x39, 0xc3, 0xdc, 0x46, 0x7d, 0x17, 0x85, 0x8b,
    0xe5, 0xff, 0x9c, 0x4a, 0x41, 0x77, 0x7d, 0x17, 0xf9, 0x8f, 0x9c, 0xca, 0xdc, 0x46,
    0xc1, 0x8b, 0xb5, 0xe7, 0x32, 0x87, 0x19, 0xa0, 0x63, 0x04, 0xc7, 0x05, 0x83, 0x47,
    0x17, 0x00, 0x03, 0x48, 0x07, 0x00, 0xb3, 0x06, 0xf7, 0x01, 0xa2, 0x07, 0xb3, 0xe7,
    0x07, 0x01, 0x83, 0xd6, 0x06, 0x00, 0xc2, 0x07, 0xc1, 0x83, 0x09, 0x07, 0xe3, 0x80,
    0xd7, 0xfe, 0x69, 0x55, 0x82, 0x80, 0x9c, 0x4a, 0x41, 0x77, 0x7d, 0x17, 0xf9, 0x8f,
    0x9c, 0xca, 0x51, 0x55, 0x82, 0x80, 0x83, 0xa7, 0x08, 0x01, 0x41, 0x77, 0x7d, 0x17,
    0xf9, 0x8f, 0x23, 0xa8, 0xf8, 0x00, 0x51, 0x55, 0x82, 0x80, 0xb7, 0x27, 0x02, 0x40,
    0x13, 0x07, 0x00, 0x02, 0x13, 0x05, 0x05, 0x10, 0xd8, 0xc7, 0x13, 0x0e, 0x0e, 0x10,
    0x13, 0x06, 0x06, 0x10, 0xe3, 0x62, 0xb5, 0xee, 0x01, 0x45, 0x82, 0x80, 0x93, 0x07,
    0x00, 0x03, 0xdc, 0xc6, 0x71, 0x55, 0x82, 0x80};

static CH32FlashHotPaths g_ch32_flash_hot_paths;

static inline void flash_clear_status_direct(uint32_t flags) { FLASH->STATR = flags; }

static void InitHotPathsOnce()
{
  static bool inited = false;
  if (inited)
  {
    return;
  }

  static_assert(sizeof(g_ch32_flash_erase_hot) == ch32_flash_erase_hot_path_size);
  static_assert(sizeof(g_ch32_flash_write_hot) == ch32_flash_write_hot_loop_size);
  static_assert(sizeof(g_ch32_flash_write_page) == ch32_flash_write_page_size);

  __builtin___clear_cache(
      reinterpret_cast<char*>(g_ch32_flash_erase_hot),
      reinterpret_cast<char*>(g_ch32_flash_erase_hot + sizeof(g_ch32_flash_erase_hot)));
  __builtin___clear_cache(
      reinterpret_cast<char*>(g_ch32_flash_write_hot),
      reinterpret_cast<char*>(g_ch32_flash_write_hot + sizeof(g_ch32_flash_write_hot)));
  __builtin___clear_cache(
      reinterpret_cast<char*>(g_ch32_flash_write_page),
      reinterpret_cast<char*>(g_ch32_flash_write_page + sizeof(g_ch32_flash_write_page)));

  g_ch32_flash_hot_paths.erase =
      reinterpret_cast<CH32FlashEraseHotPathFn>(g_ch32_flash_erase_hot);
  g_ch32_flash_hot_paths.write_halfword =
      reinterpret_cast<CH32FlashWriteHotLoopFn>(g_ch32_flash_write_hot);
  g_ch32_flash_hot_paths.write_page =
      reinterpret_cast<CH32FlashWritePageFn>(g_ch32_flash_write_page);
  inited = true;
}
}  // namespace

// 访问时钟切半
static void flash_set_access_clock_half_sysclk(void)
{
  FLASH_Unlock();              // Unlock FPEC/CTL.
  FLASH->CTLR &= ~(1u << 25);  // SCKMOD=0 => SYSCLK/2.
  FLASH_Lock();                // Optional relock.
}

// 访问时钟还原
static void flash_set_access_clock_sysclk(void)
{
  FLASH_Unlock();
  FLASH->CTLR |= (1u << 25);  // SCKMOD=1 => access clock = SYSCLK.
  FLASH_Lock();
}

static inline void flash_exit_enhanced_read_if_enabled()
{
  if (FLASH->STATR & (1u << 7))
  {  // EHMODS=1?
    FLASH_Unlock();
    FLASH->CTLR &= ~(1u << 24);  // EHMOD=0.
    FLASH->CTLR |= (1u << 22);   // RSENACT=1 (write-only, auto-cleared by hardware).
    FLASH_Lock();
  }
}

static inline void flash_fast_unlock()
{
  FLASH_Unlock();  // Regular unlock sequence (KEYR).
  // Fast-mode unlock sequence (MODEKEYR KEY1/KEY2).
  FLASH->MODEKEYR = 0x45670123u;
  FLASH->MODEKEYR = 0xCDEF89ABu;
}

static inline void flash_fast_lock()
{
  FLASH_Unlock();
  FLASH->CTLR |= (1u << 15);  // FLOCK=1.
  FLASH_Lock();
}

static inline void flash_clear_flags_once()
{
#ifdef FLASH_FLAG_BSY
  FLASH_ClearFlag(FLASH_FLAG_BSY | FLASH_FLAG_EOP | FLASH_FLAG_WRPRTERR);
#else
  FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_WRPRTERR);
#endif
}

inline void CH32Flash::ClearFlashFlagsOnce() { flash_clear_flags_once(); }

CH32Flash::CH32Flash(const FlashSector* sectors, size_t sector_count, size_t start_sector)
    : Flash(sectors[start_sector - 1].size, MinWriteSize(),
            {reinterpret_cast<void*>(sectors[start_sector - 1].address),
             sectors[sector_count - 1].address - sectors[start_sector - 1].address +
                 sectors[sector_count - 1].size}),
      sectors_(sectors),
      base_address_(sectors[start_sector - 1].address),
      sector_count_(sector_count)
{
  InitHotPathsOnce();
}

ErrorCode CH32Flash::Erase(size_t offset, size_t size)
{
  if (size == 0)
  {
    return ErrorCode::ARG_ERR;
  }

  ASSERT(SystemCoreClock <= 120000000);

  const uint32_t START_ADDR = base_address_ + static_cast<uint32_t>(offset);
  if (!IsInRange(START_ADDR, size))
  {
    return ErrorCode::OUT_OF_RANGE;
  }
  const uint32_t END_ADDR = START_ADDR + static_cast<uint32_t>(size);
  const auto erase_hot_path = g_ch32_flash_hot_paths.erase;
  ASSERT(erase_hot_path != nullptr);

  // 1) 退出增强读模式
  flash_exit_enhanced_read_if_enabled();  // 进入擦写前必须退出 EHMOD

  // 2) 访问时钟降为 SYSCLK/2（SCKMOD=0）
  flash_set_access_clock_half_sysclk();

  // 3) 解锁：常规 + 快速模式
  __disable_irq();
  flash_fast_unlock();
  ClearFlashFlagsOnce();

  // 4) 计算 256B 对齐区间
  const uint32_t FAST_SZ = 256u;
  const uint32_t ERASE_BEGIN = START_ADDR & ~(FAST_SZ - 1u);
  const uint32_t ERASE_END = (END_ADDR + FAST_SZ - 1u) & ~(FAST_SZ - 1u);
  if (ERASE_END <= ERASE_BEGIN)
  {
    flash_fast_lock();
    __enable_irq();
    flash_set_access_clock_sysclk();
    return ErrorCode::OK;
  }

  const ErrorCode ec = erase_hot_path(ERASE_BEGIN, ERASE_END);
  flash_fast_lock();
  __enable_irq();
  flash_set_access_clock_sysclk();
  return ec;
}

ErrorCode CH32Flash::Write(size_t offset, ConstRawData data)
{
  ASSERT(SystemCoreClock <= 120000000);

  if (!data.addr_ || data.size_ == 0)
  {
    ASSERT(false);
    return ErrorCode::ARG_ERR;
  }

  const uint32_t START_ADDR = base_address_ + static_cast<uint32_t>(offset);
  if (!IsInRange(START_ADDR, data.size_))
  {
    ASSERT(false);
    return ErrorCode::OUT_OF_RANGE;
  }

  const uint8_t* src = reinterpret_cast<const uint8_t*>(data.addr_);
  const uint32_t END_ADDR = START_ADDR + static_cast<uint32_t>(data.size_);
  return CH32FlashWriteHotPath(START_ADDR, END_ADDR, src);
}

extern "C" __attribute__((noinline)) ErrorCode CH32FlashWriteHotPath(uint32_t start_addr,
                                                                     uint32_t end_addr,
                                                                     const uint8_t* src)
{
  const auto write_hot_loop = g_ch32_flash_hot_paths.write_halfword;
  const auto write_page = g_ch32_flash_hot_paths.write_page;
  constexpr uint32_t page_size = 256u;
  ASSERT(write_hot_loop != nullptr && write_page != nullptr);
  flash_exit_enhanced_read_if_enabled();
  flash_set_access_clock_half_sysclk();

  __disable_irq();
  flash_fast_unlock();
  flash_clear_flags_once();
  ErrorCode ec = ErrorCode::OK;

  const uint32_t aligned_begin = (start_addr + page_size - 1u) & ~(page_size - 1u);
  const uint32_t aligned_end = end_addr & ~(page_size - 1u);

  const uint32_t head_end = (aligned_begin < end_addr) ? aligned_begin : end_addr;
  if (start_addr < head_end)
  {
    ec = write_hot_loop(start_addr, head_end, src);
  }

  const bool has_full_pages = (aligned_begin < aligned_end);
  if (ec == ErrorCode::OK && has_full_pages)
  {
    ec = write_page(aligned_begin, aligned_end, src + (aligned_begin - start_addr));
  }

  // Tail begins after the already-consumed head/body segments.
  // For single-page unaligned writes, `head_end` may already reach `end_addr`,
  // so the tail must start from `head_end` instead of replaying the same span.
  // 尾段必须从已经消费完的 head/body 之后开始。
  // 对“同页内的非对齐小写入”，`head_end` 可能已经等于 `end_addr`，
  // 这里不能再从 `start_addr` 重放同一段。
  const uint32_t tail_begin = has_full_pages ? aligned_end : head_end;
  if (ec == ErrorCode::OK && tail_begin < end_addr)
  {
    ec = write_hot_loop(tail_begin, end_addr, src + (tail_begin - start_addr));
  }

  flash_fast_lock();
  __enable_irq();
  flash_set_access_clock_sysclk();
  return ec;
}

bool CH32Flash::IsInRange(uint32_t addr, size_t size) const
{
  const uint32_t BEGIN = base_address_;
  const uint32_t LIMIT =
      sectors_[sector_count_ - 1].address + sectors_[sector_count_ - 1].size;
  const uint32_t END = addr + static_cast<uint32_t>(size);
  return (addr >= BEGIN) && (END <= LIMIT) && (END >= addr);
}

// NOLINTEND(cppcoreguidelines-pro-type-cstyle-cast,performance-no-int-to-ptr)
