#include "test.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

#include "libxr.hpp"
#include "logger.hpp"

const static char* test_name = nullptr;

#define TEST_STEP(_arg)                                \
  do                                                   \
  {                                                    \
    test_name = _arg;                                  \
    if (test_name)                                     \
    {                                                  \
      XR_LOG_PASS("\tTest [%s] Passed.\n", test_name); \
    }                                                  \
                                                       \
  } while (0)

bool equal(double a, double b) { return std::abs(a - b) < 1e-6; }

struct TestCase
{
  const char* name;
  void (*function)();
  bool isolated;
};

static void run_test_case(const TestCase& test_case)
{
  test_name = test_case.name;

  if (!test_case.isolated)
  {
    test_case.function();
    return;
  }

  pid_t child = fork();
  ASSERT(child >= 0);

  if (child == 0)
  {
    test_case.function();
    _exit(0);
  }

  int status = 0;
  ASSERT(waitpid(child, &status, 0) == child);
  ASSERT(WIFEXITED(status));
  ASSERT(WEXITSTATUS(status) == 0);
}

static void run_libxr_tests()
{
  XR_LOG_INFO("Running LibXR Tests...\n");

  TestCase core_tests[] = {
      {"callback", test_cb, false},
      {"pipe", test_pipe, false},
      {"rw", test_rw, false},
      {"memory", test_memory, false},
  };

  TestCase synchronization_tests[] = {
      {"semaphore", test_semaphore, false},
      {"mutex", test_mutex, false},
      {"async", test_async, false},
  };

  TestCase utility_tests[] = {
      {"crc", test_crc, false},
      {"encoder", test_float_encoder, false},
      {"cycle_value", test_cycle_value, false},
      {"print", test_print, false},
      {"usb_media_classes", test_usb_media_classes, false},
  };

  TestCase data_structure_tests[] = {{"rbt", test_rbt, false},
                                     {"queue", test_queue, false},
                                     {"pool", test_lock_free_pool, false},
                                     {"stack", test_stack, false},
                                     {"list", test_list, false},
                                     {"double_buffer", test_double_buffer, false},
                                     {"string", test_string, false}};

  TestCase threading_tests[] = {
      {"thread", test_thread, false},
      {"timebase", test_timebase, false},
      {"timer", test_timer, false},
  };

  TestCase motion_tests[] = {
      {"inertia", test_inertia, false},
      {"kinematic", test_kinematic, false},
      {"transform", test_transform, false},
  };

  TestCase control_tests[] = {
      {"pid", test_pid, false},
  };

  TestCase system_tests[] = {{"ramfs", test_ramfs, false},
                             {"event", test_event, false},
                             {"message", test_message, false},
                             {"database", test_database, false},
                             {"terminal", test_terminal, true},
                             {"linux_shm_topic", test_linux_shm_topic, true}};

  struct
  {
    TestCase* tests;
    const char* name;
  } test_groups[] = {{core_tests, "core_tests"},
                     {synchronization_tests, "synchronization_tests"},
                     {utility_tests, "utility_tests"},
                     {data_structure_tests, "data_structure_tests"},
                     {threading_tests, "threading_tests"},
                     {motion_tests, "motion_tests"},
                     {control_tests, "control_tests"},
                     {system_tests, "system_tests"}};

  size_t group_sizes[] = {
      sizeof(core_tests) / sizeof(TestCase),
      sizeof(synchronization_tests) / sizeof(TestCase),
      sizeof(utility_tests) / sizeof(TestCase),
      sizeof(data_structure_tests) / sizeof(TestCase),
      sizeof(threading_tests) / sizeof(TestCase),
      sizeof(motion_tests) / sizeof(TestCase),
      sizeof(control_tests) / sizeof(TestCase),
      sizeof(system_tests) / sizeof(TestCase),
  };

  size_t num_groups = sizeof(test_groups) / sizeof(test_groups[0]);

  for (size_t g = 0; g < num_groups; ++g)
  {
    XR_LOG_INFO("Test Group [%s]\n", test_groups[g].name);
    for (size_t i = 0; i < group_sizes[g]; ++i)
    {
      run_test_case(test_groups[g].tests[i]);
      TEST_STEP(test_groups[g].tests[i].name);
    }
  }

  XR_LOG_INFO("All tests completed.\n");
}

int main()
{
  LibXR::PlatformInit();

  auto err_cb = LibXR::Assert::Callback::Create(
      [](bool in_isr, void* arg, const char* file, uint32_t line)
      {
        UNUSED(in_isr);
        UNUSED(arg);
        UNUSED(file);
        UNUSED(line);

        XR_LOG_ERROR("Error: Union test failed at step [%s].\r\n", test_name);
        // NOLINTNEXTLINE
        *(volatile long long*)(nullptr) = 0;
        exit(-1);
      },
      reinterpret_cast<void*>(0));

  LibXR::Assert::RegisterFatalErrorCallback(err_cb);

  run_libxr_tests();

  exit(0);

  return 0;
}
