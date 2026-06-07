/**
 * @file test_verify_registry.hpp
 * @brief verify 二进制入口 resolver。 Entry resolver for verification binaries.
 * @details 作用：
 *          1. 把 verify manifest entry 显式绑定到 verify 入口函数。
 *          2. 执行 verify 二进制的过滤、列出和顺序调度。
 *          Purpose:
 *          1. Explicitly bind verification manifest entries to verification entry functions.
 *          2. Execute filtering, listing, and sequential dispatch for verification binaries.
 */
#pragma once

#include "../verify/environment/linux_shm/test_verify.hpp"
#include "test_main_registry.hpp"

inline TestRunFunction ResolveVerifyLinuxShmEntry(TestEntryId entry_id)
{
  switch (entry_id)
  {
    case TestEntryId::LINUX_SHM_TOPIC_CASE:
      return &RunVoidEntry<test_linux_shm_topic>;
    default:
      return nullptr;
  }
}

inline TestRunFunction CheckedResolveVerifyLinuxShmEntry(TestEntryId entry_id)
{
  TestRunFunction fn = ResolveVerifyLinuxShmEntry(entry_id);
  ASSERT(fn != nullptr);
  return fn;
}

inline int RunVerifyTestBinary(TestBinary binary)
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
    if (entry.binary != binary || !EntryMatchesFilter(entry, filter))
    {
      continue;
    }

    ++matched;
    if (filter.list_only)
    {
      PrintEntryTsv(stdout, entry);
      continue;
    }

    switch (binary)
    {
      case TestBinary::VERIFY_LINUX_SHM:
        status |= CheckedResolveVerifyLinuxShmEntry(entry.entry_id)();
        break;
      default:
        return 1;
    }
  }

  if (matched == 0)
  {
    return ReportNoMatchingEntries(binary, filter);
  }
  return status;
}
