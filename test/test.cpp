#include "test.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

#include "libxr.hpp"
#include "logger.hpp"

const static char *test_name = nullptr;

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
  const char *name;
  void (*function)();
};

static void run_libxr_tests()
{
  XR_LOG_INFO("Running LibXR Tests...\n");

  TestCase synchronization_tests[] = {
      {"semaphore", test_semaphore},
      {"async", test_async},
  };

  TestCase utility_tests[] = {
      {"crc", test_crc},
      {"encoder", test_float_encoder},
      {"cycle_value", test_cycle_value},
  };

  TestCase data_structure_tests[] = {{"rbt", test_rbt},
                                     {"queue", test_queue},
                                     {"stack", test_stack},
                                     {"list", test_list},
                                     {"double_buffer", test_double_buffer},
                                     {"string", test_string}};

  TestCase threading_tests[] = {
      {"thread", test_thread},
      {"timebase", test_timebase},
      {"timer", test_timer},
  };

  TestCase motion_tests[] = {
      {"inertia", test_inertia},
      {"kinematic", test_kinematic},
      {"transform", test_transform},
  };

  TestCase control_tests[] = {
      {"pid", test_pid},
  };

  TestCase system_tests[] = {
      {"ramfs", test_ramfs},       {"event", test_event},       {"message", test_message},
      {"database", test_database}, {"terminal", test_terminal},
  };

  struct
  {
    TestCase *tests;
    const char *name;
  } test_groups[] = {{synchronization_tests, "synchronization_tests"},
                     {utility_tests, "utility_tests"},
                     {data_structure_tests, "data_structure_tests"},
                     {threading_tests, "threading_tests"},
                     {motion_tests, "motion_tests"},
                     {control_tests, "control_tests"},
                     {system_tests, "system_tests"}};

  size_t group_sizes[] = {
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
      TEST_STEP(test_groups[g].tests[i].name);
      test_groups[g].tests[i].function();
    }
  }

  XR_LOG_INFO("All tests completed.\n");
}

int main()
{
  LibXR::PlatformInit();

  auto err_cb = LibXR::Assert::Callback::Create(
      [](bool in_isr, void *arg, const char *file, uint32_t line)
      {
        UNUSED(in_isr);
        UNUSED(arg);
        UNUSED(file);
        UNUSED(line);

        XR_LOG_ERROR("Error: Union test failed at step [%s].\r\n", test_name);
        // NOLINTNEXTLINE
        *(volatile long long *)(nullptr) = 0;
        exit(-1);
      },
      reinterpret_cast<void *>(0));

  LibXR::Assert::RegisterFatalErrorCB(err_cb);

  run_libxr_tests();

  exit(0);

  return 0;
}
