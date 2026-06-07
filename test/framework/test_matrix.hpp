/**
 * @file test_matrix.hpp
 * @brief test 二进制统一入口 manifest 与过滤 helper。 Unified manifest and filter helpers for test binaries.
 * @details 作用：
 *          1. 以单一事实源列出 `test` / `test_binding` / `verify` / `measure` 四类入口元信息。
 *          2. 提供 runner 与工具共用的 binary/plane/tag/filter helper。
 *          3. 保持 manifest 纯元信息，不直接绑定跨二进制函数符号。
 *          Purpose:
 *          1. List unified entry metadata for the `test` / `test_binding` / `verify` / `measure` binaries from one source of truth.
 *          2. Provide shared binary/plane/tag/filter helpers used by both runners and tools.
 *          3. Keep the manifest metadata-only instead of directly binding cross-binary function symbols.
 */
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

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

enum class TestTag : uint32_t
{
  NONE = 0,
  CORE = 1u << 0,
  BINDING = 1u << 1,
  VERIFY = 1u << 2,
  MEASURE = 1u << 3,
  ISOLATED = 1u << 4,
  CROSS_PROCESS = 1u << 5,
  SLOW = 1u << 6,
};

constexpr uint32_t ToMask(TestTag tag)
{
  return static_cast<uint32_t>(tag);
}

enum class TestEntryId
{
  ASSERT_CASE,
  DEF_CASE,
  CALLBACK_CASE,
  PIPE_CASE,
  RW_CASE,
  MEMORY_CASE,
  COLOR_CASE,
  TIME_CASE,
  SEMAPHORE_CASE,
  MUTEX_CASE,
  ASYNC_CASE,
  CRC_CASE,
  ENCODER_CASE,
  CYCLE_VALUE_CASE,
  PRINT_CASE,
  FLAG_CASE,
  RBT_CASE,
  QUEUE_CASE,
  LOCKFREE_QUEUE_CASE,
  POOL_CASE,
  LOCKFREE_LIST_CASE,
  STACK_CASE,
  LIST_CASE,
  DOUBLE_BUFFER_CASE,
  TYPE_CASE,
  STRING_CASE,
  THREAD_CASE,
  TIMEBASE_CASE,
  TIMER_CASE,
  RW_RUNTIME_CASE,
  PIPE_RUNTIME_CASE,
  MESSAGE_RUNTIME_CASE,
  INERTIA_CASE,
  KINEMATIC_CASE,
  TRANSFORM_CASE,
  PID_CASE,
  RAMFS_CASE,
  APP_FRAMEWORK_APPLICATION_CASE,
  APP_FRAMEWORK_HARDWARE_CASE,
  EVENT_CASE,
  MESSAGE_TOPIC_CASE,
  MESSAGE_PACKET_CASE,
  DATABASE_CASE,
  LOGGER_CASE,
  TERMINAL_COMMAND_CASE,
  TERMINAL_DISPLAY_CASE,
  TERMINAL_INPUT_CASE,
  TERMINAL_CASE,
  PRINT_BINDING_CASE,
  DATABASE_BINDING_SEQUENTIAL_CASE,
  DATABASE_BINDING_RAW_CASE,
  LINUX_SHM_TOPIC_CASE,
  SHARED_STANDARD_BENCH,
  SHARED_LATENCY_BENCH,
  SHARED_OVERLOAD_BENCH,
  SHARED_MODES_BENCH,
};

struct TestManifestEntry
{
  TestEntryId entry_id;
  const char* id;
  TestBinary binary;
  const char* group;
  TestPlane plane;
  const char* module;
  uint32_t tags;
  bool isolated;
  const char* selector;
};

