#include "libxr.hpp"
#include "libxr_def.hpp"
#include "libxr_time.hpp"
#include <stdio.h>

const char *TEST_NAME = NULL;

#define TEST_STEP(_arg) TEST_NAME = _arg

int main() {
  LibXR::PlatformInit();

  TEST_STEP("Register Error Callback");

  auto err_cb_fun = [](void *arg, const char *file, uint32_t line) {
    printf("Error:Union test failed at step [%s].\r\n", TEST_NAME);
    exit(-1);
  };

  auto err_cb = LibXR::Callback<void, const char *, uint32_t>::Create(
      err_cb_fun, (void *)(0));

  LibXR::Assert::RegisterFatalErrorCB(err_cb);

  TEST_STEP("String Test");

  LibXR::String<100> str1("str"), str2("str");

  ASSERT(str1 == str2);

  str1 = "this is a str";

  ASSERT(str1.Substr<20>(str1.Find(str2.Raw())) == str2);

  TEST_STEP("Timestamp Test");

  LibXR::TimestampMS t1(1000), t2(2005);
  ASSERT(t2 - t1 == 1005);
  ASSERT(fabs((t2 - t1).to_secondf() - 1.005) < 0.0001);

  LibXR::TimestampUS t3(1000), t4(2005);
  ASSERT(t4 - t3 == 1005);
  ASSERT(fabs((t4 - t3).to_secondf() - 0.001005) < 0.0000001);

  return 0;
}