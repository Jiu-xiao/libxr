#include "msp.h" 

static inline uint32_t atomic_enter(void) {
    uint32_t primask_state = __get_PRIMASK();
    __disable_irq();
    return primask_state;
}

static inline void atomic_exit(uint32_t primask_state) {
    if (!primask_state) {
        __enable_irq();
    }
}

#define UNUSED(x) (void)(x)

/**
 * @brief  模拟实现 __atomic_compare_exchange_4 函数 / Simulate the
 * __atomic_compare_exchange_4 function
 * @param  ptr 指向原子变量的指针 / Pointer to the atomic variable
 * @param  expected 预期值的指针，如果比较失败会更新为实际值 / Pointer to the expected
 * value, updated with actual value if comparison fails
 * @param  desired 需要交换的新值 / The new value to be stored if the comparison succeeds
 * @param  weak 忽略参数，保留以兼容 / Ignored parameter, kept for compatibility
 * @param  success_memorder 成功时的内存顺序标志（忽略） / Memory order on success
 * (ignored)
 * @param  failure_memorder 失败时的内存顺序标志（忽略） / Memory order on failure
 * (ignored)
 * @retval 返回 1 表示成功，0 表示失败 / Returns 1 on success, 0 on failure
 */
__attribute__((weak)) _Bool
__atomic_compare_exchange_4(volatile void *ptr, void *expected, unsigned int desired, // NOLINT
                            _Bool weak, int success_memorder, int failure_memorder)
{
  UNUSED(weak);
  UNUSED(success_memorder);
  UNUSED(failure_memorder);

  volatile unsigned int *addr = (volatile unsigned int *)ptr;
  unsigned int expected_val = *(unsigned int *)expected;
  _Bool result = false;

  // 2. 使用安全的方式开关中断
  uint32_t primask = atomic_enter(); 
  
  if (*addr == expected_val)
  {
    *addr = desired;
    result = true;
  }
  else
  {
    *(unsigned int *)expected = *addr;
    result = false;
  }
  
  atomic_exit(primask); // 恢复状态
  return result;
}

/**
 * @brief  模拟实现 __atomic_store_4 函数 / Simulate the __atomic_store_4 function
 * @param  ptr 指向原子变量的指针 / Pointer to the atomic variable
 * @param  val 需要存储的新值 / The new value to be stored
 * @param  memorder 内存顺序标志 / Memory order (ignored)
 * @retval 无返回值 / None
 */
__attribute__((weak)) void 
__atomic_store_4(volatile void *ptr, unsigned int val, int memorder) // NOLINT
{
  UNUSED(memorder);
  volatile unsigned int *addr = (volatile unsigned int *)ptr;

  uint32_t primask = atomic_enter();
  *addr = val;
  atomic_exit(primask);
}

/**
 * @brief  模拟实现 __atomic_load_4 函数 / Simulate the __atomic_load_4 function
 * @param  ptr 指向原子变量的指针 / Pointer to the atomic variable
 * @param  memorder 内存顺序标志 / Memory order (ignored)
 * @retval 当前值 / Returns the current value
 */
__attribute__((weak)) unsigned int 
__atomic_load_4(const volatile void *ptr, int memorder) // NOLINT
{
  UNUSED(memorder);
  volatile unsigned int *addr = (volatile unsigned int *)ptr;
  unsigned int val = 0;

  uint32_t primask = atomic_enter();
  val = *addr;
  atomic_exit(primask);
  return val;
}

/**
 * @brief  模拟实现 __atomic_exchange_4 函数 / Simulate the __atomic_exchange_4 function
 * @param  ptr 指向原子变量的指针 / Pointer to the atomic variable
 * @param  val 需要存储的新值 / The new value to be stored
 * @param  memorder 内存顺序标志 / Memory order (ignored)
 * @retval 交换前的旧值 / Returns the old value before exchange
 */
__attribute__((weak)) unsigned int 
__atomic_exchange_4(volatile void *ptr, unsigned int val, int memorder) // NOLINT
{
  UNUSED(memorder);
  volatile unsigned int *addr = (volatile unsigned int *)ptr;
  unsigned int old_val = 0;

  uint32_t primask = atomic_enter();
  old_val = *addr;
  *addr = val;
  atomic_exit(primask);
  return old_val;
}

