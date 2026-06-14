/**
 * @file test_main_sets.hpp
 * @brief 主测试二进制固定运行集合定义。 Fixed runtime-set definitions for the main `test` binary.
 * @details 作用：
 *          1. 显式列出主测试二进制在 `bare_metal` / `rtos` / `full_os` 三个集合下的可运行 case。
 *          2. 保持主 runner 的执行顺序清晰，不引入额外 manifest/resolver 层。
 *          Purpose:
 *          1. Explicitly list which cases belong to the main test binary under `bare_metal` / `rtos` / `full_os`.
 *          2. Keep the main runner order explicit without an extra manifest/resolver layer.
 */
#pragma once

#include "test_base.hpp"
#include "test_case_runner.hpp"
#include "test_runtime_set.hpp"

struct GroupedTestCase
{
  const char* group;
  TestRuntimeSet minimum_runtime;
  TestCase test_case;
};

inline constexpr GroupedTestCase kMainTestCases[] = {
    {"core_tests", TestRuntimeSet::BARE_METAL, {"assert", &RunVoidEntry<test_assert>, false}},
    {"core_tests", TestRuntimeSet::BARE_METAL, {"def", &RunVoidEntry<test_def>, false}},
    {"core_tests", TestRuntimeSet::BARE_METAL, {"callback", &RunVoidEntry<test_cb>, false}},
    {"core_tests", TestRuntimeSet::BARE_METAL, {"pipe", &RunVoidEntry<test_pipe>, false}},
    {"core_tests", TestRuntimeSet::BARE_METAL, {"rw", &RunVoidEntry<test_rw>, false}},
    {"core_tests", TestRuntimeSet::BARE_METAL, {"memory", &RunVoidEntry<test_memory>, false}},
    {"core_tests", TestRuntimeSet::BARE_METAL, {"color", &RunVoidEntry<test_color>, false}},
    {"core_tests", TestRuntimeSet::BARE_METAL, {"time", &RunVoidEntry<test_time>, false}},

    {"synchronization_tests", TestRuntimeSet::RTOS,
     {"semaphore", &RunVoidEntry<test_semaphore>, false}},
    {"synchronization_tests", TestRuntimeSet::RTOS,
     {"mutex", &RunVoidEntry<test_mutex>, false}},
    {"synchronization_tests", TestRuntimeSet::RTOS,
     {"async", &RunVoidEntry<test_async>, false}},

    {"utility_tests", TestRuntimeSet::BARE_METAL, {"crc", &RunVoidEntry<test_crc>, false}},
    {"utility_tests", TestRuntimeSet::BARE_METAL,
     {"encoder", &RunVoidEntry<test_float_encoder>, false}},
    {"utility_tests", TestRuntimeSet::BARE_METAL,
     {"cycle_value", &RunVoidEntry<test_cycle_value>, false}},
    {"utility_tests", TestRuntimeSet::BARE_METAL, {"print", &RunVoidEntry<test_print>, false}},
    {"utility_tests", TestRuntimeSet::BARE_METAL, {"flag", &RunVoidEntry<test_flag>, false}},

    {"data_structure_tests", TestRuntimeSet::BARE_METAL,
     {"rbt", &RunVoidEntry<test_rbt>, false}},
    {"data_structure_tests", TestRuntimeSet::BARE_METAL,
     {"queue", &RunVoidEntry<test_queue>, false}},
    {"data_structure_tests", TestRuntimeSet::RTOS,
     {"spsc_queue", &RunVoidEntry<test_spsc_queue>, false}},
    {"data_structure_tests", TestRuntimeSet::RTOS,
     {"mpmc_queue", &RunVoidEntry<test_mpmc_queue>, false}},
    {"data_structure_tests", TestRuntimeSet::BARE_METAL,
     {"pool", &RunVoidEntry<test_lock_free_pool>, false}},
    {"data_structure_tests", TestRuntimeSet::BARE_METAL,
     {"object_pool", &RunVoidEntry<test_object_pool>, false}},
    {"data_structure_tests", TestRuntimeSet::BARE_METAL,
     {"stack", &RunVoidEntry<test_stack>, false}},
    {"data_structure_tests", TestRuntimeSet::BARE_METAL,
     {"list", &RunVoidEntry<test_list>, false}},
    {"data_structure_tests", TestRuntimeSet::BARE_METAL,
     {"double_buffer", &RunVoidEntry<test_double_buffer>, false}},
    {"data_structure_tests", TestRuntimeSet::BARE_METAL,
     {"type", &RunVoidEntry<test_type>, false}},
    {"data_structure_tests", TestRuntimeSet::BARE_METAL,
     {"string", &RunVoidEntry<test_string>, false}},

    {"threading_tests", TestRuntimeSet::RTOS,
     {"thread", &RunVoidEntry<test_thread>, false}},
    {"threading_tests", TestRuntimeSet::BARE_METAL,
     {"timebase", &RunVoidEntry<test_timebase>, false}},
    {"threading_tests", TestRuntimeSet::BARE_METAL,
     {"timer", &RunVoidEntry<test_timer>, false}},

    {"runtime_tests", TestRuntimeSet::RTOS,
     {"rw_runtime", &RunVoidEntry<test_rw_runtime>, false}},
    {"runtime_tests", TestRuntimeSet::RTOS,
     {"pipe_runtime", &RunVoidEntry<test_pipe_runtime>, false}},
    {"runtime_tests", TestRuntimeSet::RTOS,
     {"message_runtime", &RunVoidEntry<test_message_runtime>, false}},

    {"motion_tests", TestRuntimeSet::BARE_METAL,
     {"inertia", &RunVoidEntry<test_inertia>, false}},
    {"motion_tests", TestRuntimeSet::BARE_METAL,
     {"kinematic", &RunVoidEntry<test_kinematic>, false}},
    {"motion_tests", TestRuntimeSet::BARE_METAL,
     {"transform", &RunVoidEntry<test_transform>, false}},

    {"control_tests", TestRuntimeSet::BARE_METAL, {"pid", &RunVoidEntry<test_pid>, false}},

    {"system_tests", TestRuntimeSet::BARE_METAL, {"ramfs", &RunVoidEntry<test_ramfs>, false}},
    {"system_tests", TestRuntimeSet::BARE_METAL,
     {"app_framework_application", &RunVoidEntry<test_app_framework_application>, false}},
    {"system_tests", TestRuntimeSet::BARE_METAL,
     {"app_framework_hardware", &RunVoidEntry<test_app_framework_hardware>, false}},
    {"system_tests", TestRuntimeSet::BARE_METAL,
     {"event", &RunVoidEntry<test_event>, false}},
    {"system_tests", TestRuntimeSet::BARE_METAL,
     {"message_topic", &RunVoidEntry<test_message_topic>, false}},
    {"system_tests", TestRuntimeSet::BARE_METAL,
     {"message_packet", &RunVoidEntry<test_message_packet>, false}},
    {"system_tests", TestRuntimeSet::BARE_METAL,
     {"database", &RunVoidEntry<test_database>, false}},
    {"system_tests", TestRuntimeSet::FULL_OS, {"logger", &RunVoidEntry<test_logger>, true}},
    {"system_tests", TestRuntimeSet::FULL_OS,
     {"terminal_command", &RunVoidEntry<test_terminal_command>, true}},
    {"system_tests", TestRuntimeSet::BARE_METAL,
     {"terminal_display", &RunVoidEntry<test_terminal_display>, false}},
    {"system_tests", TestRuntimeSet::FULL_OS,
     {"terminal_input", &RunVoidEntry<test_terminal_input>, true}},
    {"system_tests", TestRuntimeSet::FULL_OS,
     {"terminal", &RunVoidEntry<test_terminal>, true}},
};

