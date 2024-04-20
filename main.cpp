#include "condition_var.hpp"
#include "crc.hpp"
#include "event.hpp"
#include "libxr.hpp"
#include "libxr_cb.hpp"
#include "libxr_def.hpp"
#include "libxr_time.hpp"
#include "libxr_type.hpp"
#include "lockfree_queue.hpp"
#include "messgae.hpp"
#include "queue.hpp"
#include "rbt.hpp"
#include "semaphore.hpp"
#include "signal.hpp"
#include "stack.hpp"
#include "thread.hpp"
#include "timer.hpp"
#include <cstddef>

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

  auto err_cb_fun = [](bool in_isr, void *arg, const char *file,
                       uint32_t line) {
    UNUSED(in_isr);
    UNUSED(arg);
    UNUSED(file);
    UNUSED(line);

    printf("Error:Union test failed at step [%s].\r\n", TEST_NAME);
    *(volatile long long *)(NULL) = 0;
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
  auto lock_free_queue = LibXR::LockFreeQueue<float, 3>();

  void (*lock_free_queue_test_fun)(LibXR::LockFreeQueue<float, 3> *) =
      [](LibXR::LockFreeQueue<float, 3> *queue) {
        queue->Push(1.2f);
        queue->Push(3.8f);
        LibXR::Thread::Sleep(150);
        queue->Push(100.f);
        queue->Push(0.0f);
        queue->Push(2.1f);
        return;
      };

  thread.Create<LibXR::LockFreeQueue<float, 3> *>(
      &lock_free_queue, lock_free_queue_test_fun, "test_task", 512,
      LibXR::Thread::PRIORITY_REALTIME);
  float tmp = 0.0f;

  LibXR::Thread::Sleep(100);
  lock_free_queue.Pop(tmp);
  ASSERT(tmp == 1.2f);
  lock_free_queue.Pop(tmp);
  ASSERT(tmp == 3.8f);
  LibXR::Thread::Sleep(100);
  lock_free_queue.Pop(tmp);
  ASSERT(tmp == 100.f);
  lock_free_queue.Pop(tmp);
  ASSERT(tmp == 0.0f);
  lock_free_queue.Pop(tmp);
  ASSERT(tmp == 2.1f);

  lock_free_queue.Pop(tmp);
  ASSERT(tmp == 2.1f);

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
  tmp = 0.0f;

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
  TEST_STEP("Condition Var Test");
  static LibXR::Semaphore sem_cv(0);
  LibXR::ConditionVar cv;

  void (*cv_fun1)(LibXR::ConditionVar *) = [](LibXR::ConditionVar *cv) {
    cv->Wait(100);
    sem_cv.Post();
    return;
  };

  void (*cv_fun2)(LibXR::ConditionVar *) = [](LibXR::ConditionVar *cv) {
    cv->Wait(100);
    sem_cv.Post();
    return;
  };

  thread.Create<LibXR::ConditionVar *>(&cv, cv_fun1, "cv_fun1", 512,
                                       LibXR::Thread::PRIORITY_REALTIME);
  thread.Create<LibXR::ConditionVar *>(&cv, cv_fun2, "cv_fun2", 512,
                                       LibXR::Thread::PRIORITY_REALTIME);

  LibXR::Thread::Sleep(80);
  cv.Broadcast();
  ASSERT(sem_cv.Wait(20) == NO_ERR);
  ASSERT(sem_cv.Wait(20) == NO_ERR);

  /* --------------------------------------------------------------- */
  TEST_STEP("Event Test");
  static int event_arg = 0;
  void (*event_fun)(bool in_isr, int *, uint32_t) = [](bool in_isr, int *arg,
                                                       uint32_t event) {
    UNUSED(in_isr);
    *arg = *arg + 1;
    ASSERT(event == 0x1234);
  };

  auto event_cb =
      LibXR::Callback<void, uint32_t>::Create(event_fun, &event_arg);

  LibXR::Event event, event_bind;

  event.Register(0x1234, event_cb);
  event.Active(0x1234);
  ASSERT(event_arg == 1);
  for (int i = 0; i <= 0x1234; i++) {
    event.Active(i);
  }
  ASSERT(event_arg == 2);
  event.Bind(event_bind, 0x4321, 0x1234);
  event_bind.Active(0x4321);
  ASSERT(event_arg == 3);

  /* --------------------------------------------------------------- */
  TEST_STEP("Message Test");
  auto domain = LibXR::Topic::Domain("test_domain");
  auto topic =
      LibXR::Topic::CreateTopic<double>("test_tp", &domain, false, true);
  static double msg[4];
  auto sync_suber =
      LibXR::Topic::SyncSubscriber<double>("test_tp", msg[1], &domain);
  LibXR::Queue<double, 10> msg_queue;
  auto queue_suber = LibXR::Topic::QueuedSubscriber(topic, msg_queue);

  void (*msg_cb_fun)(bool, void *, LibXR::RawData &) =
      [](bool, void *, LibXR::RawData &data) {
        msg[3] = *reinterpret_cast<const double *>(data.addr_);
      };
  auto msg_cb =
      LibXR::Callback<void, LibXR::RawData &>::Create(msg_cb_fun, (void *)(0));
  topic.RegisterCallback(msg_cb);

  msg[0] = 16.16;
  topic.Publish(msg[0]);
  ASSERT(sync_suber.Wait(10) == NO_ERR);
  ASSERT(msg[1] == msg[0]);
  ASSERT(msg_queue.Size() == 1);
  msg_queue.Pop(msg[2], 0);
  ASSERT(msg[2] == msg[0]);
  ASSERT(msg[3] == msg[0]);

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

  LibXR::RBTree<int>::Node<int> nodes[100];

  for (int i = 0; i < 100; i++) {
    nodes[i] = i;
    rbtree.Insert(nodes[i], i);
  }

  LibXR::RBTree<int>::Node<int> *node_pos = NULL;
  for (int i = 0; i < 100; i++) {
    node_pos = rbtree.ForeachDisc(node_pos);
    ASSERT(*node_pos == i);
  }

  ASSERT(rbtree.GetNum() == 100);

  static int rbt_arg = 0;

  ErrorCode (*rbt_fun)(LibXR::RBTree<int>::Node<int> &, int *) =
      [](LibXR::RBTree<int>::Node<int> &node, int *arg) {
        *arg = *arg + 1;
        ASSERT(*arg == node + 1);
        return NO_ERR;
      };

  rbtree.Foreach<int, int *>(rbt_fun, &rbt_arg);

  for (int i = 0; i < 100; i++) {
    rbtree.Delete(nodes[i]);
    ASSERT(rbtree.GetNum() == 99 - i)
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