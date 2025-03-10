#include "test.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

#include "libxr.hpp"

const static char *test_name = nullptr;

#define TEST_STEP(_arg)                           \
  do {                                            \
    test_name = _arg;                             \
    if (test_name) {                              \
      printf("\tTest [%s] Passed.\n", test_name); \
    }                                             \
                                                  \
  } while (0)

bool equal(double a, double b) { return std::abs(a - b) < 1e-6; }

struct TestCase {
  const char *name;
  void (*function)();
};

static void run_libxr_tests() {
  std::cout << "Running LibXR Tests...\n";

  TestCase synchronization_tests[] = {
      {"semaphore", test_semaphore},
      {"signal", test_signal},
      {"async", test_async},
      {"condition_var", test_condition_var},
  };

  TestCase utility_tests[] = {
      {"crc", test_crc},
  };

  TestCase data_structure_tests[] = {
      {"rbt", test_rbt},
      {"queue", test_queue},
      {"stack", test_stack},
      {"list", test_list},
  };

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

  TestCase system_tests[] = {
      {"ramfs", test_ramfs},       {"event", test_event},
      {"message", test_message},   {"database", test_database},
      {"terminal", test_terminal},
  };

  struct {
    TestCase *tests;
    const char *name;
  } test_groups[] = {{synchronization_tests, "synchronization_tests"},
                     {utility_tests, "utility_tests"},
                     {data_structure_tests, "data_structure_tests"},
                     {threading_tests, "threading_tests"},
                     {motion_tests, "motion_tests"},
                     {system_tests, "system_tests"}};

  size_t group_sizes[] = {
      sizeof(synchronization_tests) / sizeof(TestCase),
      sizeof(utility_tests) / sizeof(TestCase),
      sizeof(data_structure_tests) / sizeof(TestCase),
      sizeof(threading_tests) / sizeof(TestCase),
      sizeof(motion_tests) / sizeof(TestCase),
      sizeof(system_tests) / sizeof(TestCase),
  };

  size_t num_groups = sizeof(test_groups) / sizeof(test_groups[0]);

  for (size_t g = 0; g < num_groups; ++g) {
    std::cout << "Test Group [" << test_groups[g].name << "]\n";
    for (size_t i = 0; i < group_sizes[g]; ++i) {
      TEST_STEP(test_groups[g].tests[i].name);
      test_groups[g].tests[i].function();
    }
  }

  std::cout << "All tests completed.\n";
}

int main() {
  LibXR::PlatformInit();

  auto err_cb = LibXR::Callback<const char *, uint32_t>::Create(
      [](bool in_isr, void *arg, const char *file, uint32_t line) {
        UNUSED(in_isr);
        UNUSED(arg);
        UNUSED(file);
        UNUSED(line);

        printf("Error: Union test failed at step [%s].\r\n", test_name);
        *(volatile long long *)(nullptr) = 0;
        exit(-1);
      },
      reinterpret_cast<void *>(0));

  LibXR::Assert::RegisterFatalErrorCB(err_cb);
  LibXR::Thread::Sleep(1000);

  run_libxr_tests();

  exit(0);

  return 0;
}