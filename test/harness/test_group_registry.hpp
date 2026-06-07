/**
 * @file test_group_registry.hpp
 * @brief base/runtime 测试分组注册表。 Test-group registry for the base/runtime harness.
 * @details 作用：
 *          1. 显式列出主测试二进制的分组矩阵和执行顺序。
 *          2. 让执行矩阵与测试树结构在一个集中位置保持同步。
 *          Purpose:
 *          1. Explicitly list the group matrix and execution order of the main test binary.
 *          2. Keep the execution matrix synchronized with the test-tree structure in one place.
 */
#pragma once

#include "test_base.hpp"
#include "test_case_runner.hpp"

/**
 * @brief 辅助函数 `run_libxr_tests`。 Helper function `run_libxr_tests`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把测试分组矩阵集中在一个入口里，保证 runner 和测试树结构不会漂移。 Keep the test-group matrix in one place so the runner and test-tree structure do not drift apart.
 */
inline void run_libxr_tests()
{
  XR_LOG_INFO("Running LibXR Tests...\n");

  TestCase core_tests[] = {{"assert", test_assert, false},
                           {"def", test_def, false},
                           {"callback", test_cb, false},
                           {"pipe", test_pipe, false},
                           {"rw", test_rw, false},
                           {"memory", test_memory, false},
                           {"color", test_color, false},
                           {"time", test_time, false}};

  TestCase synchronization_tests[] = {{"semaphore", test_semaphore, false},
                                      {"mutex", test_mutex, false},
                                      {"async", test_async, false}};

  TestCase utility_tests[] = {{"crc", test_crc, false},
                              {"encoder", test_float_encoder, false},
                              {"cycle_value", test_cycle_value, false},
                              {"print", test_print, false},
                              {"flag", test_flag, false}};

  TestCase data_structure_tests[] = {{"rbt", test_rbt, false},
                                     {"queue", test_queue, false},
                                     {"lockfree_queue", test_lockfree_queue, false},
                                     {"pool", test_lock_free_pool, false},
                                     {"lockfree_list", test_lockfree_list, false},
                                     {"stack", test_stack, false},
                                     {"list", test_list, false},
                                     {"double_buffer", test_double_buffer, false},
                                     {"type", test_type, false},
                                     {"string", test_string, false}};

  TestCase threading_tests[] = {{"thread", test_thread, false},
                                {"timebase", test_timebase, false},
                                {"timer", test_timer, false}};

  TestCase runtime_tests[] = {{"rw_runtime", test_rw_runtime, false},
                              {"pipe_runtime", test_pipe_runtime, false},
                              {"message_runtime", test_message_runtime, false}};

  TestCase motion_tests[] = {{"inertia", test_inertia, false},
                             {"kinematic", test_kinematic, false},
                             {"transform", test_transform, false}};

  TestCase control_tests[] = {{"pid", test_pid, false}};

  TestCase system_tests[] = {{"ramfs", test_ramfs, false},
                             {"app_framework_application", test_app_framework_application,
                              false},
                             {"app_framework_hardware", test_app_framework_hardware, false},
                             {"event", test_event, false},
                             {"message_topic", test_message_topic, false},
                             {"message_packet", test_message_packet, false},
                             {"database", test_database, false},
                             {"logger", test_logger, true},
                             {"terminal_command", test_terminal_command, true},
                             {"terminal_display", test_terminal_display, false},
                             {"terminal_input", test_terminal_input, true},
                             {"terminal", test_terminal, true}};

  struct TestGroup
  {
    TestCase* tests;
    size_t size;
    const char* name;
  };

  TestGroup test_groups[] = {
      {core_tests, sizeof(core_tests) / sizeof(TestCase), "core_tests"},
      {synchronization_tests, sizeof(synchronization_tests) / sizeof(TestCase),
       "synchronization_tests"},
      {utility_tests, sizeof(utility_tests) / sizeof(TestCase), "utility_tests"},
      {data_structure_tests, sizeof(data_structure_tests) / sizeof(TestCase),
       "data_structure_tests"},
      {threading_tests, sizeof(threading_tests) / sizeof(TestCase), "threading_tests"},
      {runtime_tests, sizeof(runtime_tests) / sizeof(TestCase), "runtime_tests"},
      {motion_tests, sizeof(motion_tests) / sizeof(TestCase), "motion_tests"},
      {control_tests, sizeof(control_tests) / sizeof(TestCase), "control_tests"},
      {system_tests, sizeof(system_tests) / sizeof(TestCase), "system_tests"},
  };

  for (const auto& group : test_groups)
  {
    XR_LOG_INFO("Test Group [%s]\n", group.name);
    for (size_t i = 0; i < group.size; ++i)
    {
      run_test_case(group.tests[i]);
      TEST_STEP(group.tests[i].name);
    }
  }

  XR_LOG_INFO("All tests completed.\n");
  std::fprintf(stderr, "All tests completed.\n");
  std::fflush(stderr);
}
