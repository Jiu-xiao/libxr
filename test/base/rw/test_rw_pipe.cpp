/**
 * @file test_rw_pipe.cpp
 * @brief RW 与 Pipe 基础测试入口 / Base RW and Pipe test entry points.
 */
#include "rw_test_common.hpp"

/**
 * @brief 运行 RW 基础测试 / Run base RW tests.
 */
void test_rw()
{
  RunBaseRwReadQueueTests();
  RunBaseRwPendingTests();
}

/**
 * @brief 运行 Pipe 基础、流与压力测试 / Run Pipe basic, stream, and stress tests.
 */
void test_pipe()
{
  RunBasePipeBasicTests();
  RunBasePipeStreamTests();
  RunBasePipeStressTests();
}
