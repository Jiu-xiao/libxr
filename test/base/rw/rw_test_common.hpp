/**
 * @file rw_test_common.hpp
 * @brief RW 与 Pipe 基础测试声明 / Declarations for base RW and Pipe tests.
 */
#pragma once

#include "../../common/rw/pipe_test_common.hpp"

void RunBaseRwReadQueueTests();
void RunBaseRwPendingTests();
void RunBaseRwBlockTests();
void RunBasePipeBasicTests();
void RunBasePipeStreamTests();
void RunBasePipeStressTests();
