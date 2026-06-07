/**
 * @file test_bench_registry.hpp
 * @brief benchmark 二进制入口 resolver。 Entry resolver for benchmark binaries.
 * @details 作用：
 *          1. 把 benchmark manifest entry 显式绑定到 benchmark 入口函数。
 *          2. 执行 benchmark 二进制的 selector/filter/list 调度。
 *          Purpose:
 *          1. Explicitly bind benchmark manifest entries to benchmark entry functions.
 *          2. Execute selector/filter/list dispatch for benchmark binaries.
 */
#pragma once

#include "../measure/perf/linux_shared_topic_bench_common.hpp"
#include "test_main_registry.hpp"

inline TestRunFunction ResolveBenchLinuxSharedTopicEntry(TestEntryId entry_id)
{
  switch (entry_id)
  {
    case TestEntryId::SHARED_STANDARD_BENCH:
      return &LinuxSharedTopicBench::RunStandardBenchmarks;
    case TestEntryId::SHARED_LATENCY_BENCH:
      return &LinuxSharedTopicBench::RunLatencyBenchmarks;
    case TestEntryId::SHARED_OVERLOAD_BENCH:
      return &LinuxSharedTopicBench::RunOverloadBenchmarks;
    case TestEntryId::SHARED_MODES_BENCH:
      return &LinuxSharedTopicBench::RunModeBenchmarks;
    default:
      return nullptr;
  }
}

inline TestRunFunction CheckedResolveBenchLinuxSharedTopicEntry(TestEntryId entry_id)
{
  TestRunFunction fn = ResolveBenchLinuxSharedTopicEntry(entry_id);
  ASSERT(fn != nullptr);
  return fn;
}

inline int RunBenchTestBinary(const char* selector)
{
  TestFilter filter;
  if (!LoadTestFilterFromEnv(filter))
  {
    return 2;
  }

  if (filter.list_only)
  {
    std::printf("id\tbinary\tgroup\tplane\tmodule\ttags\tisolated\tselector\n");
  }

  size_t matched = 0;
  int status = 0;
  for (const auto& entry : kTestManifest)
  {
    if (entry.binary != TestBinary::BENCH_LINUX_SHARED_TOPIC)
    {
      continue;
    }
    if (selector != nullptr && std::strcmp(selector, entry.selector) != 0)
    {
      continue;
    }
    if (!EntryMatchesFilter(entry, filter))
    {
      continue;
    }

    ++matched;
    if (filter.list_only)
    {
      PrintEntryTsv(stdout, entry);
      continue;
    }

    status |= CheckedResolveBenchLinuxSharedTopicEntry(entry.entry_id)();
  }

  if (matched == 0)
  {
    return ReportNoMatchingEntries(TestBinary::BENCH_LINUX_SHARED_TOPIC, filter);
  }
  return status;
}
