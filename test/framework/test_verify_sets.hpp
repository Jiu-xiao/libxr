/**
 * @file test_verify_sets.hpp
 * @brief verify 测试固定运行集合定义。 Fixed runtime-set definitions for verification tests.
 * @details 作用：
 *          1. 显式列出 verify 入口。
 *          2. 强制 verify 只允许在 `full_os` 集合下运行。
 *          Purpose:
 *          1. Explicitly list the verification entrypoints.
 *          2. Enforce that verification runs only under the `full_os` set.
 */
#pragma once

#include "../verify/environment/linux_shm/test_verify.hpp"
#include "test_case_runner.hpp"
#include "test_runtime_set.hpp"

inline int RunVerifyLinuxShmBinary()
{
  TestRuntimeSet runtime_set;
  const LibXR::ErrorCode load_result = LoadRuntimeSetFromEnv(runtime_set);
  if (!IsOk(load_result))
  {
    return ErrorCodeToExitStatus(load_result);
  }
  const LibXR::ErrorCode require_result = RequireRuntimeSet(
      runtime_set, TestRuntimeSet::FULL_OS, "test_linux_shm_topic");
  if (!IsOk(require_result))
  {
    return ErrorCodeToExitStatus(require_result);
  }

  test_linux_shm_topic();
  return ErrorCodeToExitStatus(LibXR::ErrorCode::OK);
}
