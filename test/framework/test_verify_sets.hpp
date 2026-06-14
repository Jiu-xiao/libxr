/**
 * @file test_verify_sets.hpp
 * @brief verify 测试入口集合定义。 Verification test entry-set definition.
 * @details 作用：
 *          1. 显式列出 verify 入口。
 *          2. 提供主 Linux 测试 runner 可直接调用的顺序执行 helper。
 *          Purpose:
 *          1. Explicitly list the verification entrypoints.
 *          2. Provide a sequential helper directly callable from the main Linux test runner.
 */
#pragma once

#include "../verify/environment/linux_shm/test_verify.hpp"
inline int RunVerifyLinuxShmSet()
{
  test_linux_shm_topic();
  return 0;
}
