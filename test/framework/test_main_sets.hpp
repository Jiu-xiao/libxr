/**
 * @file test_main_sets.hpp
 * @brief Linux 主测试二进制固定执行顺序定义。 Fixed execution order for the Linux main `test` binary.
 * @details 作用：
 *          1. 显式列出 Linux 主测试二进制要执行的 case。
 *          2. 保持主 runner 的执行顺序清晰，不再按 `bare_metal` / `rtos` / `full_os` 切分运行集合。
 *          Purpose:
 *          1. Explicitly list the cases executed by the Linux main `test` binary.
 *          2. Keep the execution order clear without splitting runtime sets into `bare_metal`, `rtos`, and `full_os`.
 */
#pragma once

#include "../linux_bench/linux_shared_topic_bench_common.hpp"
#include "test_base.hpp"
#include "test_case_runner.hpp"

void test_linux_stdio_print();
void test_linux_database_raw();
void test_linux_database_sequential();
void test_linux_shm_topic();

enum class BenchSelector
{
  Smoke,
  All,
  Standard,
  Latency,
  Overload,
  Modes,
};

inline int RunLinuxStdioAndDatabaseSet()
{
  static constexpr TestCase kLinuxHostTests[] = {
      {"linux_stdio_print", &RunVoidEntry<test_linux_stdio_print>, false},
      {"linux_database_sequential", &RunVoidEntry<test_linux_database_sequential>, false},
      {"linux_database_raw", &RunVoidEntry<test_linux_database_raw>, false},
  };

  for (const auto& test_case : kLinuxHostTests)
  {
    run_test_case(test_case);
  }
  return 0;
}

inline int RunLinuxShmSet()
{
  test_linux_shm_topic();
  return 0;
}

inline int RunBenchLinuxSharedTopicSet(BenchSelector selector)
{
  int status = 0;
  switch (selector)
  {
    case BenchSelector::Smoke:
      status |= LinuxSharedTopicBench::RunStandardBenchmarksSmoke();
      status |= LinuxSharedTopicBench::RunLatencyBenchmarksSmoke();
      status |= LinuxSharedTopicBench::RunOverloadBenchmarksSmoke();
      status |= LinuxSharedTopicBench::RunModeBenchmarksSmoke();
      break;
    case BenchSelector::All:
      status |= LinuxSharedTopicBench::RunStandardBenchmarks();
      status |= LinuxSharedTopicBench::RunLatencyBenchmarks();
      status |= LinuxSharedTopicBench::RunOverloadBenchmarks();
      status |= LinuxSharedTopicBench::RunModeBenchmarks();
      break;
    case BenchSelector::Standard:
      status |= LinuxSharedTopicBench::RunStandardBenchmarks();
      break;
    case BenchSelector::Latency:
      status |= LinuxSharedTopicBench::RunLatencyBenchmarks();
      break;
    case BenchSelector::Overload:
      status |= LinuxSharedTopicBench::RunOverloadBenchmarks();
      break;
    case BenchSelector::Modes:
      status |= LinuxSharedTopicBench::RunModeBenchmarks();
      break;
  }
  return status;
}

inline int RunBenchLinuxSharedTopicDefaultSet()
{
  return RunBenchLinuxSharedTopicSet(BenchSelector::Smoke);
}

struct GroupedTestCase
{
  const char* group;
  TestCase test_case;
};

