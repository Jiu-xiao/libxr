/**
 * @file binding_main.cpp
 * @brief Main runner for binding-plane tests.
 *
 * Responsibilities:
 * 1. Install the fatal assertion callback for binding tests.
 * 2. Run the binding-specific test entrypoints in a deterministic order.
 */
#include "test_binding.hpp"

#include <cstdlib>

#include "libxr.hpp"
#include "logger.hpp"

int main()
{
  LibXR::PlatformInit();

  auto err_cb = LibXR::Assert::FatalCallback::Create(
      [](bool in_isr, void* arg, const char* file, uint32_t line)
      {
        UNUSED(in_isr);
        UNUSED(arg);
        UNUSED(file);
        UNUSED(line);

        XR_LOG_ERROR("Error: binding test failed.\r\n");
        exit(-1);
      },
      reinterpret_cast<void*>(0));

  LibXR::Assert::RegisterFatalErrorCallback(err_cb);

  test_print_binding();
  test_database_binding_sequential();
  test_database_binding_raw();
  return 0;
}
