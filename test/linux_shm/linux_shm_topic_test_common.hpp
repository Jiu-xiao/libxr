/**
 * @file linux_shm_topic_test_common.hpp
 * @brief `LinuxSharedTopic` 环境验证共用 helper。 Shared helpers for `LinuxSharedTopic` environment verification.
 *
 * 作用 / Purpose:
 * 1. 集中跨进程验证共用的 payload、别名、命名和断言 helper。
 *    Centralize shared payload, aliases, naming helpers, and assertions used by cross-process verification.
 * 2. 让拆分后的各个场景文件只保留自己的验证语义。
 *    Keep each split scenario file focused on its own verification semantics.
 */
#pragma once

#include <array>
#include <cstdio>
#include <cstdlib>

#include <sys/wait.h>
#include <unistd.h>

#include "libxr.hpp"
#include "test.hpp"

namespace LinuxShmTopicTest
{
constexpr uint32_t SHORT_WAIT_MS = 100;
constexpr uint32_t LONG_WAIT_MS = 2000;

struct IPCFrame
{
  uint32_t seq = 0;
  uint32_t checksum = 0;
  std::array<uint8_t, 128> payload = {};
};

using SharedTopic = LibXR::LinuxSharedTopic<IPCFrame>;
using SharedData = SharedTopic::Data;
using SharedSubscriber = SharedTopic::SyncSubscriber;

/**
 * @brief 辅助函数 `ComputeChecksum`。 Helper function `ComputeChecksum`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
inline uint32_t ComputeChecksum(const IPCFrame& frame)
{
  uint32_t sum = frame.seq;
  for (uint8_t byte : frame.payload)
  {
    sum = sum * 131U + byte;
  }
  return sum;
}

/**
 * @brief 辅助函数 `FillFrame`。 Helper function `FillFrame`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
inline void FillFrame(IPCFrame& frame, uint32_t seq)
{
  // 辅助内容：为后续测试准备或校验共享状态。
  // Helper coverage: prepare or validate shared state for later tests.
  frame.seq = seq;
  for (size_t i = 0; i < frame.payload.size(); ++i)
  {
    frame.payload[i] = static_cast<uint8_t>((seq + i * 3U) & 0xFFU);
  }
  frame.checksum = ComputeChecksum(frame);
}

/**
 * @brief 断言辅助函数 `AssertFrame`。 Assertion helper function `AssertFrame`.
 * @details 测试内容：对当前结果施加统一的期望检查。 Apply one unified expectation check to the current result.
 *          测试原理：把重复判定逻辑收口，避免各测试项使用不一致的检查标准。 Concentrate repeated validation logic so test items do not drift to inconsistent checks.
 */
inline void AssertFrame(const IPCFrame& frame, uint32_t expected_seq)
{
  // 辅助内容：为后续测试准备或校验共享状态。
  // Helper coverage: prepare or validate shared state for later tests.
  ASSERT(frame.seq == expected_seq);
  ASSERT(frame.checksum == ComputeChecksum(frame));
}

/**
 * @brief 辅助函数 `MakeTopicName`。 Helper function `MakeTopicName`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
inline void MakeTopicName(char* topic_name, size_t topic_name_size, const char* prefix)
{
  // 辅助内容：为后续测试准备或校验共享状态。
  // Helper coverage: prepare or validate shared state for later tests.
  std::snprintf(topic_name, topic_name_size, "%s_%d", prefix, static_cast<int>(getpid()));
}

/**
 * @brief 辅助函数 `WaitForSubscriberNum`。 Helper function `WaitForSubscriberNum`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
inline void WaitForSubscriberNum(SharedTopic& topic, uint32_t expected_num)
{
  // 辅助内容：为后续测试准备或校验共享状态。
  // Helper coverage: prepare or validate shared state for later tests.
  for (int retry = 0; retry < 200 && topic.GetSubscriberNum() < expected_num; ++retry)
  {
    usleep(10000);
  }
  ASSERT(topic.GetSubscriberNum() == expected_num);
}

/**
 * @brief 断言辅助函数 `ExpectChildExit`。 Assertion helper function `ExpectChildExit`.
 * @details 测试内容：对当前结果施加统一的期望检查。 Apply one unified expectation check to the current result.
 *          测试原理：把重复判定逻辑收口，避免各测试项使用不一致的检查标准。 Concentrate repeated validation logic so test items do not drift to inconsistent checks.
 */
inline void ExpectChildExit(pid_t child, int expected_code = 0)
{
  // 辅助内容：验证当前失败或退出预期。
  // Helper coverage: validate the current expected failure or exit condition.
  int status = 0;
  ASSERT(waitpid(child, &status, 0) == child);
  ASSERT(WIFEXITED(status));
  ASSERT(WEXITSTATUS(status) == expected_code);
}

void RunAttachQueueScenarios();
void RunLifecycleScenarios();
void RunBalanceRoundRobinScenarios();
void RunMixedModeScenarios();
void RunCrossProcessScenarios();
}  // namespace LinuxShmTopicTest
