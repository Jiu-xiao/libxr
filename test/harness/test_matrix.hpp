/**
 * @file test_matrix.hpp
 * @brief test 二进制统一入口矩阵。 Unified entry matrix for test binaries.
 * @details 作用：
 *          1. 以单一事实源列出 `test` / `test_binding` / `verify` / `measure` 四类入口。
 *          2. 让主 runner、binding runner、verify runner、benchmark runner 都从同一份注册表取入口。
 *          3. 把入口的 binary/group/module/plane 元信息固定下来，避免文件拆分后 runner 漂移。
 *          Purpose:
 *          1. List the four entry families of `test` / `test_binding` / `verify` / `measure` from one source of truth.
 *          2. Make the main, binding, verify, and benchmark runners consume one shared registry.
 *          3. Fix binary/group/module/plane metadata at the entry boundary so runner wiring does not drift after file splits.
 */
#pragma once

#include <cstring>

#include "test_base.hpp"
#include "test_binding.hpp"
#include "test_case_runner.hpp"
#include "../measure/perf/linux_shared_topic_bench_common.hpp"
#include "../verify/environment/linux_shm/test_verify.hpp"

enum class TestBinary
{
  MAIN,
  BINDING,
  VERIFY_LINUX_SHM,
  BENCH_LINUX_SHARED_TOPIC,
};

enum class TestPlane
{
  MAIN_HOST,
  BINDING_HOST,
  VERIFY_ENVIRONMENT,
  MEASURE_PERF,
};

struct TestEntryMeta
{
  const char* id;
  const char* group;
  TestPlane plane;
  const char* module;
  bool isolated;
  const char* selector;
  int (*run)();
};

template <void (*Fn)()>
int RunVoidEntry()
{
  Fn();
  return 0;
}

