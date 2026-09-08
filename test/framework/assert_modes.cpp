#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <cstring>

#include "libxr_def.hpp"
#include "test_assert.hpp"

namespace
{
int fatal_count = 0;
bool fatal_in_isr = false;
const char* fatal_file = nullptr;
uint32_t fatal_line = 0;
}  // namespace

// 仅记录宏的分发，不调用平台致命错误处理 / Record dispatch without platform shutdown.
extern "C" void libxr_fatal_error(const char* file, uint32_t line, bool in_isr)
{
  ++fatal_count;
  fatal_file = file;
  fatal_line = line;
  fatal_in_isr = in_isr;
}

int main()
{
  int evaluations = 0;
  ASSERT(++evaluations == 1);
  TEST_ASSERT(evaluations == 1);
  ASSERT(false);
#ifdef LIBXR_DEBUG_BUILD
  TEST_ASSERT(fatal_count == 1);
#else
  TEST_ASSERT(fatal_count == 0);
#endif

  fatal_count = 0;
  evaluations = 0;
  DEV_ASSERT(++evaluations == 1);
  DEV_ASSERT(false);
#ifdef LIBXR_DEV_ASSERT_BUILD
  TEST_ASSERT(evaluations == 1 && fatal_count == 1);
#else
  TEST_ASSERT(evaluations == 0 && fatal_count == 0);
  DEV_ASSERT(no_such_identifier);
  DEV_ASSERT_FROM_CALLBACK(no_such_condition, no_such_context);
#endif

  fatal_count = 0;
  evaluations = 0;
  REQUIRE(++evaluations == 1);
  const uint32_t line = __LINE__ + 1;
  REQUIRE_FROM_CALLBACK(false, true);
  TEST_ASSERT(evaluations == 1 && fatal_count == 1);
  TEST_ASSERT(fatal_in_isr && fatal_line == line);
  TEST_ASSERT(std::strcmp(fatal_file, __FILE__) == 0);

  fatal_count = 0;
  ASSERT_FROM_CALLBACK(false, true);
#ifdef LIBXR_DEBUG_BUILD
  TEST_ASSERT(fatal_count == 1 && fatal_in_isr);
#else
  TEST_ASSERT(fatal_count == 0);
#endif
  fatal_count = 0;
  DEV_ASSERT_FROM_CALLBACK(false, true);
#ifdef LIBXR_DEV_ASSERT_BUILD
  TEST_ASSERT(fatal_count == 1 && fatal_in_isr);
#else
  TEST_ASSERT(fatal_count == 0);
#endif

  // 测试检查自身在所有开关组合下都必须失败 / Test checks must fail in every mode.
  const pid_t child = fork();
  TEST_ASSERT(child >= 0);
  if (child == 0)
  {
    TEST_ASSERT(false);
    _exit(0);
  }
  int status = 0;
  TEST_ASSERT(waitpid(child, &status, 0) == child);
  TEST_ASSERT(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
  return 0;
}
