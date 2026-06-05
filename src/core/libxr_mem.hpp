#pragma once
#include <cstdint>
#include <cstdio>

namespace LibXR
{
/**
 * @class Memory
 * @brief 静态内存操作工具 / Static memory-operation utilities
 */
class Memory
{
 public:
  /**
   * @brief 快速内存拷贝 / Fast memory copy
   *
   * @param dst 目标地址 / Destination address
   * @param src 源地址 / Source address
   * @param size 拷贝大小 / Copy size
   */
  static void FastCopy(void* dst, const void* src, size_t size);

  /**
   * @brief 内存搬移 / Memory move
   *
   * @note 这个接口会先判断 `dst` 和 `src` 区间是否重叠；若不重叠则直接退化成
   *       `FastCopy()`，若重叠则走安全的 move 路径。
   *       This interface first checks whether the `dst` and `src` ranges
   *       overlap; if they do not, it falls back to `FastCopy()`, otherwise it
   *       uses the safe move path.
   *
   * @param dst 目标地址 / Destination address
   * @param src 源地址 / Source address
   * @param size 搬移大小 / Move size
   */
  static void FastMove(void* dst, const void* src, size_t size);

  /**
   * @brief 快速内存填充 / Fast memory fill
   *
   * @param dst 目标地址 / Destination address
   * @param value 填充值 / Fill value
   * @param size 填充大小 / Fill size
   */
  static void FastSet(void* dst, uint8_t value, size_t size);

  /**
   * @brief 快速内存比较 / Fast memory comparison
   *
   * @param a 比较对象 A
   * @param b 比较对象 B
   * @param size 比较大小 / Comparison size
   * @return int 比较结果 / Comparison result
   */
  static int FastCmp(const void* a, const void* b, size_t size);
};
}  // namespace LibXR
