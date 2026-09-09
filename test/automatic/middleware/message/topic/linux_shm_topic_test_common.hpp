/**
 * @file linux_shm_topic_test_common.hpp
 * @brief 共享 Topic 测试辅助 / Helpers for shared Topic tests.
 *
 * 提供 LinuxSharedTopic 测试数据、主题命名、等待及子进程退出检查。
 * Provide LinuxSharedTopic test frames, names, waits and child-exit checks.
 */

#pragma once

#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cstdio>
#include <cstdlib>

#include "libxr.hpp"
#include "test.hpp"
#include "test_assert.hpp"

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

inline uint32_t ComputeChecksum(const IPCFrame& frame)
{
  uint32_t sum = frame.seq;
  for (uint8_t byte : frame.payload)
  {
    sum = sum * 131U + byte;
  }
  return sum;
}

inline void FillFrame(IPCFrame& frame, uint32_t seq)
{
  frame.seq = seq;
  for (size_t i = 0; i < frame.payload.size(); ++i)
  {
    frame.payload[i] = static_cast<uint8_t>((seq + i * 3U) & 0xFFU);
  }
  frame.checksum = ComputeChecksum(frame);
}

inline void AssertFrame(const IPCFrame& frame, uint32_t expected_seq)
{
  TEST_ASSERT(frame.seq == expected_seq);
  TEST_ASSERT(frame.checksum == ComputeChecksum(frame));
}

inline void MakeTopicName(char* topic_name, size_t topic_name_size, const char* prefix)
{
  std::snprintf(topic_name, topic_name_size, "%s_%d", prefix, static_cast<int>(getpid()));
}

inline void WaitForSubscriberNum(SharedTopic& topic, uint32_t expected_num)
{
  for (int retry = 0; retry < 200 && topic.GetSubscriberNum() < expected_num; ++retry)
  {
    usleep(10000);
  }
  TEST_ASSERT(topic.GetSubscriberNum() == expected_num);
}

inline void ExpectChildExit(pid_t child, int expected_code = 0)
{
  int status = 0;
  TEST_ASSERT(waitpid(child, &status, 0) == child);
  TEST_ASSERT(WIFEXITED(status));
  TEST_ASSERT(WEXITSTATUS(status) == expected_code);
}

void RunAttachQueueScenarios();
void RunLifecycleScenarios();
void RunBalanceRoundRobinScenarios();
void RunMixedModeScenarios();
void RunCrossProcessScenarios();
}  // namespace LinuxShmTopicTest
