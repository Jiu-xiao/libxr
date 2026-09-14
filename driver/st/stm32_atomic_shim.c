#include "main.h"

#if defined(STM32F0) || defined(STM32G0) || defined(STM32L0)

/**
 * @brief 保存中断状态并进入原子操作 / Save interrupt state and enter an atomic operation.
 * @return 进入前的 PRIMASK / PRIMASK before entry.
 */
static inline uint32_t AtomicEnter(void)
{
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  __DMB();
  return primask;
}

/**
 * @brief 完成原子操作并恢复中断状态 / Finish an atomic operation and restore interrupts.
 * @param primask 进入前的 PRIMASK / PRIMASK saved on entry.
 */
static inline void AtomicExit(uint32_t primask)
{
  __DMB();
  __set_PRIMASK(primask);
}

/* GCC 定长运行时接口有五个参数，使用独立名称避开六参数内建声明。
 * Name the five-argument runtime helper separately from the six-argument builtin. */
__attribute__((weak, used)) _Bool libxr_atomic_compare_exchange_4(
    volatile void* ptr, void* expected, unsigned int desired, int success_memorder,
    int failure_memorder) __asm__("__atomic_compare_exchange_4");

/**
 * @brief  模拟实现 __atomic_compare_exchange_4 函数 / Simulate the
 * __atomic_compare_exchange_4 function
 * @param  ptr 指向原子变量的指针 / Pointer to the atomic variable
 * @param  expected 预期值的指针，如果比较失败会更新为实际值 / Pointer to the expected
 * value, updated with actual value if comparison fails
 * @param  desired 需要交换的新值 / The new value to be stored if the comparison succeeds
 * @param  success_memorder 成功时的内存顺序标志（忽略） / Memory order on success
 * (ignored)
 * @param  failure_memorder 失败时的内存顺序标志（忽略） / Memory order on failure
 * (ignored)
 * @retval 返回 1 表示成功，0 表示失败 / Returns 1 on success, 0 on failure
 */
__attribute__((weak, used)) _Bool libxr_atomic_compare_exchange_4(volatile void* ptr,
                                                                  void* expected,
                                                                  unsigned int desired,
                                                                  int success_memorder,
                                                                  int failure_memorder)
{
  UNUSED(success_memorder);
  UNUSED(failure_memorder);

  volatile unsigned int* addr = (volatile unsigned int*)ptr;
  unsigned int expected_val = *(unsigned int*)expected;
  int result;

  const uint32_t primask = AtomicEnter();
  if (*addr == expected_val)
  {
    *addr = desired;
    result = 1;
  }
  else
  {
    *(unsigned int*)expected = *addr;
    result = 0;
  }
  AtomicExit(primask);
  return result;
}

/**
 * @brief  模拟实现 __atomic_store_4 函数 / Simulate the __atomic_store_4 function
 * @param  ptr 指向原子变量的指针 / Pointer to the atomic variable
 * @param  val 需要存储的新值 / The new value to be stored
 * @param  memorder 内存顺序标志 / Memory order (ignored)
 * @retval 无返回值 / None
 */
__attribute__((weak, used)) void __atomic_store_4(volatile void* ptr, unsigned int val,
                                                  int memorder)
{
  UNUSED(memorder);

  volatile unsigned int* addr = (volatile unsigned int*)ptr;
  const uint32_t primask = AtomicEnter();
  *addr = val;
  AtomicExit(primask);
}

/**
 * @brief  模拟实现 __atomic_load_4 函数 / Simulate the __atomic_load_4 function
 * @param  ptr 指向原子变量的指针 / Pointer to the atomic variable
 * @param  memorder 内存顺序标志 / Memory order (ignored)
 * @retval 当前值 / Returns the current value
 */
__attribute__((weak, used)) unsigned int __atomic_load_4(const volatile void* ptr,
                                                         int memorder)
{
  UNUSED(memorder);

  volatile unsigned int* addr = (volatile unsigned int*)ptr;
  unsigned int val;
  const uint32_t primask = AtomicEnter();
  val = *addr;
  AtomicExit(primask);
  return val;
}

/**
 * @brief  模拟实现 __atomic_exchange_4 函数 / Simulate the __atomic_exchange_4 function
 * @param  ptr 指向原子变量的指针 / Pointer to the atomic variable
 * @param  val 需要存储的新值 / The new value to be stored
 * @param  memorder 内存顺序标志 / Memory order (ignored)
 * @retval 交换前的旧值 / Returns the old value before exchange
 */
__attribute__((weak, used)) unsigned int __atomic_exchange_4(volatile void* ptr,
                                                             unsigned int val,
                                                             int memorder)
{
  UNUSED(memorder);

  volatile unsigned int* addr = (volatile unsigned int*)ptr;
  unsigned int old_val;
  const uint32_t primask = AtomicEnter();
  old_val = *addr;
  *addr = val;
  AtomicExit(primask);
  return old_val;
}

/**
 * @brief  模拟实现 __atomic_fetch_add_4 函数 / Simulate the __atomic_fetch_add_4 function
 * @param  ptr 指向原子变量的指针 / Pointer to the atomic variable
 * @param  val 需要相加的值 / The value to add
 * @param  memorder 内存顺序标志 / Memory order (ignored)
 * @retval 加法前的旧值 / Returns the old value before addition
 */
__attribute__((weak, used)) unsigned int __atomic_fetch_add_4(volatile void* ptr,
                                                              unsigned int val,
                                                              int memorder)
{
  UNUSED(memorder);

  volatile unsigned int* addr = (volatile unsigned int*)ptr;
  unsigned int old_val;
  const uint32_t primask = AtomicEnter();
  old_val = *addr;
  *addr = old_val + val;
  AtomicExit(primask);
  return old_val;
}

/**
 * @brief  模拟实现 __atomic_fetch_sub_4 函数 / Simulate the __atomic_fetch_sub_4 function
 * @param  ptr 指向原子变量的指针 / Pointer to the atomic variable
 * @param  val 需要相减的值 / The value to subtract
 * @param  memorder 内存顺序标志 / Memory order (ignored)
 * @retval 减法前的旧值 / Returns the old value before subtraction
 */
__attribute__((weak, used)) unsigned int __atomic_fetch_sub_4(volatile void* ptr,
                                                              unsigned int val,
                                                              int memorder)
{
  UNUSED(memorder);

  volatile unsigned int* addr = (volatile unsigned int*)ptr;
  unsigned int old_val;
  const uint32_t primask = AtomicEnter();
  old_val = *addr;
  *addr = old_val - val;
  AtomicExit(primask);
  return old_val;
}

/**
 * @brief 原子置位并返回原值 / Atomically set bits and return the previous value.
 * @param ptr 目标字地址 / Target word address.
 * @param val 待置位的掩码 / Bits to set.
 * @param memorder 内存顺序，本实现统一使用屏障 / Memory order; barriers are always used.
 * @return 更新前的值 / Value before the update.
 */
__attribute__((weak, used)) unsigned int __atomic_fetch_or_4(volatile void* ptr,
                                                             unsigned int val,
                                                             int memorder)
{
  UNUSED(memorder);
  volatile unsigned int* addr = (volatile unsigned int*)ptr;
  const uint32_t primask = AtomicEnter();
  const unsigned int old_val = *addr;
  *addr = old_val | val;
  AtomicExit(primask);
  return old_val;
}

#endif
