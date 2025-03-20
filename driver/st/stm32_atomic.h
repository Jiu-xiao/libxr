#include "main.h"

#if defined(STM32F0)

/**
 * @brief  模拟实现 __atomic_compare_exchange_4 函数 / Simulate the
 * __atomic_compare_exchange_4 function
 * @param  ptr: 指向原子变量的指针 / Pointer to the atomic variable
 * @param  expected: 预期值的指针，若比较失败会更新为实际值 / Pointer to the expected
 * value, updated with actual value if comparison fails
 * @param  desired: 需要交换的新值 / The new value to be stored if the comparison succeeds
 * @param  weak: 忽略参数，保留以兼容 / Ignored parameter, kept for compatibility
 * @param  success_memorder: 成功时的内存顺序标志（忽略，仅占位） / Memory order on
 * success (ignored, placeholder only)
 * @param  failure_memorder: 失败时的内存顺序标志（忽略，仅占位） / Memory order on
 * failure (ignored, placeholder only)
 * @retval int: 返回1表示成功，返回0表示失败 / Returns 1 on success, 0 on failure
 */
int __atomic_compare_exchange_4(volatile void *ptr, void *expected, uint32_t desired,
                                bool weak, int success_memorder, int failure_memorder)
{
  volatile uint32_t *addr = (volatile uint32_t *)ptr;
  uint32_t expected_val = *(uint32_t *)expected;
  int result;

  __disable_irq();
  if (*addr == expected_val)
  {
    *addr = desired;
    result = 1;  // 成功 / Success
  }
  else
  {
    *(uint32_t *)expected = *addr;  // 更新 expected / Update expected value
    result = 0;                     // 失败 / Failure
  }
  __enable_irq();
  return result;
}

/**
 * @brief  模拟实现 __atomic_exchange_4 函数 / Simulate the __atomic_exchange_4 function
 * @param  ptr: 指向原子变量的指针 / Pointer to the atomic variable
 * @param  val: 需要存储的新值 / The new value to be stored
 * @param  memorder: 内存顺序标志（忽略，仅占位） / Memory order (ignored, placeholder
 * only)
 * @retval uint32_t: 返回交换前的旧值 / Returns the old value before exchange
 */
uint32_t __atomic_exchange_4(volatile void *ptr, uint32_t val, int memorder)
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
 * @param  ptr: 指向原子变量的指针 / Pointer to the atomic variable
 * @param  val: 要增加的值 / The value to add
 * @param  memorder: 内存顺序标志（忽略，仅占位） / Memory order (ignored, placeholder
 * only)
 * @retval uint32_t: 返回加法操作前的旧值 / Returns the old value before addition
 */
uint32_t __atomic_fetch_add_4(volatile void *ptr, uint32_t val, int memorder)
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
 * @param  ptr: 指向原子变量的指针 / Pointer to the atomic variable
 * @param  val: 要减少的值 / The value to subtract
 * @param  memorder: 内存顺序标志（忽略，仅占位） / Memory order (ignored, placeholder
 * only)
 * @retval uint32_t: 返回减法操作前的旧值 / Returns the old value before subtraction
 */
uint32_t __atomic_fetch_sub_4(volatile void *ptr, uint32_t val, int memorder)
{
  volatile uint32_t *addr = (volatile uint32_t *)ptr;
  uint32_t old_val;

  __disable_irq();
  old_val = *addr;
  *addr = old_val - val;
  __enable_irq();
  return old_val;
}

/**
 * @brief  模拟实现 __atomic_load_4 函数 / Simulate the __atomic_load_4 function
 * @param  ptr: 指向原子变量的指针 / Pointer to the atomic variable
 * @param  memorder: 内存顺序标志（忽略，仅占位） / Memory order (ignored, placeholder
 * only)
 * @retval uint32_t: 返回当前值 / Returns the current value
 */
uint32_t __atomic_load_4(volatile void *ptr, int memorder)
{
  volatile uint32_t *addr = (volatile uint32_t *)ptr;
  uint32_t val;

  __disable_irq();
  val = *addr;
  __enable_irq();
  return val;
}

/**
 * @brief  模拟实现 __atomic_store_4 函数 / Simulate the __atomic_store_4 function
 * @param  ptr: 指向原子变量的指针 / Pointer to the atomic variable
 * @param  val: 要存储的新值 / The new value to be stored
 * @param  memorder: 内存顺序标志（忽略，仅占位） / Memory order (ignored, placeholder
 * only)
 * @retval 无返回值 / None
 */
void __atomic_store_4(volatile void *ptr, uint32_t val, int memorder)
{
  volatile uint32_t *addr = (volatile uint32_t *)ptr;

  __disable_irq();
  *addr = val;
  __enable_irq();
}

#endif