inline constexpr TestManifestEntry kTestManifest[] = {
    {TestEntryId::ASSERT_CASE, "assert", TestBinary::MAIN, "core_tests",
     TestPlane::MAIN_HOST, "base/core", ToMask(TestTag::CORE), false, nullptr},
    {TestEntryId::DEF_CASE, "def", TestBinary::MAIN, "core_tests",
     TestPlane::MAIN_HOST, "base/core", ToMask(TestTag::CORE), false, nullptr},
    {TestEntryId::CALLBACK_CASE, "callback", TestBinary::MAIN, "core_tests",
     TestPlane::MAIN_HOST, "base/core", ToMask(TestTag::CORE), false, nullptr},
    {TestEntryId::PIPE_CASE, "pipe", TestBinary::MAIN, "core_tests",
     TestPlane::MAIN_HOST, "base/core/rw", ToMask(TestTag::CORE), false, nullptr},
    {TestEntryId::RW_CASE, "rw", TestBinary::MAIN, "core_tests",
     TestPlane::MAIN_HOST, "base/core/rw", ToMask(TestTag::CORE), false, nullptr},
    {TestEntryId::MEMORY_CASE, "memory", TestBinary::MAIN, "core_tests",
     TestPlane::MAIN_HOST, "base/core", ToMask(TestTag::CORE), false, nullptr},
    {TestEntryId::COLOR_CASE, "color", TestBinary::MAIN, "core_tests",
     TestPlane::MAIN_HOST, "base/core", ToMask(TestTag::CORE), false, nullptr},
    {TestEntryId::TIME_CASE, "time", TestBinary::MAIN, "core_tests",
     TestPlane::MAIN_HOST, "base/core", ToMask(TestTag::CORE), false, nullptr},

    {TestEntryId::SEMAPHORE_CASE, "semaphore", TestBinary::MAIN,
     "synchronization_tests", TestPlane::MAIN_HOST, "runtime/system",
     ToMask(TestTag::CORE), false, nullptr},
    {TestEntryId::MUTEX_CASE, "mutex", TestBinary::MAIN, "synchronization_tests",
     TestPlane::MAIN_HOST, "runtime/system", ToMask(TestTag::CORE), false, nullptr},
    {TestEntryId::ASYNC_CASE, "async", TestBinary::MAIN, "synchronization_tests",
     TestPlane::MAIN_HOST, "runtime/system", ToMask(TestTag::CORE), false, nullptr},

    {TestEntryId::CRC_CASE, "crc", TestBinary::MAIN, "utility_tests",
     TestPlane::MAIN_HOST, "base/utils", ToMask(TestTag::CORE), false, nullptr},
    {TestEntryId::ENCODER_CASE, "encoder", TestBinary::MAIN, "utility_tests",
     TestPlane::MAIN_HOST, "base/utils", ToMask(TestTag::CORE), false, nullptr},
    {TestEntryId::CYCLE_VALUE_CASE, "cycle_value", TestBinary::MAIN, "utility_tests",
     TestPlane::MAIN_HOST, "base/utils", ToMask(TestTag::CORE), false, nullptr},
    {TestEntryId::PRINT_CASE, "print", TestBinary::MAIN, "utility_tests",
     TestPlane::MAIN_HOST, "base/core/print", ToMask(TestTag::CORE), false, nullptr},
    {TestEntryId::FLAG_CASE, "flag", TestBinary::MAIN, "utility_tests",
     TestPlane::MAIN_HOST, "base/utils", ToMask(TestTag::CORE), false, nullptr},

    {TestEntryId::RBT_CASE, "rbt", TestBinary::MAIN, "data_structure_tests",
     TestPlane::MAIN_HOST, "base/structure", ToMask(TestTag::CORE), false, nullptr},
    {TestEntryId::QUEUE_CASE, "queue", TestBinary::MAIN, "data_structure_tests",
     TestPlane::MAIN_HOST, "base/structure", ToMask(TestTag::CORE), false, nullptr},
    {TestEntryId::LOCKFREE_QUEUE_CASE, "lockfree_queue", TestBinary::MAIN,
     "data_structure_tests", TestPlane::MAIN_HOST, "base/structure",
     ToMask(TestTag::CORE), false, nullptr},
    {TestEntryId::POOL_CASE, "pool", TestBinary::MAIN, "data_structure_tests",
     TestPlane::MAIN_HOST, "base/structure", ToMask(TestTag::CORE), false, nullptr},
    {TestEntryId::LOCKFREE_LIST_CASE, "lockfree_list", TestBinary::MAIN,
     "data_structure_tests", TestPlane::MAIN_HOST, "base/structure",
     ToMask(TestTag::CORE), false, nullptr},
    {TestEntryId::STACK_CASE, "stack", TestBinary::MAIN, "data_structure_tests",
     TestPlane::MAIN_HOST, "base/structure", ToMask(TestTag::CORE), false, nullptr},
    {TestEntryId::LIST_CASE, "list", TestBinary::MAIN, "data_structure_tests",
     TestPlane::MAIN_HOST, "base/structure", ToMask(TestTag::CORE), false, nullptr},
    {TestEntryId::DOUBLE_BUFFER_CASE, "double_buffer", TestBinary::MAIN,
     "data_structure_tests", TestPlane::MAIN_HOST, "base/structure",
     ToMask(TestTag::CORE), false, nullptr},
    {TestEntryId::TYPE_CASE, "type", TestBinary::MAIN, "data_structure_tests",
     TestPlane::MAIN_HOST, "base/core", ToMask(TestTag::CORE), false, nullptr},
    {TestEntryId::STRING_CASE, "string", TestBinary::MAIN, "data_structure_tests",
     TestPlane::MAIN_HOST, "base/core", ToMask(TestTag::CORE), false, nullptr},

    {TestEntryId::THREAD_CASE, "thread", TestBinary::MAIN, "threading_tests",
     TestPlane::MAIN_HOST, "runtime/system", ToMask(TestTag::CORE), false, nullptr},
    {TestEntryId::TIMEBASE_CASE, "timebase", TestBinary::MAIN, "threading_tests",
     TestPlane::MAIN_HOST, "runtime/system", ToMask(TestTag::CORE), false, nullptr},
    {TestEntryId::TIMER_CASE, "timer", TestBinary::MAIN, "threading_tests",
     TestPlane::MAIN_HOST, "runtime/system", ToMask(TestTag::CORE), false, nullptr},

    {TestEntryId::RW_RUNTIME_CASE, "rw_runtime", TestBinary::MAIN, "runtime_tests",
     TestPlane::MAIN_HOST, "runtime/core/rw", ToMask(TestTag::CORE), false, nullptr},
    {TestEntryId::PIPE_RUNTIME_CASE, "pipe_runtime", TestBinary::MAIN, "runtime_tests",
     TestPlane::MAIN_HOST, "runtime/core/rw", ToMask(TestTag::CORE), false, nullptr},
    {TestEntryId::MESSAGE_RUNTIME_CASE, "message_runtime", TestBinary::MAIN,
     "runtime_tests", TestPlane::MAIN_HOST, "runtime/middleware/message",
     ToMask(TestTag::CORE), false, nullptr},

    {TestEntryId::INERTIA_CASE, "inertia", TestBinary::MAIN, "motion_tests",
     TestPlane::MAIN_HOST, "base/utils", ToMask(TestTag::CORE), false, nullptr},
    {TestEntryId::KINEMATIC_CASE, "kinematic", TestBinary::MAIN, "motion_tests",
     TestPlane::MAIN_HOST, "base/utils", ToMask(TestTag::CORE), false, nullptr},
    {TestEntryId::TRANSFORM_CASE, "transform", TestBinary::MAIN, "motion_tests",
     TestPlane::MAIN_HOST, "base/utils", ToMask(TestTag::CORE), false, nullptr},

    {TestEntryId::PID_CASE, "pid", TestBinary::MAIN, "control_tests",
     TestPlane::MAIN_HOST, "base/utils", ToMask(TestTag::CORE), false, nullptr},

    {TestEntryId::RAMFS_CASE, "ramfs", TestBinary::MAIN, "system_tests",
     TestPlane::MAIN_HOST, "base/middleware/ramfs", ToMask(TestTag::CORE), false, nullptr},
    {TestEntryId::APP_FRAMEWORK_APPLICATION_CASE, "app_framework_application",
     TestBinary::MAIN, "system_tests", TestPlane::MAIN_HOST,
     "base/middleware/app_framework", ToMask(TestTag::CORE), false, nullptr},
    {TestEntryId::APP_FRAMEWORK_HARDWARE_CASE, "app_framework_hardware",
     TestBinary::MAIN, "system_tests", TestPlane::MAIN_HOST,
     "base/middleware/app_framework", ToMask(TestTag::CORE), false, nullptr},
    {TestEntryId::EVENT_CASE, "event", TestBinary::MAIN, "system_tests",
     TestPlane::MAIN_HOST, "base/middleware/event", ToMask(TestTag::CORE), false, nullptr},
    {TestEntryId::MESSAGE_TOPIC_CASE, "message_topic", TestBinary::MAIN,
     "system_tests", TestPlane::MAIN_HOST, "base/middleware/message",
     ToMask(TestTag::CORE), false, nullptr},
    {TestEntryId::MESSAGE_PACKET_CASE, "message_packet", TestBinary::MAIN,
     "system_tests", TestPlane::MAIN_HOST, "base/middleware/message",
     ToMask(TestTag::CORE), false, nullptr},
    {TestEntryId::DATABASE_CASE, "database", TestBinary::MAIN, "system_tests",
     TestPlane::MAIN_HOST, "base/middleware/database", ToMask(TestTag::CORE), false,
     nullptr},
    {TestEntryId::LOGGER_CASE, "logger", TestBinary::MAIN, "system_tests",
     TestPlane::MAIN_HOST, "base/middleware/logger",
     ToMask(TestTag::CORE) | ToMask(TestTag::ISOLATED), true, nullptr},
    {TestEntryId::TERMINAL_COMMAND_CASE, "terminal_command", TestBinary::MAIN,
     "system_tests", TestPlane::MAIN_HOST, "base/middleware/terminal",
     ToMask(TestTag::CORE) | ToMask(TestTag::ISOLATED), true, nullptr},
    {TestEntryId::TERMINAL_DISPLAY_CASE, "terminal_display", TestBinary::MAIN,
     "system_tests", TestPlane::MAIN_HOST, "base/middleware/terminal",
     ToMask(TestTag::CORE), false, nullptr},
    {TestEntryId::TERMINAL_INPUT_CASE, "terminal_input", TestBinary::MAIN,
     "system_tests", TestPlane::MAIN_HOST, "base/middleware/terminal",
     ToMask(TestTag::CORE) | ToMask(TestTag::ISOLATED), true, nullptr},
    {TestEntryId::TERMINAL_CASE, "terminal", TestBinary::MAIN, "system_tests",
     TestPlane::MAIN_HOST, "base/middleware/terminal",
     ToMask(TestTag::CORE) | ToMask(TestTag::ISOLATED), true, nullptr},

    {TestEntryId::PRINT_BINDING_CASE, "print_binding", TestBinary::BINDING,
     "binding_tests", TestPlane::BINDING_HOST, "binding/core/print",
     ToMask(TestTag::BINDING), false, nullptr},
    {TestEntryId::DATABASE_BINDING_SEQUENTIAL_CASE, "database_binding_sequential",
     TestBinary::BINDING, "binding_tests", TestPlane::BINDING_HOST,
     "binding/middleware/database",
     ToMask(TestTag::BINDING) | ToMask(TestTag::SLOW), false, nullptr},
    {TestEntryId::DATABASE_BINDING_RAW_CASE, "database_binding_raw", TestBinary::BINDING,
     "binding_tests", TestPlane::BINDING_HOST, "binding/middleware/database",
     ToMask(TestTag::BINDING), false, nullptr},

    {TestEntryId::LINUX_SHM_TOPIC_CASE, "linux_shm_topic", TestBinary::VERIFY_LINUX_SHM,
     "verify_linux_shm", TestPlane::VERIFY_ENVIRONMENT, "verify/environment/linux_shm",
     ToMask(TestTag::VERIFY) | ToMask(TestTag::CROSS_PROCESS) | ToMask(TestTag::SLOW),
     false, nullptr},

    {TestEntryId::SHARED_STANDARD_BENCH, "shared_standard",
     TestBinary::BENCH_LINUX_SHARED_TOPIC, "bench_linux_shared_topic",
     TestPlane::MEASURE_PERF, "measure/perf",
     ToMask(TestTag::MEASURE) | ToMask(TestTag::SLOW), false, "standard"},
    {TestEntryId::SHARED_LATENCY_BENCH, "shared_latency",
     TestBinary::BENCH_LINUX_SHARED_TOPIC, "bench_linux_shared_topic",
     TestPlane::MEASURE_PERF, "measure/perf",
     ToMask(TestTag::MEASURE) | ToMask(TestTag::SLOW), false, "latency"},
    {TestEntryId::SHARED_OVERLOAD_BENCH, "shared_overload",
     TestBinary::BENCH_LINUX_SHARED_TOPIC, "bench_linux_shared_topic",
     TestPlane::MEASURE_PERF, "measure/perf",
     ToMask(TestTag::MEASURE) | ToMask(TestTag::SLOW), false, "overload"},
    {TestEntryId::SHARED_MODES_BENCH, "shared_modes",
     TestBinary::BENCH_LINUX_SHARED_TOPIC, "bench_linux_shared_topic",
     TestPlane::MEASURE_PERF, "measure/perf",
     ToMask(TestTag::MEASURE) | ToMask(TestTag::SLOW), false, "modes"},
};

