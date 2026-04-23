#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "main.h"

namespace LibXR
{
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
 * @brief Cleans D-Cache lines covering the specified memory range
 * @brief 清理指定内存范围覆盖的 D-Cache cache line
 */
inline void STM32_CleanDCacheByAddr(const void* addr, size_t size)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
  auto* raw = const_cast<void*>(addr);
  const auto dsize = static_cast<int32_t>(size);
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
inline void STM32_InvalidateDCacheByAddr(void* addr, size_t size)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
  const auto dsize = static_cast<int32_t>(size);
  STM32_CallDCacheByAddr(&SCB_InvalidateDCache_by_Addr, addr, dsize);
#else
  (void)addr;
  (void)size;
#endif
}
}  // namespace LibXR
