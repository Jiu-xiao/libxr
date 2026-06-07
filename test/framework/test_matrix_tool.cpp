/**
 * @file test_matrix_tool.cpp
 * @brief test 统一入口矩阵校验/导出工具。 Validation/export tool for the unified test manifest.
 * @details 职责：
 *          1. 校验 manifest 的 id/group/module/tag/selector/isolated 规则。
 *          2. 导出矩阵清单，作为 runner 外部的可读事实表。
 *          Responsibilities:
 *          1. Validate id/group/module/tag/selector/isolated rules of the manifest.
 *          2. Export the manifest as a human-readable fact table outside the runners.
 */
#include <cstdio>
#include <cstring>
#include <set>
#include <string_view>

#include "test_matrix.hpp"

namespace
{

bool HasDuplicateIds()
{
  std::set<std::string_view> ids;
  for (const auto& entry : kTestManifest)
  {
    if (!ids.insert(entry.id).second)
    {
      std::fprintf(stderr, "duplicate id: %s\n", entry.id);
      return true;
    }
  }
  return false;
}

bool HasEmptyFields()
{
  for (const auto& entry : kTestManifest)
  {
    if (entry.id == nullptr || entry.id[0] == '\0')
    {
      std::fprintf(stderr, "empty id on binary=%s\n", BinaryName(entry.binary));
      return true;
    }
    if (entry.group == nullptr || entry.group[0] == '\0')
    {
      std::fprintf(stderr, "empty group on id=%s\n", entry.id);
      return true;
    }
    if (entry.module == nullptr || entry.module[0] == '\0')
    {
      std::fprintf(stderr, "empty module on id=%s\n", entry.id);
      return true;
    }
  }
  return false;
}

bool HasInvalidSelectorPolicy()
{
  std::set<std::string_view> selectors;
  for (const auto& entry : kTestManifest)
  {
    const bool selector_present = entry.selector != nullptr && entry.selector[0] != '\0';
    if (entry.binary == TestBinary::BENCH_LINUX_SHARED_TOPIC)
    {
      if (!selector_present)
      {
        std::fprintf(stderr, "bench entry missing selector: %s\n", entry.id);
        return true;
      }
      if (!selectors.insert(entry.selector).second)
      {
        std::fprintf(stderr, "duplicate selector: %s\n", entry.selector);
        return true;
      }
    }
    else if (selector_present)
    {
      std::fprintf(stderr, "non-bench entry has selector: %s\n", entry.id);
      return true;
    }
  }
  return false;
}

bool HasInvalidIsolationPolicy()
{
  for (const auto& entry : kTestManifest)
  {
    const bool isolated_tag = (entry.tags & ToMask(TestTag::ISOLATED)) != 0;
    if (entry.isolated && entry.binary != TestBinary::MAIN)
    {
      std::fprintf(stderr, "isolated entry outside main binary: %s\n", entry.id);
      return true;
    }
    if (entry.isolated != isolated_tag)
    {
      std::fprintf(stderr, "isolated flag/tag mismatch on entry: %s\n", entry.id);
      return true;
    }
  }
  return false;
}

bool HasInvalidPlaneTagPolicy()
{
  for (const auto& entry : kTestManifest)
  {
    switch (entry.binary)
    {
      case TestBinary::MAIN:
        if ((entry.tags & ToMask(TestTag::CORE)) == 0)
        {
          std::fprintf(stderr, "main entry missing core tag: %s\n", entry.id);
          return true;
        }
        break;
      case TestBinary::BINDING:
        if ((entry.tags & ToMask(TestTag::BINDING)) == 0)
        {
          std::fprintf(stderr, "binding entry missing binding tag: %s\n", entry.id);
          return true;
        }
        break;
      case TestBinary::VERIFY_LINUX_SHM:
        if ((entry.tags & ToMask(TestTag::VERIFY)) == 0)
        {
          std::fprintf(stderr, "verify entry missing verify tag: %s\n", entry.id);
          return true;
        }
        break;
      case TestBinary::BENCH_LINUX_SHARED_TOPIC:
        if ((entry.tags & ToMask(TestTag::MEASURE)) == 0)
        {
          std::fprintf(stderr, "bench entry missing measure tag: %s\n", entry.id);
          return true;
        }
        break;
    }
  }
  return false;
}

bool HasEmptyBinary()
{
  bool seen_main = false;
  bool seen_binding = false;
  bool seen_verify = false;
  bool seen_bench = false;
  for (const auto& entry : kTestManifest)
  {
    switch (entry.binary)
    {
      case TestBinary::MAIN:
        seen_main = true;
        break;
      case TestBinary::BINDING:
        seen_binding = true;
        break;
      case TestBinary::VERIFY_LINUX_SHM:
        seen_verify = true;
        break;
      case TestBinary::BENCH_LINUX_SHARED_TOPIC:
        seen_bench = true;
        break;
    }
  }

  if (!seen_main || !seen_binding || !seen_verify || !seen_bench)
  {
    std::fprintf(stderr, "one or more binaries have no entries\n");
    return true;
  }
  return false;
}

int ValidateMatrix()
{
  bool failed = false;
  failed |= HasDuplicateIds();
  failed |= HasEmptyFields();
  failed |= HasInvalidSelectorPolicy();
  failed |= HasInvalidIsolationPolicy();
  failed |= HasInvalidPlaneTagPolicy();
  failed |= HasEmptyBinary();
  return failed ? 1 : 0;
}

int PrintTsv(FILE* out)
{
  std::fprintf(out, "id\tbinary\tgroup\tplane\tmodule\ttags\tisolated\tselector\n");
  for (const auto& entry : kTestManifest)
  {
    std::fprintf(out, "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n", entry.id,
                 BinaryName(entry.binary), entry.group, PlaneName(entry.plane), entry.module,
                 TagsText(entry.tags).c_str(), entry.isolated ? "true" : "false",
                 (entry.selector != nullptr) ? entry.selector : "");
  }
  return 0;
}

int WriteTsv(const char* path)
{
  std::FILE* file = std::fopen(path, "wb");
  if (file == nullptr)
  {
    std::fprintf(stderr, "failed to open output: %s\n", path);
    return 1;
  }
  const int rc = PrintTsv(file);
  std::fclose(file);
  return rc;
}

}  // namespace

int main(int argc, char** argv)
{
  if (argc == 2 && std::strcmp(argv[1], "--validate") == 0)
  {
    return ValidateMatrix();
  }
  if (argc == 2 && std::strcmp(argv[1], "--tsv") == 0)
  {
    return PrintTsv(stdout);
  }
  if (argc == 3 && std::strcmp(argv[1], "--write-tsv") == 0)
  {
    return WriteTsv(argv[2]);
  }

  std::fprintf(stderr, "usage: %s --validate | --tsv | --write-tsv <path>\n", argv[0]);
  return 2;
}
