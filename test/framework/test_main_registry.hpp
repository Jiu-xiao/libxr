/**
 * @file test_main_registry.hpp
 * @brief `test` 主二进制入口 resolver。 Entry resolver for the main `test` binary.
 * @details 作用：
 *          1. 把主测试 manifest entry 显式绑定到 `test_base.hpp` 里的入口函数。
 *          2. 执行主测试二进制的过滤、列出和 case 调度。
 *          Purpose:
 *          1. Explicitly bind main-test manifest entries to entry functions declared in `test_base.hpp`.
 *          2. Execute filtering, listing, and case dispatch for the main test binary.
 */
#pragma once

#include "test_base.hpp"
#include "test_case_runner.hpp"
#include "test_matrix.hpp"

using TestRunFunction = int (*)();

template <void (*Fn)()>
int RunVoidEntry()
{
  Fn();
  return 0;
}

inline TestRunFunction ResolveMainEntry(TestEntryId entry_id)
{
  switch (entry_id)
  {
    case TestEntryId::ASSERT_CASE:
      return &RunVoidEntry<test_assert>;
    case TestEntryId::DEF_CASE:
      return &RunVoidEntry<test_def>;
    case TestEntryId::CALLBACK_CASE:
      return &RunVoidEntry<test_cb>;
    case TestEntryId::PIPE_CASE:
      return &RunVoidEntry<test_pipe>;
    case TestEntryId::RW_CASE:
      return &RunVoidEntry<test_rw>;
    case TestEntryId::MEMORY_CASE:
      return &RunVoidEntry<test_memory>;
    case TestEntryId::COLOR_CASE:
      return &RunVoidEntry<test_color>;
    case TestEntryId::TIME_CASE:
      return &RunVoidEntry<test_time>;
    case TestEntryId::SEMAPHORE_CASE:
      return &RunVoidEntry<test_semaphore>;
    case TestEntryId::MUTEX_CASE:
      return &RunVoidEntry<test_mutex>;
    case TestEntryId::ASYNC_CASE:
      return &RunVoidEntry<test_async>;
    case TestEntryId::CRC_CASE:
      return &RunVoidEntry<test_crc>;
    case TestEntryId::ENCODER_CASE:
      return &RunVoidEntry<test_float_encoder>;
    case TestEntryId::CYCLE_VALUE_CASE:
      return &RunVoidEntry<test_cycle_value>;
    case TestEntryId::PRINT_CASE:
      return &RunVoidEntry<test_print>;
    case TestEntryId::FLAG_CASE:
      return &RunVoidEntry<test_flag>;
    case TestEntryId::RBT_CASE:
      return &RunVoidEntry<test_rbt>;
    case TestEntryId::QUEUE_CASE:
      return &RunVoidEntry<test_queue>;
    case TestEntryId::LOCKFREE_QUEUE_CASE:
      return &RunVoidEntry<test_lockfree_queue>;
    case TestEntryId::POOL_CASE:
      return &RunVoidEntry<test_lock_free_pool>;
    case TestEntryId::LOCKFREE_LIST_CASE:
      return &RunVoidEntry<test_lockfree_list>;
    case TestEntryId::STACK_CASE:
      return &RunVoidEntry<test_stack>;
    case TestEntryId::LIST_CASE:
      return &RunVoidEntry<test_list>;
    case TestEntryId::DOUBLE_BUFFER_CASE:
      return &RunVoidEntry<test_double_buffer>;
    case TestEntryId::TYPE_CASE:
      return &RunVoidEntry<test_type>;
    case TestEntryId::STRING_CASE:
      return &RunVoidEntry<test_string>;
    case TestEntryId::THREAD_CASE:
      return &RunVoidEntry<test_thread>;
    case TestEntryId::TIMEBASE_CASE:
      return &RunVoidEntry<test_timebase>;
    case TestEntryId::TIMER_CASE:
      return &RunVoidEntry<test_timer>;
    case TestEntryId::RW_RUNTIME_CASE:
      return &RunVoidEntry<test_rw_runtime>;
    case TestEntryId::PIPE_RUNTIME_CASE:
      return &RunVoidEntry<test_pipe_runtime>;
    case TestEntryId::MESSAGE_RUNTIME_CASE:
      return &RunVoidEntry<test_message_runtime>;
    case TestEntryId::INERTIA_CASE:
      return &RunVoidEntry<test_inertia>;
    case TestEntryId::KINEMATIC_CASE:
      return &RunVoidEntry<test_kinematic>;
    case TestEntryId::TRANSFORM_CASE:
      return &RunVoidEntry<test_transform>;
    case TestEntryId::PID_CASE:
      return &RunVoidEntry<test_pid>;
    case TestEntryId::RAMFS_CASE:
      return &RunVoidEntry<test_ramfs>;
    case TestEntryId::APP_FRAMEWORK_APPLICATION_CASE:
      return &RunVoidEntry<test_app_framework_application>;
    case TestEntryId::APP_FRAMEWORK_HARDWARE_CASE:
      return &RunVoidEntry<test_app_framework_hardware>;
    case TestEntryId::EVENT_CASE:
      return &RunVoidEntry<test_event>;
    case TestEntryId::MESSAGE_TOPIC_CASE:
      return &RunVoidEntry<test_message_topic>;
    case TestEntryId::MESSAGE_PACKET_CASE:
      return &RunVoidEntry<test_message_packet>;
    case TestEntryId::DATABASE_CASE:
      return &RunVoidEntry<test_database>;
    case TestEntryId::LOGGER_CASE:
      return &RunVoidEntry<test_logger>;
    case TestEntryId::TERMINAL_COMMAND_CASE:
      return &RunVoidEntry<test_terminal_command>;
    case TestEntryId::TERMINAL_DISPLAY_CASE:
      return &RunVoidEntry<test_terminal_display>;
    case TestEntryId::TERMINAL_INPUT_CASE:
      return &RunVoidEntry<test_terminal_input>;
    case TestEntryId::TERMINAL_CASE:
      return &RunVoidEntry<test_terminal>;
    default:
      return nullptr;
  }
}