#define XR_MATRIX_MAIN_ENTRY_LIST(X)                                                         \
  X("assert", "core_tests", TestPlane::MAIN_HOST, "base/core", false, test_assert)          \
  X("def", "core_tests", TestPlane::MAIN_HOST, "base/core", false, test_def)                \
  X("callback", "core_tests", TestPlane::MAIN_HOST, "base/core", false, test_cb)            \
  X("pipe", "core_tests", TestPlane::MAIN_HOST, "base/core/rw", false, test_pipe)           \
  X("rw", "core_tests", TestPlane::MAIN_HOST, "base/core/rw", false, test_rw)               \
  X("memory", "core_tests", TestPlane::MAIN_HOST, "base/core", false, test_memory)          \
  X("color", "core_tests", TestPlane::MAIN_HOST, "base/core", false, test_color)            \
  X("time", "core_tests", TestPlane::MAIN_HOST, "base/core", false, test_time)              \
  X("semaphore", "synchronization_tests", TestPlane::MAIN_HOST, "runtime/system", false,    \
    test_semaphore)                                                                          \
  X("mutex", "synchronization_tests", TestPlane::MAIN_HOST, "runtime/system", false,        \
    test_mutex)                                                                              \
  X("async", "synchronization_tests", TestPlane::MAIN_HOST, "runtime/system", false,        \
    test_async)                                                                              \
  X("crc", "utility_tests", TestPlane::MAIN_HOST, "base/utils", false, test_crc)            \
  X("encoder", "utility_tests", TestPlane::MAIN_HOST, "base/utils", false,                  \
    test_float_encoder)                                                                      \
  X("cycle_value", "utility_tests", TestPlane::MAIN_HOST, "base/utils", false,              \
    test_cycle_value)                                                                        \
  X("print", "utility_tests", TestPlane::MAIN_HOST, "base/core/print", false, test_print)   \
  X("flag", "utility_tests", TestPlane::MAIN_HOST, "base/utils", false, test_flag)          \
  X("rbt", "data_structure_tests", TestPlane::MAIN_HOST, "base/structure", false, test_rbt) \
  X("queue", "data_structure_tests", TestPlane::MAIN_HOST, "base/structure", false,         \
    test_queue)                                                                              \
  X("lockfree_queue", "data_structure_tests", TestPlane::MAIN_HOST, "base/structure",       \
    false, test_lockfree_queue)                                                              \
  X("pool", "data_structure_tests", TestPlane::MAIN_HOST, "base/structure", false,          \
    test_lock_free_pool)                                                                     \
  X("lockfree_list", "data_structure_tests", TestPlane::MAIN_HOST, "base/structure", false, \
    test_lockfree_list)                                                                      \
  X("stack", "data_structure_tests", TestPlane::MAIN_HOST, "base/structure", false,         \
    test_stack)                                                                              \
  X("list", "data_structure_tests", TestPlane::MAIN_HOST, "base/structure", false,          \
    test_list)                                                                               \
  X("double_buffer", "data_structure_tests", TestPlane::MAIN_HOST, "base/structure", false, \
    test_double_buffer)                                                                      \
  X("type", "data_structure_tests", TestPlane::MAIN_HOST, "base/core", false, test_type)    \
  X("string", "data_structure_tests", TestPlane::MAIN_HOST, "base/core", false,             \
    test_string)                                                                             \
  X("thread", "threading_tests", TestPlane::MAIN_HOST, "runtime/system", false, test_thread) \
  X("timebase", "threading_tests", TestPlane::MAIN_HOST, "runtime/system", false,           \
    test_timebase)                                                                           \
  X("timer", "threading_tests", TestPlane::MAIN_HOST, "runtime/system", false, test_timer)  \
  X("rw_runtime", "runtime_tests", TestPlane::MAIN_HOST, "runtime/core/rw", false,          \
    test_rw_runtime)                                                                         \
  X("pipe_runtime", "runtime_tests", TestPlane::MAIN_HOST, "runtime/core/rw", false,        \
    test_pipe_runtime)                                                                       \
  X("message_runtime", "runtime_tests", TestPlane::MAIN_HOST,                               \
    "runtime/middleware/message", false, test_message_runtime)                              \
  X("inertia", "motion_tests", TestPlane::MAIN_HOST, "base/utils", false, test_inertia)     \
  X("kinematic", "motion_tests", TestPlane::MAIN_HOST, "base/utils", false, test_kinematic) \
  X("transform", "motion_tests", TestPlane::MAIN_HOST, "base/utils", false, test_transform) \
  X("pid", "control_tests", TestPlane::MAIN_HOST, "base/utils", false, test_pid)            \
  X("ramfs", "system_tests", TestPlane::MAIN_HOST, "base/middleware/ramfs", false,          \
    test_ramfs)                                                                              \
  X("app_framework_application", "system_tests", TestPlane::MAIN_HOST,                      \
    "base/middleware/app_framework", false, test_app_framework_application)                  \
  X("app_framework_hardware", "system_tests", TestPlane::MAIN_HOST,                         \
    "base/middleware/app_framework", false, test_app_framework_hardware)                     \
  X("event", "system_tests", TestPlane::MAIN_HOST, "base/middleware/event", false,          \
    test_event)                                                                              \
  X("message_topic", "system_tests", TestPlane::MAIN_HOST, "base/middleware/message",       \
    false, test_message_topic)                                                               \
  X("message_packet", "system_tests", TestPlane::MAIN_HOST, "base/middleware/message",      \
    false, test_message_packet)                                                              \
  X("database", "system_tests", TestPlane::MAIN_HOST, "base/middleware/database", false,    \
    test_database)                                                                           \
  X("logger", "system_tests", TestPlane::MAIN_HOST, "base/middleware/logger", true,         \
    test_logger)                                                                             \
  X("terminal_command", "system_tests", TestPlane::MAIN_HOST, "base/middleware/terminal",   \
    true, test_terminal_command)                                                             \
  X("terminal_display", "system_tests", TestPlane::MAIN_HOST, "base/middleware/terminal",   \
    false, test_terminal_display)                                                            \
  X("terminal_input", "system_tests", TestPlane::MAIN_HOST, "base/middleware/terminal",     \
    true, test_terminal_input)                                                               \
  X("terminal", "system_tests", TestPlane::MAIN_HOST, "base/middleware/terminal", true,     \
    test_terminal)

#define XR_MATRIX_BINDING_ENTRY_LIST(X)                                                        \
  X("print_binding", "binding_tests", TestPlane::BINDING_HOST, "binding/core/print", false,  \
    test_print_binding)                                                                        \
  X("database_binding_sequential", "binding_tests", TestPlane::BINDING_HOST,                  \
    "binding/middleware/database", false, test_database_binding_sequential)                    \
  X("database_binding_raw", "binding_tests", TestPlane::BINDING_HOST,                          \
    "binding/middleware/database", false, test_database_binding_raw)

