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
};
}  // namespace LibXR