/**
 * @file test_binding_registry.hpp
 * @brief `test_binding` 二进制入口 resolver。 Entry resolver for the `test_binding` binary.
 * @details 作用：
 *          1. 把 binding manifest entry 显式绑定到 binding 入口函数。
 *          2. 执行 binding 二进制的过滤、列出和顺序调度。
 *          Purpose:
 *          1. Explicitly bind binding manifest entries to binding entry functions.
 *          2. Execute filtering, listing, and sequential dispatch for the binding binary.
 */
#pragma once

#include "test_binding.hpp"
#include "test_main_registry.hpp"

inline TestRunFunction ResolveBindingEntry(TestEntryId entry_id)
{
  switch (entry_id)
  {
    case TestEntryId::PRINT_BINDING_CASE:
      return &RunVoidEntry<test_print_binding>;
    case TestEntryId::DATABASE_BINDING_SEQUENTIAL_CASE:
      return &RunVoidEntry<test_database_binding_sequential>;
    case TestEntryId::DATABASE_BINDING_RAW_CASE:
      return &RunVoidEntry<test_database_binding_raw>;
    default:
      return nullptr;
  }
}

inline TestRunFunction CheckedResolveBindingEntry(TestEntryId entry_id)
{
  TestRunFunction fn = ResolveBindingEntry(entry_id);
  ASSERT(fn != nullptr);
  return fn;
}

inline int RunBindingTestBinary()
{
  TestSelection selection;
  if (!LoadTestSelectionFromEnv(selection))
  {
    return 2;
  }

  size_t matched = 0;
  int status = 0;
  for (const auto& entry : kTestManifest)
  {
    if (entry.binary != TestBinary::BINDING || !EntryMatchesSelection(entry, selection))
    {
      continue;
    }

    ++matched;
    status |= CheckedResolveBindingEntry(entry.entry_id)();
  }

  if (matched == 0)
  {
    return ReportNoMatchingEntries(TestBinary::BINDING, selection);
  }
  return status;
}