inline TestRunFunction CheckedResolveMainEntry(TestEntryId entry_id)
{
  TestRunFunction fn = ResolveMainEntry(entry_id);
  ASSERT(fn != nullptr);
  return fn;
}

inline int RunMainTestBinary()
{
  TestFilter filter;
  if (!LoadTestFilterFromEnv(filter))
  {
    return 2;
  }

  if (!filter.list_only)
  {
    XR_LOG_INFO("Running LibXR Tests...\n");
  }

  if (filter.list_only)
  {
    std::printf("id\tbinary\tgroup\tplane\tmodule\ttags\tisolated\tselector\n");
  }

  const char* current_group = nullptr;
  size_t matched = 0;
  for (const auto& entry : kTestManifest)
  {
    if (entry.binary != TestBinary::MAIN || !EntryMatchesFilter(entry, filter))
    {
      continue;
    }

    ++matched;
    if (filter.list_only)
    {
      PrintEntryTsv(stdout, entry);
      continue;
    }

    if (current_group == nullptr || std::strcmp(current_group, entry.group) != 0)
    {
      current_group = entry.group;
      XR_LOG_INFO("Test Group [%s]\n", current_group);
    }

    run_test_case(TestCase{entry.id, CheckedResolveMainEntry(entry.entry_id), entry.isolated});
    TEST_STEP(entry.id);
  }

  if (matched == 0)
  {
    return ReportNoMatchingEntries(TestBinary::MAIN, filter);
  }

  if (!filter.list_only)
  {
    XR_LOG_INFO("All tests completed.\n");
    std::fprintf(stderr, "All tests completed.\n");
    std::fflush(stderr);
  }
  return 0;
}
