#include "libxr.hpp"
#include "libxr_def.hpp"
#include "libxr_time.hpp"
#include "queue.hpp"
#include "semaphore.hpp"
#include "signal.hpp"
#include "thread.hpp"
#include <stdio.h>

const char *TEST_NAME = NULL;

#define TEST_STEP(_arg)                                                        \
  if (TEST_NAME)                                                               \
    printf("Test [%s] Passed.\n", TEST_NAME);                                  \
  TEST_NAME = _arg

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

  TEST_STEP("Thread Test");

  LibXR::Thread thread;

  LibXR::Semaphore sem(0);

  void (*thread_fun)(LibXR::Semaphore *) = [](LibXR::Semaphore *sem) {
    LibXR::Signal::Wait(5);
    sem->Post();
    return;
  };

  thread.Create<LibXR::Semaphore *>(&sem, thread_fun, "test_task", 512,
                                    LibXR::Thread::PRIORITY_REALTIME);

  LibXR::Thread::Sleep(100);

  LibXR::Signal::Action(thread, 5);

  ASSERT(sem.Wait(100) == NO_ERR);

  TEST_STEP("Queue Test");
  auto queue = LibXR::Queue<float, 3>();

  void (*queue_test_fun)(LibXR::Queue<float, 3> *) =
      [](LibXR::Queue<float, 3> *queue) {
        LibXR::Thread::Sleep(100);
        queue->Push(1.2f);
        LibXR::Thread::Sleep(10);
        queue->Push(3.8f);
        LibXR::Thread::Sleep(10);
        queue->Push(100.f);
        LibXR::Thread::Sleep(10);
        queue->Push(0.0f);
        LibXR::Thread::Sleep(10);
        queue->Push(2.1f);
        return;
      };

  thread.Create<LibXR::Queue<float, 3> *>(&queue, queue_test_fun, "test_task",
                                          512,
                                          LibXR::Thread::PRIORITY_REALTIME);
  float tmp = 0.0f;

  queue.Pop(tmp, 200);
  ASSERT(tmp == 1.2f);
  queue.Pop(tmp, 20);
  ASSERT(tmp == 3.8f);
  queue.Pop(tmp, 20);
  ASSERT(tmp == 100.f);
  queue.Pop(tmp, 20);
  ASSERT(tmp == 0.0f);
  queue.Pop(tmp, 20);
  ASSERT(tmp == 2.1f);

  queue.Pop(tmp, 20);
  ASSERT(tmp == 2.1f);

  TEST_STEP("End");

  return 0;
}