inline constexpr GroupedTestCase kMainTestCases[] = {
    {"core_tests", {"assert", &RunVoidEntry<test_assert>, false}},
    {"core_tests", {"def", &RunVoidEntry<test_def>, false}},
    {"core_tests", {"callback", &RunVoidEntry<test_cb>, false}},
    {"core_tests", {"pipe", &RunVoidEntry<test_pipe>, false}},
    {"core_tests", {"rw", &RunVoidEntry<test_rw>, false}},
    {"core_tests", {"memory", &RunVoidEntry<test_memory>, false}},
    {"core_tests", {"color", &RunVoidEntry<test_color>, false}},
    {"core_tests", {"time", &RunVoidEntry<test_time>, false}},

    {"synchronization_tests", {"semaphore", &RunVoidEntry<test_semaphore>, false}},
    {"synchronization_tests", {"mutex", &RunVoidEntry<test_mutex>, false}},
    {"synchronization_tests", {"async", &RunVoidEntry<test_async>, false}},

    {"utility_tests", {"crc", &RunVoidEntry<test_crc>, false}},
    {"utility_tests", {"encoder", &RunVoidEntry<test_float_encoder>, false}},
    {"utility_tests", {"cycle_value", &RunVoidEntry<test_cycle_value>, false}},
    {"utility_tests", {"print", &RunVoidEntry<test_print>, false}},
    {"utility_tests", {"flag", &RunVoidEntry<test_flag>, false}},

    {"data_structure_tests", {"rbt", &RunVoidEntry<test_rbt>, false}},
    {"data_structure_tests", {"queue", &RunVoidEntry<test_queue>, false}},
    {"data_structure_tests", {"spsc_queue", &RunVoidEntry<test_spsc_queue>, false}},
    {"data_structure_tests", {"mpmc_queue", &RunVoidEntry<test_mpmc_queue>, false}},
    {"data_structure_tests", {"pool", &RunVoidEntry<test_lock_free_pool>, false}},
    {"data_structure_tests", {"object_pool", &RunVoidEntry<test_object_pool>, false}},
    {"data_structure_tests", {"stack", &RunVoidEntry<test_stack>, false}},
    {"data_structure_tests", {"list", &RunVoidEntry<test_list>, false}},
    {"data_structure_tests", {"double_buffer", &RunVoidEntry<test_double_buffer>, false}},
    {"data_structure_tests", {"type", &RunVoidEntry<test_type>, false}},
    {"data_structure_tests", {"string", &RunVoidEntry<test_string>, false}},

    {"threading_tests", {"thread", &RunVoidEntry<test_thread>, false}},
    {"threading_tests", {"timebase", &RunVoidEntry<test_timebase>, false}},
    {"threading_tests", {"timer", &RunVoidEntry<test_timer>, false}},

    {"runtime_tests", {"rw_runtime", &RunVoidEntry<test_rw_runtime>, false}},
    {"runtime_tests", {"pipe_runtime", &RunVoidEntry<test_pipe_runtime>, false}},
    {"runtime_tests", {"message_runtime", &RunVoidEntry<test_message_runtime>, false}},

    {"motion_tests", {"inertia", &RunVoidEntry<test_inertia>, false}},
    {"motion_tests", {"kinematic", &RunVoidEntry<test_kinematic>, false}},
    {"motion_tests", {"transform", &RunVoidEntry<test_transform>, false}},

    {"control_tests", {"pid", &RunVoidEntry<test_pid>, false}},

    {"system_tests", {"ramfs", &RunVoidEntry<test_ramfs>, false}},
    {"system_tests", {"app_framework_application", &RunVoidEntry<test_app_framework_application>, false}},
    {"system_tests", {"app_framework_hardware", &RunVoidEntry<test_app_framework_hardware>, false}},
    {"system_tests", {"event", &RunVoidEntry<test_event>, false}},
    {"system_tests", {"message_topic", &RunVoidEntry<test_message_topic>, false}},
    {"system_tests", {"message_packet", &RunVoidEntry<test_message_packet>, false}},
    {"system_tests", {"database", &RunVoidEntry<test_database>, false}},
    {"linux_host_tests", {"stdio_and_database", &RunLinuxStdioAndDatabaseSet, false}},
    {"system_tests", {"logger", &RunVoidEntry<test_logger>, true}},
    {"system_tests", {"linux_shm_topic", &RunLinuxShmSet, false}},
    {"system_tests", {"linux_shm_bench", &RunBenchLinuxSharedTopicDefaultSet, false}},
    {"system_tests", {"terminal_command", &RunVoidEntry<test_terminal_command>, true}},
    {"system_tests", {"terminal_display", &RunVoidEntry<test_terminal_display>, false}},
    {"system_tests", {"terminal_input", &RunVoidEntry<test_terminal_input>, true}},
    {"system_tests", {"terminal", &RunVoidEntry<test_terminal>, true}},
};

inline int RunMainTestBinary()
{
  std::fprintf(stderr, "Running LibXR Tests...\n");
  std::fflush(stderr);

  const char* current_group = nullptr;
  for (const auto& entry : kMainTestCases)
  {
    if (current_group == nullptr || std::strcmp(current_group, entry.group) != 0)
    {
      current_group = entry.group;
      std::fprintf(stderr, "Test Group [%s]\n", current_group);
      std::fflush(stderr);
    }

    run_test_case(entry.test_case);
    TEST_STEP(entry.test_case.name);
  }

  std::fprintf(stderr, "All tests completed.\n");
  std::fflush(stderr);
  return 0;
}
