#include "crc.hpp"
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "libxr_time.hpp"
#include "queue.hpp"
#include "rbt.hpp"
#include "semaphore.hpp"
#include "signal.hpp"
#include "stack.hpp"
#include "thread.hpp"
#include "timer.hpp"
#include <cstdint>

const char *TEST_NAME = NULL;

#define TEST_STEP(_arg)                                                        \
  if (TEST_NAME)                                                               \
    printf("Test [%s] Passed.\n", TEST_NAME);                                  \
  TEST_NAME = _arg

int main() {
  LibXR::LibXR_Init();

  LibXR::Thread::Sleep(1000);

  /* --------------------------------------------------------------- */
  TEST_STEP("Register Error Callback");

  auto err_cb_fun = [](void *arg, const char *file, uint32_t line) {
    printf("Error:Union test failed at step [%s].\r\n", TEST_NAME);
    exit(-1);
  };

  auto err_cb = LibXR::Callback<void, const char *, uint32_t>::Create(
      err_cb_fun, (void *)(0));

  LibXR::Assert::RegisterFatalErrorCB(err_cb);

  /* --------------------------------------------------------------- */
  TEST_STEP("String Test");

  LibXR::String<100> str1("str"), str2("str");

  ASSERT(str1 == str2);

  str1 = "this is a str";

  ASSERT(str1.Substr<20>(str1.Find(str2.Raw())) == str2);

  /* --------------------------------------------------------------- */
  TEST_STEP("Timestamp Test");

  LibXR::TimestampMS t1(1000), t2(2005);
  ASSERT(t2 - t1 == 1005);
  ASSERT(fabs((t2 - t1).to_secondf() - 1.005) < 0.0001);

  LibXR::TimestampUS t3(1000), t4(2005);
  ASSERT(t4 - t3 == 1005);
  ASSERT(fabs((t4 - t3).to_secondf() - 0.001005) < 0.0000001);

  /* --------------------------------------------------------------- */
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

  /* --------------------------------------------------------------- */
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

  /* --------------------------------------------------------------- */
  TEST_STEP("Timer Test");
  int timer_arg = 0;
  void (*timer_fun)(int *) = [](int *arg) { *arg = *arg + 1; };

  auto handle = LibXR::Timer::CreatetTask(timer_fun, &timer_arg, 10,
                                          LibXR::Thread::PRIORITY_REALTIME);
  LibXR::Timer::Add(handle);
  LibXR::Timer::Start(handle);

  LibXR::Thread::Sleep(205);
  LibXR::Timer::Stop(handle);
  for (int i = 0; i < 10; i++) {
    timer_arg = 0;
    LibXR::Timer::Start(handle);
    LibXR::Thread::Sleep(205);
    LibXR::Timer::Stop(handle);
    if (timer_arg == 20) {
      break;
    }
  }
  ASSERT(timer_arg == 20);
  UNUSED(timer_arg);

  /* --------------------------------------------------------------- */
  TEST_STEP("Stack Test");
  LibXR::Stack<int, 10> stack;
  for (int i = 0; i < 10; i++) {
    stack.Push(i);
  }

  ASSERT(stack.Push(1) == ERR_FULL);

  for (int i = 0; i <= 9; i++) {
    int tmp = -1;
    stack.Pop(tmp);
    ASSERT(tmp == 9 - i);
  }

  int tmp_int = 0;

  ASSERT(stack.Pop(tmp_int) == ERR_EMPTY);

  /* --------------------------------------------------------------- */
  TEST_STEP("RedBlackTree Test");
  int (*compare_fun)(const int &a, const int &b) =
      [](const int &a, const int &b) { return a - b; };

  LibXR::RBTree<int> rbtree(compare_fun);

  LibXR::RBTree<int>::Node nodes[100];

  for (int i = 0; i < 100; i++) {
    nodes[i] = i;
    rbtree.Insert(nodes[i]);
  }

  LibXR::RBTree<int>::Node *node_pos = NULL;
  for (int i = 0; i < 100; i++) {
    node_pos = rbtree.ForeachDisc(node_pos);
    ASSERT(node_pos->key == i);
  }

  ASSERT(rbtree.GetNum() == 100);

  static int rbt_arg = 0;

  ErrorCode (*rbt_fun)(LibXR::RBTree<int>::Node &,
                       int *) = [](LibXR::RBTree<int>::Node &node, int *arg) {
    *arg = *arg + 1;
    ASSERT(*arg == node.key + 1);
    return NO_ERR;
  };

  rbtree.Foreach<int *>(rbt_fun, &rbt_arg);

  for (int i = 0; i < 100; i++) {
    nodes[i] = i;
    rbtree.Delete(nodes[i]);
  }

  ASSERT(rbtree.GetNum() == 0);

  /* --------------------------------------------------------------- */
  TEST_STEP("CRC8/16/32 Test");

  struct __attribute__((packed)) {
    double a;
    char b;
    uint8_t crc;
  } TestCRC8 = {.a = M_PI, .b = 'X'};

  struct __attribute__((packed)) {
    double a;
    char b;
    uint16_t crc;
  } TestCRC16 = {.a = M_PI * 2, .b = 'X'};

  struct __attribute__((packed)) {
    double a;
    char b;
    uint32_t crc;
  } TestCRC32 = {.a = M_PI * 3, .b = 'X'};

  TestCRC8.crc =
      LibXR::CRC8::Calculate(&TestCRC8, sizeof(TestCRC8) - sizeof(uint8_t));
  TestCRC16.crc =
      LibXR::CRC16::Calculate(&TestCRC16, sizeof(TestCRC16) - sizeof(uint16_t));
  TestCRC32.crc =
      LibXR::CRC32::Calculate(&TestCRC32, sizeof(TestCRC32) - sizeof(uint32_t));

  ASSERT(LibXR::CRC8::Verify(&TestCRC8, sizeof(TestCRC8)));
  ASSERT(LibXR::CRC16::Verify(&TestCRC16, sizeof(TestCRC16)));
  ASSERT(LibXR::CRC32::Verify(&TestCRC32, sizeof(TestCRC32)));

  /* --------------------------------------------------------------- */
  TEST_STEP("End");

  return 0;
}