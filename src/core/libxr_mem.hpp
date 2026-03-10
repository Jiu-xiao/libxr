#pragma once
#include <cstdint>
#include <cstdio>

namespace LibXR
{
/**
 * @brief 内存操作类 / Memory operation class
 *
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
