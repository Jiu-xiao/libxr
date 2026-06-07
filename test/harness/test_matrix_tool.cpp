/**
 * @file test_matrix_tool.cpp
 * @brief test 统一入口矩阵校验/导出工具。 Validation/export tool for the unified test entry matrix.
 * @details 职责：
 *          1. 校验矩阵的 id/group/selector/isolated 规则。
 *          2. 导出矩阵清单，作为 runner 外部的可读事实表。
 *          Responsibilities:
 *          1. Validate id/group/selector/isolated rules of the matrix.
 *          2. Export the matrix manifest as a human-readable fact table outside the runners.
 */
#include <cstdio>
#include <cstring>
#include <set>
#include <string_view>
#include <vector>

#include "test_matrix.hpp"

namespace
{

struct MatrixRecord
{
  const char* id;
  const char* group;
  TestBinary binary;
  TestPlane plane;
  const char* module;
  bool isolated;
  const char* selector;
};

#define XR_TOOL_MAIN_RECORD(id, group, plane, module, isolated, fn) \
  {id, group, TestBinary::MAIN, plane, module, isolated, nullptr},
#define XR_TOOL_BINDING_RECORD(id, group, plane, module, isolated, fn) \
  {id, group, TestBinary::BINDING, plane, module, isolated, nullptr},
#define XR_TOOL_VERIFY_RECORD(id, group, plane, module, isolated, fn) \
  {id, group, TestBinary::VERIFY_LINUX_SHM, plane, module, isolated, nullptr},
#define XR_TOOL_BENCH_RECORD(id, group, plane, module, isolated, selector_name, fn) \
  {id, group, TestBinary::BENCH_LINUX_SHARED_TOPIC, plane, module, isolated, selector_name},

const MatrixRecord kMatrixRecords[] = {
    XR_MATRIX_MAIN_ENTRY_LIST(XR_TOOL_MAIN_RECORD)
    XR_MATRIX_BINDING_ENTRY_LIST(XR_TOOL_BINDING_RECORD)
    XR_MATRIX_VERIFY_LINUX_SHM_ENTRY_LIST(XR_TOOL_VERIFY_RECORD)
    XR_MATRIX_BENCH_LINUX_SHARED_TOPIC_ENTRY_LIST(XR_TOOL_BENCH_RECORD)
};

#undef XR_TOOL_MAIN_RECORD
#undef XR_TOOL_BINDING_RECORD
#undef XR_TOOL_VERIFY_RECORD
#undef XR_TOOL_BENCH_RECORD

const char* BinaryName(TestBinary binary)
{
  switch (binary)
  {
    case TestBinary::MAIN:
      return "test";
    case TestBinary::BINDING:
      return "test_binding";
    case TestBinary::VERIFY_LINUX_SHM:
      return "test_linux_shm_topic";
    case TestBinary::BENCH_LINUX_SHARED_TOPIC:
      return "bench_linux_shared_topic";
  }
  return "unknown";
}

const char* PlaneName(TestPlane plane)
{
  switch (plane)
  {
    case TestPlane::MAIN_HOST:
      return "main_host";
    case TestPlane::BINDING_HOST:
      return "binding_host";
    case TestPlane::VERIFY_ENVIRONMENT:
      return "verify_environment";
    case TestPlane::MEASURE_PERF:
      return "measure_perf";
  }
  return "unknown";
}

bool HasDuplicateIds()
{
  std::set<std::string_view> ids;
  for (const auto& record : kMatrixRecords)
  {
    if (!ids.insert(record.id).second)
    {
      std::fprintf(stderr, "duplicate id: %s\n", record.id);
      return true;
    }
  }
  return false;
}

bool HasEmptyFields()
{
  for (const auto& record : kMatrixRecords)
  {
    if (record.id == nullptr || record.id[0] == '\0')
    {
      std::fprintf(stderr, "empty id on binary=%s\n", BinaryName(record.binary));
      return true;
    }
    if (record.group == nullptr || record.group[0] == '\0')
    {
      std::fprintf(stderr, "empty group on id=%s\n", record.id);
      return true;
    }
    if (record.module == nullptr || record.module[0] == '\0')
    {
      std::fprintf(stderr, "empty module on id=%s\n", record.id);
      return true;
    }
  }
  return false;
}

bool HasInvalidSelectorPolicy()
{
  std::set<std::string_view> selectors;
  for (const auto& record : kMatrixRecords)
  {
    const bool selector_present = record.selector != nullptr && record.selector[0] != '\0';
    if (record.binary == TestBinary::BENCH_LINUX_SHARED_TOPIC)
    {
      if (!selector_present)
      {
        std::fprintf(stderr, "bench entry missing selector: %s\n", record.id);
        return true;
      }
      if (!selectors.insert(record.selector).second)
      {
        std::fprintf(stderr, "duplicate selector: %s\n", record.selector);
        return true;
      }
    }
    else if (selector_present)
    {
      std::fprintf(stderr, "non-bench entry has selector: %s\n", record.id);
      return true;
    }
  }
  return false;
}

bool HasInvalidIsolationPolicy()
{
  for (const auto& record : kMatrixRecords)
  {
    if (record.isolated && record.binary != TestBinary::MAIN)
    {
      std::fprintf(stderr, "isolated entry outside main binary: %s\n", record.id);
      return true;
    }
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
  return failed ? 1 : 0;
}

int PrintTsv()
{
  std::printf("id\tbinary\tgroup\tplane\tmodule\tisolated\tselector\n");
  for (const auto& record : kMatrixRecords)
  {
    std::printf("%s\t%s\t%s\t%s\t%s\t%s\t%s\n", record.id, BinaryName(record.binary),
                record.group, PlaneName(record.plane), record.module,
                record.isolated ? "true" : "false",
                (record.selector != nullptr) ? record.selector : "");
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv)
{
  if (argc != 2)
  {
    std::fprintf(stderr, "usage: %s --validate|--tsv\n", argv[0]);
    return 2;
  }

  if (std::strcmp(argv[1], "--validate") == 0)
  {
    return ValidateMatrix();
  }
  if (std::strcmp(argv[1], "--tsv") == 0)
  {
    return PrintTsv();
  }

  std::fprintf(stderr, "unknown option: %s\n", argv[1]);
  return 2;
}
