#include <stdint.h>
#include <stdbool.h>

#include "main.h"

#if defined(STM32F0) || defined(STM32G0) || defined(STM32L0)

/**
 * @brief  模拟实现 __atomic_compare_exchange_4 函数 / Simulate the __atomic_compare_exchange_4 function
 * @param  ptr 指向原子变量的指针 / Pointer to the atomic variable
 * @param  expected 预期值的指针，如果比较失败会更新为实际值 / Pointer to the expected value, updated with actual value if comparison fails
 * @param  desired 需要交换的新值 / The new value to be stored if the comparison succeeds
 * @param  weak 忽略参数，保留以兼容 / Ignored parameter, kept for compatibility
 * @param  success_memorder 成功时的内存顺序标志（忽略） / Memory order on success (ignored)
 * @param  failure_memorder 失败时的内存顺序标志（忽略） / Memory order on failure (ignored)
 * @retval 返回 1 表示成功，0 表示失败 / Returns 1 on success, 0 on failure
 */
__attribute__((weak, used)) int __atomic_compare_exchange_4(volatile void *ptr, void *expected,
                                                             uint32_t desired, bool weak,
                                                             int success_memorder,
                                                             int failure_memorder)
{
    volatile uint32_t *addr = (volatile uint32_t *)ptr;
    uint32_t expected_val = *(uint32_t *)expected;
    int result;

    __disable_irq();
    if (*addr == expected_val)
    {
        *addr = desired;
        result = 1;
    }
    else
    {
        *(uint32_t *)expected = *addr;
        result = 0;
    }
    __enable_irq();
    return result;
}

/**
 * @brief  模拟实现 __atomic_store_4 函数 / Simulate the __atomic_store_4 function
 * @param  ptr 指向原子变量的指针 / Pointer to the atomic variable
 * @param  val 需要存储的新值 / The new value to be stored
 * @param  memorder 内存顺序标志 / Memory order (ignored)
 * @retval 无返回值 / None
 */
__attribute__((weak, used)) void __atomic_store_4(volatile void *ptr, uint32_t val, int memorder)
{
    volatile uint32_t *addr = (volatile uint32_t *)ptr;
    __disable_irq();
    *addr = val;
    __enable_irq();
}

/**
 * @brief  模拟实现 __atomic_load_4 函数 / Simulate the __atomic_load_4 function
 * @param  ptr 指向原子变量的指针 / Pointer to the atomic variable
 * @param  memorder 内存顺序标志 / Memory order (ignored)
 * @retval 当前值 / Returns the current value
 */
__attribute__((weak, used)) uint32_t __atomic_load_4(volatile void *ptr, int memorder)
{
    volatile uint32_t *addr = (volatile uint32_t *)ptr;
    uint32_t val;
    __disable_irq();
    val = *addr;
    __enable_irq();
    return val;
}

/**
 * @brief  模拟实现 __atomic_exchange_4 函数 / Simulate the __atomic_exchange_4 function
 * @param  ptr 指向原子变量的指针 / Pointer to the atomic variable
 * @param  val 需要存储的新值 / The new value to be stored
 * @param  memorder 内存顺序标志 / Memory order (ignored)
 * @retval 交换前的旧值 / Returns the old value before exchange
 */
__attribute__((weak, used)) uint32_t __atomic_exchange_4(volatile void *ptr, uint32_t val, int memorder)
{
    volatile uint32_t *addr = (volatile uint32_t *)ptr;
    uint32_t old_val;
    __disable_irq();
    old_val = *addr;
    *addr = val;
    __enable_irq();
    return old_val;
}

/**
 * @brief  模拟实现 __atomic_fetch_add_4 函数 / Simulate the __atomic_fetch_add_4 function
 * @param  ptr 指向原子变量的指针 / Pointer to the atomic variable
 * @param  val 需要相加的值 / The value to add
 * @param  memorder 内存顺序标志 / Memory order (ignored)
 * @retval 加法前的旧值 / Returns the old value before addition
 */
__attribute__((weak, used)) uint32_t __atomic_fetch_add_4(volatile void *ptr, uint32_t val, int memorder)
{
    volatile uint32_t *addr = (volatile uint32_t *)ptr;
    uint32_t old_val;
    __disable_irq();
    old_val = *addr;
    *addr = old_val + val;
    __enable_irq();
    return old_val;
}

/**
 * @brief  模拟实现 __atomic_fetch_sub_4 函数 / Simulate the __atomic_fetch_sub_4 function
 * @param  ptr 指向原子变量的指针 / Pointer to the atomic variable
 * @param  val 需要相减的值 / The value to subtract
 * @param  memorder 内存顺序标志 / Memory order (ignored)
 * @retval 减法前的旧值 / Returns the old value before subtraction
 */
__attribute__((weak, used)) uint32_t __atomic_fetch_sub_4(volatile void *ptr, uint32_t val, int memorder)
{
    volatile uint32_t *addr = (volatile uint32_t *)ptr;
    uint32_t old_val;
    __disable_irq();
    old_val = *addr;
    *addr = old_val - val;
    __enable_irq();
    return old_val;
}

#endif