#define XR_MATRIX_VERIFY_LINUX_SHM_ENTRY_LIST(X)                                                \
  X("linux_shm_topic", "verify_linux_shm", TestPlane::VERIFY_ENVIRONMENT,                      \
    "verify/environment/linux_shm", false, test_linux_shm_topic)

#define XR_MATRIX_BENCH_LINUX_SHARED_TOPIC_ENTRY_LIST(X)                                        \
  X("shared_standard", "bench_linux_shared_topic", TestPlane::MEASURE_PERF,                    \
    "measure/perf", false, "standard", LinuxSharedTopicBench::RunStandardBenchmarks)           \
  X("shared_latency", "bench_linux_shared_topic", TestPlane::MEASURE_PERF,                     \
    "measure/perf", false, "latency", LinuxSharedTopicBench::RunLatencyBenchmarks)             \
  X("shared_overload", "bench_linux_shared_topic", TestPlane::MEASURE_PERF,                    \
    "measure/perf", false, "overload", LinuxSharedTopicBench::RunOverloadBenchmarks)           \
  X("shared_modes", "bench_linux_shared_topic", TestPlane::MEASURE_PERF,                       \
    "measure/perf", false, "modes", LinuxSharedTopicBench::RunModeBenchmarks)

inline int RunMainTestBinary()
{
  XR_LOG_INFO("Running LibXR Tests...\n");

  #define XR_BUILD_MAIN_ENTRY(id, group, plane, module, isolated, fn) \
    {id, group, plane, module, isolated, nullptr, &RunVoidEntry<fn>},
  const TestEntryMeta entries[] = {XR_MATRIX_MAIN_ENTRY_LIST(XR_BUILD_MAIN_ENTRY)};
  #undef XR_BUILD_MAIN_ENTRY

  const char* current_group = nullptr;
  for (const auto& entry : entries)
  {
    if (current_group == nullptr || std::strcmp(current_group, entry.group) != 0)
    {
      current_group = entry.group;
      XR_LOG_INFO("Test Group [%s]\n", current_group);
    }

    run_test_case(TestCase{entry.id, entry.run, entry.isolated});
    TEST_STEP(entry.id);
  }

  XR_LOG_INFO("All tests completed.\n");
  std::fprintf(stderr, "All tests completed.\n");
  std::fflush(stderr);
  return 0;
}

inline int RunBindingTestBinary()
{
  #define XR_BUILD_BINDING_ENTRY(id, group, plane, module, isolated, fn) \
    {id, group, plane, module, isolated, nullptr, &RunVoidEntry<fn>},
  const TestEntryMeta entries[] = {XR_MATRIX_BINDING_ENTRY_LIST(XR_BUILD_BINDING_ENTRY)};
  #undef XR_BUILD_BINDING_ENTRY

  int status = 0;
  for (const auto& entry : entries)
  {
    status |= entry.run();
  }
  return status;
}

inline int RunVerifyTestBinary(TestBinary binary)
{
  (void)binary;
  #define XR_BUILD_VERIFY_ENTRY(id, group, plane, module, isolated, fn) \
    {id, group, plane, module, isolated, nullptr, &RunVoidEntry<fn>},
  const TestEntryMeta entries[] = {XR_MATRIX_VERIFY_LINUX_SHM_ENTRY_LIST(XR_BUILD_VERIFY_ENTRY)};
  #undef XR_BUILD_VERIFY_ENTRY

  int status = 0;
  for (const auto& entry : entries)
  {
    status |= entry.run();
  }
  return status;
}

inline int RunBenchTestBinary(const char* selector)
{
  #define XR_BUILD_BENCH_ENTRY(id, group, plane, module, isolated, selector_name, fn) \
    {id, group, plane, module, isolated, selector_name, &fn},
  const TestEntryMeta entries[] = {
      XR_MATRIX_BENCH_LINUX_SHARED_TOPIC_ENTRY_LIST(XR_BUILD_BENCH_ENTRY)};
  #undef XR_BUILD_BENCH_ENTRY

  int status = 0;
  for (const auto& entry : entries)
  {
    if (selector != nullptr && std::strcmp(selector, entry.selector) != 0)
    {
      continue;
    }
    status |= entry.run();
  }
  return status;
}