struct TestFilter
{
  const char* id = nullptr;
  const char* group = nullptr;
  const char* module = nullptr;
  const char* plane = nullptr;
  uint32_t required_tags = 0;
  bool list_only = false;
  bool has_any_filter = false;
};

inline bool IsTruthyEnvValue(const char* value)
{
  return value != nullptr &&
         (std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 ||
          std::strcmp(value, "yes") == 0);
}

inline bool TestListOnlyRequested()
{
  return IsTruthyEnvValue(std::getenv("XR_TEST_LIST"));
}

inline const char* BinaryName(TestBinary binary)
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

inline const char* PlaneName(TestPlane plane)
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

inline bool ParseTagName(std::string_view text, uint32_t& mask)
{
  if (text == "core")
  {
    mask |= ToMask(TestTag::CORE);
    return true;
  }
  if (text == "binding")
  {
    mask |= ToMask(TestTag::BINDING);
    return true;
  }
  if (text == "verify")
  {
    mask |= ToMask(TestTag::VERIFY);
    return true;
  }
  if (text == "measure")
  {
    mask |= ToMask(TestTag::MEASURE);
    return true;
  }
  if (text == "isolated")
  {
    mask |= ToMask(TestTag::ISOLATED);
    return true;
  }
  if (text == "cross_process")
  {
    mask |= ToMask(TestTag::CROSS_PROCESS);
    return true;
  }
  if (text == "slow")
  {
    mask |= ToMask(TestTag::SLOW);
    return true;
  }
  return false;
}

