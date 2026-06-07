#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "libxr_def.hpp"
#include "main.h"

namespace LibXR
{
static_assert(LibXR::HW_CACHE_LINE_SIZE != 0u &&
                  (LibXR::HW_CACHE_LINE_SIZE &
                   (LibXR::HW_CACHE_LINE_SIZE - static_cast<size_t>(1u))) == 0u,
              "HW_CACHE_LINE_SIZE must be a non-zero power of two");

/**
 * @brief D-Cache API accepts `void*`
 * @brief D-Cache API 可直接接受 `void*`
 */
template <typename FunctionType>
concept DCacheFunctionAcceptsVoidPtr =
    std::is_invocable_v<FunctionType, void*, int32_t>;

/**
 * @brief D-Cache API accepts `volatile void*`
 * @brief D-Cache API 可直接接受 `volatile void*`
 */
template <typename FunctionType>
concept DCacheFunctionAcceptsVolatileVoidPtr =
    std::is_invocable_v<FunctionType, volatile void*, int32_t>;

/**
 * @brief Calls the CMSIS D-Cache helper with the pointer type accepted by the
 * current toolchain
 * @brief 按当前工具链接受的指针类型调用 CMSIS D-Cache 接口
 */
template <typename FunctionType>
requires DCacheFunctionAcceptsVoidPtr<FunctionType>
inline void STM32_CallDCacheByAddr(FunctionType function, void* addr, int32_t dsize)
{
  function(addr, dsize);
}

template <typename FunctionType>
requires(!DCacheFunctionAcceptsVoidPtr<FunctionType> &&
         DCacheFunctionAcceptsVolatileVoidPtr<FunctionType>)
inline void STM32_CallDCacheByAddr(FunctionType function, void* addr, int32_t dsize)
{
  function(reinterpret_cast<volatile void*>(addr), dsize);
}

template <typename FunctionType>
requires(!DCacheFunctionAcceptsVoidPtr<FunctionType> &&
         !DCacheFunctionAcceptsVolatileVoidPtr<FunctionType>)
inline void STM32_CallDCacheByAddr(FunctionType function, void* addr, int32_t dsize)
{
  function(reinterpret_cast<uint32_t*>(addr), dsize);
}

/**
 * @brief Computes the cache-line-aligned address range consumed by CMSIS
 * D-Cache helpers
 * @brief 计算 CMSIS D-Cache 接口需要的 cache line 对齐地址范围
 *
 * @note 这里的按位掩码对齐算法依赖 `HW_CACHE_LINE_SIZE` 为非零 2 次幂。
 *       The bit-mask alignment math below requires `HW_CACHE_LINE_SIZE` to be
 *       a non-zero power of two.
 */
inline bool STM32_ComputeAlignedDCacheRange(const void* addr, size_t size, void*& raw,
                                            int32_t& dsize)
{
  if (addr == nullptr || size == 0u)
  {
    return false;
  }

  const auto align = static_cast<uintptr_t>(LibXR::HW_CACHE_LINE_SIZE);
  const auto start = reinterpret_cast<uintptr_t>(addr) & ~(align - 1u);
  const auto end = (reinterpret_cast<uintptr_t>(addr) + size + align - 1u) & ~(align - 1u);
  raw = reinterpret_cast<void*>(start);
  dsize = static_cast<int32_t>(end - start);
  return dsize > 0;
}

/**
 * @brief Cleans D-Cache lines covering the specified memory range
 * @brief 清理指定内存范围覆盖的 D-Cache cache line
 */
inline void STM32_CleanDCacheByAddr(const void* addr, size_t size)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
  void* raw = nullptr;
  auto dsize = int32_t{};

  if (!STM32_ComputeAlignedDCacheRange(addr, size, raw, dsize))
  {
    return;
  }

  STM32_CallDCacheByAddr(&SCB_CleanDCache_by_Addr, raw, dsize);
#else
  (void)addr;
  (void)size;
#endif
}

/**
 * @brief Invalidates D-Cache lines covering the specified memory range
 * @brief 失效指定内存范围覆盖的 D-Cache cache line
 */
inline void STM32_InvalidateDCacheByAddr(const void* addr, size_t size)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
  void* raw = nullptr;
  auto dsize = int32_t{};

  if (!STM32_ComputeAlignedDCacheRange(addr, size, raw, dsize))
  {
    return;
  }

  STM32_CallDCacheByAddr(&SCB_InvalidateDCache_by_Addr, raw, dsize);
#else
  (void)addr;
  (void)size;
#endif
}
}  // namespace LibXR