/**
 * @brief  模拟实现 __atomic_fetch_add_4 函数 / Simulate the __atomic_fetch_add_4 function
 * @param  ptr 指向原子变量的指针 / Pointer to the atomic variable
 * @param  val 需要相加的值 / The value to add
 * @param  memorder 内存顺序标志 / Memory order (ignored)
 * @retval 加法前的旧值 / Returns the old value before addition
 */
__attribute__((weak)) unsigned int 
__atomic_fetch_add_4(volatile void *ptr, unsigned int val, int memorder) // NOLINT
{
  UNUSED(memorder);
  volatile unsigned int *addr = (volatile unsigned int *)ptr;
  unsigned int old_val = 0;

  uint32_t primask = atomic_enter();
  old_val = *addr;
  *addr = old_val + val;
  atomic_exit(primask);
  return old_val;
}

/**
 * @brief  模拟实现 __atomic_fetch_sub_4 函数 / Simulate the __atomic_fetch_sub_4 function
 * @param  ptr 指向原子变量的指针 / Pointer to the atomic variable
 * @param  val 需要相减的值 / The value to subtract
 * @param  memorder 内存顺序标志 / Memory order (ignored)
 * @retval 减法前的旧值 / Returns the old value before subtraction
 */
__attribute__((weak)) unsigned int 
__atomic_fetch_sub_4(volatile void *ptr, unsigned int val, int memorder) // NOLINT
{
  UNUSED(memorder);
  volatile unsigned int *addr = (volatile unsigned int *)ptr;
  unsigned int old_val = 0;

  uint32_t primask = atomic_enter();
  old_val = *addr;
  *addr = old_val - val;
  atomic_exit(primask);
  return old_val;
}

/**
 * @brief  模拟实现 __atomic_exchange_1 函数 / Simulate the __atomic_exchange_1 function
 * @param  ptr 指向原子变量（1字节大小）的指针 / Pointer to the 1-byte atomic variable
 * @param  val 需要存储的新值 / The new value to be stored
 * @param  memorder 内存顺序标志（忽略） / Memory order (ignored)
 * @retval 交换前的旧值 / Returns the old value before exchange
 */
__attribute__((weak)) unsigned char 
__atomic_exchange_1(volatile void *ptr, unsigned char val, int memorder) // NOLINT
{
  UNUSED(memorder);
  volatile unsigned char *addr = (volatile unsigned char *)ptr;
  unsigned char old_val = 0;

  uint32_t primask = atomic_enter();
  old_val = *addr;
  *addr = val;
  atomic_exit(primask);
  return old_val;
}

/**
 * @brief  模拟实现 __atomic_store_1 函数 / Simulate the __atomic_store_1 function
 * @param  ptr 指向原子变量（1字节大小）的指针 / Pointer to the 1-byte atomic variable
 * @param  val 需要存储的新值 / The new value to be stored
 * @param  memorder 内存顺序标志（忽略） / Memory order (ignored)
 * @retval 无返回值 / None
 */
__attribute__((weak)) void 
__atomic_store_1(volatile void *ptr, unsigned char val, int memorder) // NOLINT
{
  UNUSED(memorder);
  volatile unsigned char *addr = (volatile unsigned char *)ptr;

  uint32_t primask = atomic_enter();
  *addr = val;
  atomic_exit(primask);
}

/**
 * @brief  模拟实现 __atomic_test_and_set 函数 / Simulate the __atomic_test_and_set
 * function
 * @param  ptr 指向原子标志位的指针 / Pointer to the atomic flag variable
 * @param  memorder 内存顺序标志（忽略） / Memory order (ignored)
 * @retval 返回之前的值 / Returns the previous value (0 or 1)
 */
#if !defined(__clang__)
__attribute__((weak)) _Bool
__atomic_test_and_set(volatile void *ptr, int memorder)
{
  UNUSED(memorder);
  volatile unsigned char *addr = (volatile unsigned char *)ptr;
  _Bool old_val = false;

  uint32_t primask = atomic_enter();
  old_val = *addr;
  *addr = 1; 
  atomic_exit(primask);
  
  return old_val;
}
#endif