inline std::string TagsText(uint32_t tags)
{
  std::string text;
  auto append = [&](const char* token, uint32_t mask) {
    if ((tags & mask) == 0)
    {
      return;
    }
    if (!text.empty())
    {
      text.push_back(',');
    }
    text += token;
  };

  append("core", ToMask(TestTag::CORE));
  append("binding", ToMask(TestTag::BINDING));
  append("verify", ToMask(TestTag::VERIFY));
  append("measure", ToMask(TestTag::MEASURE));
  append("isolated", ToMask(TestTag::ISOLATED));
  append("cross_process", ToMask(TestTag::CROSS_PROCESS));
  append("slow", ToMask(TestTag::SLOW));
  return text;
}

inline bool ParseTagCsv(const char* csv, uint32_t& mask, FILE* err)
{
  mask = 0;
  if (csv == nullptr || csv[0] == '\0')
  {
    return true;
  }

  const char* begin = csv;
  const char* end = csv;
  while (true)
  {
    if (*end == ',' || *end == '\0')
    {
      const std::string_view token(begin, static_cast<size_t>(end - begin));
      if (!token.empty() && !ParseTagName(token, mask))
      {
        std::fprintf(err, "unknown tag filter: %.*s\n", static_cast<int>(token.size()),
                     token.data());
        return false;
      }
      if (*end == '\0')
      {
        break;
      }
      begin = end + 1;
    }
    ++end;
  }
  return true;
}