inline int RunMainTestBinary()
{
  TestRuntimeSet runtime_set;
  const LibXR::ErrorCode load_result = LoadRuntimeSetFromEnv(runtime_set);
  if (!IsOk(load_result))
  {
    return ErrorCodeToExitStatus(load_result);
  }

  std::fprintf(stderr, "Running LibXR Tests...\n");
  std::fflush(stderr);

  const char* current_group = nullptr;
  size_t matched = 0;
  for (const auto& entry : kMainTestCases)
  {
    if (static_cast<int>(entry.minimum_runtime) > static_cast<int>(runtime_set))
    {
      continue;
    }

    ++matched;
    if (current_group == nullptr || std::strcmp(current_group, entry.group) != 0)
    {
      current_group = entry.group;
      std::fprintf(stderr, "Test Group [%s]\n", current_group);
      std::fflush(stderr);
    }

    run_test_case(entry.test_case);
    TEST_STEP(entry.test_case.name);
  }

  if (matched == 0)
  {
    std::fprintf(stderr, "no matching main tests for XR_TEST_SET=%s\n",
                 RuntimeSetName(runtime_set));
    return ErrorCodeToExitStatus(LibXR::ErrorCode::NOT_FOUND);
  }

  std::fprintf(stderr, "All tests completed.\n");
  std::fflush(stderr);
  return ErrorCodeToExitStatus(LibXR::ErrorCode::OK);
}