inline bool LoadTestFilterFromEnv(TestFilter& filter, FILE* err = stderr)
{
  filter = {};
  filter.id = std::getenv("XR_TEST_ID");
  filter.group = std::getenv("XR_TEST_GROUP");
  filter.module = std::getenv("XR_TEST_MODULE");
  filter.plane = std::getenv("XR_TEST_PLANE");
  const char* tags = std::getenv("XR_TEST_TAG");
  const char* list_only = std::getenv("XR_TEST_LIST");

  filter.has_any_filter = (filter.id != nullptr && filter.id[0] != '\0') ||
                          (filter.group != nullptr && filter.group[0] != '\0') ||
                          (filter.module != nullptr && filter.module[0] != '\0') ||
                          (filter.plane != nullptr && filter.plane[0] != '\0') ||
                          (tags != nullptr && tags[0] != '\0');
  filter.list_only = IsTruthyEnvValue(list_only);

  return ParseTagCsv(tags, filter.required_tags, err);
}

inline bool EntryMatchesFilter(const TestManifestEntry& entry, const TestFilter& filter)
{
  if (filter.id != nullptr && filter.id[0] != '\0' && std::strcmp(filter.id, entry.id) != 0)
  {
    return false;
  }
  if (filter.group != nullptr && filter.group[0] != '\0' &&
      std::strcmp(filter.group, entry.group) != 0)
  {
    return false;
  }
  if (filter.module != nullptr && filter.module[0] != '\0' &&
      std::strcmp(filter.module, entry.module) != 0)
  {
    return false;
  }
  if (filter.plane != nullptr && filter.plane[0] != '\0' &&
      std::strcmp(filter.plane, PlaneName(entry.plane)) != 0)
  {
    return false;
  }
  if (filter.required_tags != 0 && (entry.tags & filter.required_tags) != filter.required_tags)
  {
    return false;
  }
  return true;
}

inline void PrintEntryTsv(FILE* out, const TestManifestEntry& entry)
{
  const std::string tags = TagsText(entry.tags);
  std::fprintf(out, "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n", entry.id,
               BinaryName(entry.binary), entry.group, PlaneName(entry.plane), entry.module,
               tags.c_str(), entry.isolated ? "true" : "false",
               entry.selector != nullptr ? entry.selector : "");
}

inline int ReportNoMatchingEntries(TestBinary binary, const TestFilter& filter)
{
  std::fprintf(stderr,
               "no matching entries for binary=%s id=%s group=%s module=%s plane=%s tags=%s\n",
               BinaryName(binary), filter.id != nullptr ? filter.id : "",
               filter.group != nullptr ? filter.group : "",
               filter.module != nullptr ? filter.module : "",
               filter.plane != nullptr ? filter.plane : "",
               TagsText(filter.required_tags).c_str());
  return 1;
}
