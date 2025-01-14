#include "async.hpp"
#include "condition_var.hpp"
#include "crc.hpp"
#include "event.hpp"
#include "inertia.hpp"
#include "kinematic.hpp"
#include "libxr.hpp"
#include "libxr_cb.hpp"
#include "libxr_def.hpp"
#include "libxr_system.hpp"
#include "libxr_time.hpp"
#include "libxr_type.hpp"
#include "lockfree_queue.hpp"
#include "message.hpp"
#include "queue.hpp"
#include "ramfs.hpp"
#include "rbt.hpp"
#include "semaphore.hpp"
#include "signal.hpp"
#include "stack.hpp"
#include "terminal.hpp"
#include "thread.hpp"
#include "timebase.hpp"
#include "timer.hpp"
#include "transform.hpp"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iostream>

const char *TEST_NAME = nullptr;

#define TEST_STEP(_arg)                                                        \
  if (TEST_NAME)                                                               \
    printf("Test [%s] Passed.\n", TEST_NAME);                                  \
  TEST_NAME = _arg

static bool equal(double a, double b) { return std::abs(a - b) < 1e-6; }

int main() {
  LibXR::PlatformInit();

  LibXR::Thread::Sleep(1000);

  /* --------------------------------------------------------------- */
  TEST_STEP("Register Error Callback");

  auto err_cb = LibXR::Callback<const char *, uint32_t>::Create(
      [](bool in_isr, void *arg, const char *file, uint32_t line) {
        UNUSED(in_isr);
        UNUSED(arg);
        UNUSED(file);
        UNUSED(line);

        printf("Error:Union test failed at step [%s].\r\n", TEST_NAME);
        *(volatile long long *)(nullptr) = 0;
        exit(-1);
      },
      (void *)(0));

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
  TEST_STEP("Timebase Test");

  t1 = LibXR::Timebase::GetMilliseconds();
  t3 = LibXR::Timebase::GetMicroseconds();
  LibXR::Thread::Sleep(100);
  t4 = LibXR::Timebase::GetMicroseconds();
  t2 = LibXR::Timebase::GetMilliseconds();

  ASSERT(fabs(t2 - t1 - 100) < 2);
  ASSERT(fabs(t4 - t3 - 100000) < 2000);

  /* --------------------------------------------------------------- */
  TEST_STEP("Thread Test");

  LibXR::Thread thread;

  LibXR::Semaphore sem(0);

  thread.Create<LibXR::Semaphore *>(
      &sem,
      [](LibXR::Semaphore *sem) {
        LibXR::Signal::Wait(5);
        sem->Post();
        return;
      },
      "test_task", 512, LibXR::Thread::Priority::REALTIME);

  LibXR::Thread::Sleep(100);

  LibXR::Signal::Action(thread, 5);

  ASSERT(sem.Wait(100) == ErrorCode::OK);

  /* --------------------------------------------------------------- */
  TEST_STEP("Queue Test");
  auto lock_free_queue = LibXR::LockFreeQueue<float>(3);
  lock_free_queue.Reset();

  thread.Create<LibXR::LockFreeQueue<float> *>(
      &lock_free_queue,
      [](LibXR::LockFreeQueue<float> *queue) {
        queue->Push(1.2f);
        queue->Push(3.8f);
        LibXR::Thread::Sleep(150);
        queue->Push(100.f);
        queue->Push(0.0f);
        queue->Push(2.1f);
        return;
      },
      "test_task", 512, LibXR::Thread::Priority::REALTIME);
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

  auto queue = LibXR::LockQueue<float>(3);
  queue.Reset();

  thread.Create<LibXR::LockQueue<float> *>(
      &queue,
      [](LibXR::LockQueue<float> *queue) {
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
      },
      "test_task", 512, LibXR::Thread::Priority::REALTIME);
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

  auto chunked_queue = LibXR::ChunkedQueue(10, 10);

  uint16_t tmp_u16 = 0;

  for (int i = 0; i < 30; i++) {
    tmp_u16 = i;
    chunked_queue.PushPartial(&tmp_u16, sizeof(tmp_u16));
  }

  for (int i = 0; i < 6; i++) {
    uint16_t tmp[5];
    uint16_t size;
    chunked_queue.Pop(&size, tmp);
    ASSERT(size == 10);
    for (int j = 0; j < 5; j++) {
      ASSERT(tmp[j] == i * 5 + j);
    }
  }

  for (int i = 0; i < 10; i++) {
    double tmp = i;
    uint16_t size;
    chunked_queue.PushPartial(&tmp, sizeof(tmp));
    chunked_queue.Pop(&size, &tmp);
    ASSERT(fabs(tmp - double(i)) < 0.01);
  }

  ASSERT(chunked_queue.Size() == 0);

  /* --------------------------------------------------------------- */
  TEST_STEP("Timer Test");
  int timer_arg = 0;

  auto handle = LibXR::Timer::CreatetTask<int *>(
      [](int *arg) { *arg = *arg + 1; }, &timer_arg, 10);

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

  /* --------------------------------------------------------------- */
  TEST_STEP("Condition Var Test");
  static LibXR::Semaphore sem_cv(0);
  LibXR::ConditionVar cv;

  thread.Create<LibXR::ConditionVar *>(
      &cv,
      [](LibXR::ConditionVar *cv) {
        ASSERT(cv->Wait(100) == ErrorCode::OK);
        sem_cv.Post();
        return;
      },
      "cv_fun1", 512, LibXR::Thread::Priority::REALTIME);

  thread.Create<LibXR::ConditionVar *>(
      &cv,
      [](LibXR::ConditionVar *cv) {
        ASSERT(cv->Wait(100) == ErrorCode::OK);
        sem_cv.Post();
        return;
      },
      "cv_fun2", 512, LibXR::Thread::Priority::REALTIME);

  LibXR::Thread::Sleep(80);
  cv.Broadcast();
  ASSERT(sem_cv.Wait(20) == ErrorCode::OK);
  ASSERT(sem_cv.Wait(20) == ErrorCode::OK);

  /* --------------------------------------------------------------- */
  TEST_STEP("Event Test");
  static int event_arg = 0;

  auto event_cb = LibXR::Callback<uint32_t>::Create(
      [](bool in_isr, int *arg, uint32_t event) {
        UNUSED(in_isr);
        *arg = *arg + 1;
        ASSERT(event == 0x1234);
      },
      &event_arg);

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
  TEST_STEP("ASync Test");

  int async_arg = 0;
  auto async_cb = LibXR::Callback<LibXR::ASync *>::Create(
      [](bool in_isr, int *arg, LibXR::ASync *async) {
        ASSERT(in_isr == false);
        LibXR::Thread::Sleep(10);
        *arg = *arg + 1;
      },
      &async_arg);

  LibXR::ASync async(512, LibXR::Thread::Priority::REALTIME);
  for (int i = 0; i < 10; i++) {
    ASSERT(async.GetStatus() == LibXR::ASync::Status::REDAY);
    async.AssignJob(async_cb);

    ASSERT(async_arg == i);
    ASSERT(async.GetStatus() == LibXR::ASync::Status::BUSY);
    LibXR::Thread::Sleep(20);

    ASSERT(async_arg == i + 1);
    ASSERT(async.GetStatus() == LibXR::ASync::Status::DONE);
    ASSERT(async.GetStatus() == LibXR::ASync::Status::REDAY);
  }

  /* --------------------------------------------------------------- */
  TEST_STEP("RamFS Test");

  auto ramfs = LibXR::RamFS();

  int ramfs_arg = 0;

  auto file = LibXR::RamFS::CreateFile<int *>(
      "test_file",
      [](int *&arg, int argc, char **argv) {
        UNUSED(argc);
        UNUSED(argv);
        *arg = *arg + 1;
        return 0;
      },
      &ramfs_arg);

  auto file_1 = LibXR::RamFS::CreateFile("test_file1", ramfs_arg);

  auto dir = LibXR::RamFS::CreateDir("test_dir");

  auto dev = LibXR::RamFS::Device("test_dev");
  file_1->size = 4;
  for (int i = 1; i < 10; i++) {
    file->Run(0, nullptr);
    ASSERT(file_1->GetData<int>() == i);
  }

  ramfs.Add(dir);
  ramfs.Add(file_1);
  dir.Add(file);
  dir.Add(dev);

  ASSERT(ramfs.FindDir("test") == nullptr);
  ASSERT(ramfs.FindFile("test") == nullptr);
  ASSERT(ramfs.FindDevice("test") == nullptr);
  ASSERT(dir.FindDevice("test") == nullptr);
  ASSERT(dir.FindFile("test") == nullptr);

  ASSERT(ramfs.FindDir("test_dir") == &dir);
  ASSERT(ramfs.FindFile("test_file") == &file);
  ASSERT(ramfs.FindDevice("test_dev") == &dev);
  ASSERT(dir.FindDevice("test_dev") == &dev);
  ASSERT(dir.FindFile("test_file") == &file);

  /* --------------------------------------------------------------- */
  TEST_STEP("Message Test");
  auto domain = LibXR::Topic::Domain("test_domain");
  auto topic =
      LibXR::Topic::CreateTopic<double>("test_tp", &domain, false, true);
  static double msg[4];
  auto sync_suber =
      LibXR::Topic::SyncSubscriber<double>("test_tp", msg[1], &domain);
  LibXR::LockQueue<double> msg_queue(10);
  auto queue_suber = LibXR::Topic::QueuedSubscriber(topic, msg_queue);

  auto msg_cb = LibXR::Callback<LibXR::RawData &>::Create(
      [](bool, void *, LibXR::RawData &data) {
        msg[3] = *reinterpret_cast<const double *>(data.addr_);
      },
      (void *)(0));

  topic.RegisterCallback(msg_cb);

  msg[0] = 16.16;
  topic.Publish(msg[0]);
  ASSERT(sync_suber.Wait(10) == ErrorCode::OK);
  ASSERT(msg[1] == msg[0]);
  ASSERT(msg_queue.Size() == 1);
  msg_queue.Pop(msg[2], 0);
  ASSERT(msg[2] == msg[0]);
  ASSERT(msg[3] == msg[0]);

  topic.Publish(msg[0]);
  msg[1] = 0.0f;
  LibXR::Topic::PackedData<double> packed_data;
  LibXR::Topic::Server topic_server(512);

  topic.DumpData(packed_data);
  topic_server.Register(topic);

  topic_server.ParseData(LibXR::ConstRawData(packed_data));

  ASSERT(msg[1] == msg[0]);

  for (int i = 0; i < 1000; i++) {
    msg[0] = i * 0.1;
    topic.Publish(msg[0]);
    topic.DumpData(packed_data);
    topic_server.ParseData(LibXR::ConstRawData(packed_data));
    ASSERT(msg[1] == msg[0]);
  }

  for (int i = 0; i < 1000; i++) {
    msg[0] = i * 0.1;
    topic.Publish(msg[0]);
    topic.DumpData(packed_data);
    for (uint8_t j = 0; j < 255; j++) {
      topic_server.ParseData(LibXR::ConstRawData(j));
    }
    for (int j = 0; j < sizeof(packed_data); j++) {
      auto tmp = reinterpret_cast<uint8_t *>(&packed_data);
      topic_server.ParseData(LibXR::ConstRawData(tmp[j]));
    }
    ASSERT(msg[1] == msg[0]);
  }

  /* --------------------------------------------------------------- */
  TEST_STEP("Stack Test");
  LibXR::Stack<int> stack(10);
  for (int i = 0; i < 10; i++) {
    stack.Push(i);
  }

  ASSERT(stack.Push(1) == ErrorCode::FULL);

  for (int i = 0; i <= 9; i++) {
    int tmp = -1;
    stack.Pop(tmp);
    ASSERT(tmp == 9 - i);
  }

  ASSERT(stack.Pop() == ErrorCode::EMPTY);

  for (int i = 0; i <= 5; i++) {
    stack.Push(i);
  }

  stack.Insert(10, 2);
  ASSERT(stack[2] == 10);
  ASSERT(stack[3] == 2);
  ASSERT(stack.Size() == 7);
  stack.Delete(2);
  ASSERT(stack[2] == 2);
  ASSERT(stack[3] == 3);
  ASSERT(stack.Size() == 6);

  /* --------------------------------------------------------------- */
  TEST_STEP("RedBlackTree Test");

  LibXR::RBTree<int> rbtree([](const int &a, const int &b) { return a - b; });

  LibXR::RBTree<int>::Node<int> nodes[100];

  for (int i = 0; i < 100; i++) {
    nodes[i] = i;
    rbtree.Insert(nodes[i], i);
  }

  LibXR::RBTree<int>::Node<int> *node_pos = nullptr;
  for (int i = 0; i < 100; i++) {
    node_pos = rbtree.ForeachDisc(node_pos);
    ASSERT(*node_pos == i);
  }

  ASSERT(rbtree.GetNum() == 100);

  static int rbt_arg = 0;

  rbtree.Foreach<int, int *>(
      [](LibXR::RBTree<int>::Node<int> &node, int *arg) {
        *arg = *arg + 1;
        ASSERT(*arg == node + 1);
        return ErrorCode::OK;
      },
      &rbt_arg);

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
  TEST_STEP("Transfromation");
  LibXR::Position pos(1., 8., 0.3);
  LibXR::Position pos_new;
  LibXR::EulerAngle eulr = {M_PI / 12, M_PI / 6, M_PI / 4}, eulr_new;
  LibXR::RotationMatrix rot, rot_new;
  LibXR::Quaternion quat, quat_new;

  /* Position */
  rot = eulr.toRotationMatrix();
  quat = LibXR::Quaternion(rot);

  pos_new = pos * quat;
  quat_new = pos_new / pos;
  rot_new = quat_new.toRotationMatrix();
  pos_new = pos_new / rot_new;
  ASSERT(equal(pos_new(0), pos(0)) && equal(pos_new(1), pos(1)) &&
         equal(pos_new(2), pos(2)));

  pos_new /= quat;
  pos_new *= rot;
  ASSERT(equal(pos_new(0), pos(0)) && equal(pos_new(1), pos(1)) &&
         equal(pos_new(2), pos(2)));

  pos_new = (pos - pos_new) * 2.;
  pos_new *= 2;
  pos_new /= 4;
  ASSERT(equal(pos_new(0), 0.) && equal(pos_new(1), 0.) &&
         equal(pos_new(2), 0.));

  pos_new = pos + pos_new;
  ASSERT(equal(pos_new(0), pos(0)) && equal(pos_new(1), pos(1)) &&
         equal(pos_new(2), pos(2)));

  pos_new -= pos;

  ASSERT(equal(pos_new(0), 0.) && equal(pos_new(1), 0.) &&
         equal(pos_new(2), 0.));

  pos_new += pos;
  ASSERT(equal(pos_new(0), pos(0)) && equal(pos_new(1), pos(1)) &&
         equal(pos_new(2), pos(2)));

  /* Rotation */
  quat_new = quat;
  quat_new = quat - quat_new;
  quat_new = quat + quat_new;
  ASSERT(equal(quat_new(0), quat(0)) && equal(quat_new(1), quat(1)) &&
         equal(quat_new(2), quat(2)) && equal(quat_new(3), quat(3)));

  /* ZYX Order */
  rot = eulr.toRotationMatrixZYX();
  ASSERT(equal(rot(0, 0), 0.6123725) && equal(rot(0, 1), -0.5915064) &&
         equal(rot(0, 2), 0.5245190) && equal(rot(1, 0), 0.6123725) &&
         equal(rot(1, 1), 0.7745190) && equal(rot(1, 2), 0.1584937) &&
         equal(rot(2, 0), -0.5000000) && equal(rot(2, 1), 0.2241439) &&
         equal(rot(2, 2), 0.8365163));

  eulr_new = rot.toEulerAngleZYX();
  ASSERT(equal(eulr_new(0), eulr(0)) && equal(eulr_new(1), eulr(1)) &&
         equal(eulr_new(2), eulr(2)));

  quat = LibXR::Quaternion(rot);
  quat_new = eulr.toQuaternionZYX();
  ASSERT(equal(quat_new.w(), quat.w()) && equal(quat_new.x(), quat.x()) &&
         equal(quat_new.y(), quat.y()) && equal(quat_new.z(), quat.z()));

  rot_new = quat.toRotationMatrix();
  ASSERT(equal(rot_new(0, 0), rot(0, 0)) && equal(rot_new(0, 1), rot(0, 1)) &&
         equal(rot_new(0, 2), rot(0, 2)) && equal(rot_new(1, 0), rot(1, 0)) &&
         equal(rot_new(1, 1), rot(1, 1)) && equal(rot_new(1, 2), rot(1, 2)) &&
         equal(rot_new(2, 0), rot(2, 0)) && equal(rot_new(2, 1), rot(2, 1)) &&
         equal(rot_new(2, 2), rot(2, 2)));

  eulr_new = quat.toEulerAngleZYX();
  ASSERT(equal(eulr_new(0), eulr(0)) && equal(eulr_new(1), eulr(1)) &&
         equal(eulr_new(2), eulr(2)));

  /* ZXY Order */
  rot = eulr.toRotationMatrixZXY();
  ASSERT(equal(rot(0, 0), 0.5208661) && equal(rot(0, 1), -0.6830127) &&
         equal(rot(0, 2), 0.5120471) && equal(rot(1, 0), 0.7038788) &&
         equal(rot(1, 1), 0.6830127) && equal(rot(1, 2), 0.1950597) &&
         equal(rot(2, 0), -0.4829629) && equal(rot(2, 1), 0.2588190) &&
         equal(rot(2, 2), 0.8365163));

  eulr_new = rot.toEulerAngleZXY();
  ASSERT(equal(eulr_new(0), eulr(0)) && equal(eulr_new(1), eulr(1)) &&
         equal(eulr_new(2), eulr(2)));

  quat = LibXR::Quaternion(rot);
  quat_new = eulr.toQuaternionZXY();
  ASSERT(equal(quat_new.w(), quat.w()) && equal(quat_new.x(), quat.x()) &&
         equal(quat_new.y(), quat.y()) && equal(quat_new.z(), quat.z()));

  rot_new = quat.toRotationMatrix();
  ASSERT(equal(rot_new(0, 0), rot(0, 0)) && equal(rot_new(0, 1), rot(0, 1)) &&
         equal(rot_new(0, 2), rot(0, 2)) && equal(rot_new(1, 0), rot(1, 0)) &&
         equal(rot_new(1, 1), rot(1, 1)) && equal(rot_new(1, 2), rot(1, 2)) &&
         equal(rot_new(2, 0), rot(2, 0)) && equal(rot_new(2, 1), rot(2, 1)) &&
         equal(rot_new(2, 2), rot(2, 2)));

  eulr_new = quat.toEulerAngleZXY();
  ASSERT(equal(eulr_new(0), eulr(0)) && equal(eulr_new(1), eulr(1)) &&
         equal(eulr_new(2), eulr(2)));

  /* YXZ Order */
  rot = eulr.toRotationMatrixYXZ();
  ASSERT(equal(rot(0, 0), 0.7038788) && equal(rot(0, 1), -0.5208661) &&
         equal(rot(0, 2), 0.4829629) && equal(rot(1, 0), 0.6830127) &&
         equal(rot(1, 1), 0.6830127) && equal(rot(1, 2), -0.2588190) &&
         equal(rot(2, 0), -0.1950597) && equal(rot(2, 1), 0.5120471) &&
         equal(rot(2, 2), 0.8365163));

  eulr_new = rot.toEulerAngleYXZ();
  ASSERT(equal(eulr_new(0), eulr(0)) && equal(eulr_new(1), eulr(1)) &&
         equal(eulr_new(2), eulr(2)));

  quat = LibXR::Quaternion(rot);
  quat_new = eulr.toQuaternionYXZ();
  ASSERT(equal(quat_new.w(), quat.w()) && equal(quat_new.x(), quat.x()) &&
         equal(quat_new.y(), quat.y()) && equal(quat_new.z(), quat.z()));

  rot_new = quat.toRotationMatrix();
  ASSERT(equal(rot_new(0, 0), rot(0, 0)) && equal(rot_new(0, 1), rot(0, 1)) &&
         equal(rot_new(0, 2), rot(0, 2)) && equal(rot_new(1, 0), rot(1, 0)) &&
         equal(rot_new(1, 1), rot(1, 1)) && equal(rot_new(1, 2), rot(1, 2)) &&
         equal(rot_new(2, 0), rot(2, 0)) && equal(rot_new(2, 1), rot(2, 1)) &&
         equal(rot_new(2, 2), rot(2, 2)));

  eulr_new = quat.toEulerAngleYXZ();
  ASSERT(equal(eulr_new(0), eulr(0)) && equal(eulr_new(1), eulr(1)) &&
         equal(eulr_new(2), eulr(2)));

  /* XYZ Order */
  rot = eulr.toRotationMatrixXYZ();
  ASSERT(equal(rot(0, 0), 0.6123725) && equal(rot(0, 1), -0.6123725) &&
         equal(rot(0, 2), 0.5000000) && equal(rot(1, 0), 0.7745190) &&
         equal(rot(1, 1), 0.5915064) && equal(rot(1, 2), -0.2241439) &&
         equal(rot(2, 0), -0.1584937) && equal(rot(2, 1), 0.5245190) &&
         equal(rot(2, 2), 0.8365163));

  eulr_new = rot.toEulerAngleXYZ();
  ASSERT(equal(eulr_new(0), eulr(0)) && equal(eulr_new(1), eulr(1)) &&
         equal(eulr_new(2), eulr(2)));

  quat = LibXR::Quaternion(rot);
  quat_new = eulr.toQuaternionXYZ();
  ASSERT(equal(quat_new.w(), quat.w()) && equal(quat_new.x(), quat.x()) &&
         equal(quat_new.y(), quat.y()) && equal(quat_new.z(), quat.z()));

  rot_new = quat.toRotationMatrix();
  ASSERT(equal(rot_new(0, 0), rot(0, 0)) && equal(rot_new(0, 1), rot(0, 1)) &&
         equal(rot_new(0, 2), rot(0, 2)) && equal(rot_new(1, 0), rot(1, 0)) &&
         equal(rot_new(1, 1), rot(1, 1)) && equal(rot_new(1, 2), rot(1, 2)) &&
         equal(rot_new(2, 0), rot(2, 0)) && equal(rot_new(2, 1), rot(2, 1)) &&
         equal(rot_new(2, 2), rot(2, 2)));

  eulr_new = quat.toEulerAngleXYZ();
  ASSERT(equal(eulr_new(0), eulr(0)) && equal(eulr_new(1), eulr(1)) &&
         equal(eulr_new(2), eulr(2)));

  /* XZY Order */
  rot = eulr.toRotationMatrixXZY();
  ASSERT(equal(rot(0, 0), 0.6123725) && equal(rot(0, 1), -0.7071068) &&
         equal(rot(0, 2), 0.3535534) && equal(rot(1, 0), 0.7209159) &&
         equal(rot(1, 1), 0.6830127) && equal(rot(1, 2), 0.1173625) &&
         equal(rot(2, 0), -0.3244693) && equal(rot(2, 1), 0.1830127) &&
         equal(rot(2, 2), 0.9280227));

  eulr_new = rot.toEulerAngleXZY();
  ASSERT(equal(eulr_new(0), eulr(0)) && equal(eulr_new(1), eulr(1)) &&
         equal(eulr_new(2), eulr(2)));

  quat = LibXR::Quaternion(rot);
  quat_new = eulr.toQuaternionXZY();
  ASSERT(equal(quat_new.w(), quat.w()) && equal(quat_new.x(), quat.x()) &&
         equal(quat_new.y(), quat.y()) && equal(quat_new.z(), quat.z()));

  rot_new = quat.toRotationMatrix();
  ASSERT(equal(rot_new(0, 0), rot(0, 0)) && equal(rot_new(0, 1), rot(0, 1)) &&
         equal(rot_new(0, 2), rot(0, 2)) && equal(rot_new(1, 0), rot(1, 0)) &&
         equal(rot_new(1, 1), rot(1, 1)) && equal(rot_new(1, 2), rot(1, 2)) &&
         equal(rot_new(2, 0), rot(2, 0)) && equal(rot_new(2, 1), rot(2, 1)) &&
         equal(rot_new(2, 2), rot(2, 2)));

  eulr_new = quat.toEulerAngleXZY();
  ASSERT(equal(eulr_new(0), eulr(0)) && equal(eulr_new(1), eulr(1)) &&
         equal(eulr_new(2), eulr(2)));

  /* YZX Order */
  rot = eulr.toRotationMatrixYZX();
  ASSERT(equal(rot(0, 0), 0.6123725) && equal(rot(0, 1), -0.4620968) &&
         equal(rot(0, 2), 0.6414565) && equal(rot(1, 0), 0.7071068) &&
         equal(rot(1, 1), 0.6830127) && equal(rot(1, 2), -0.1830127) &&
         equal(rot(2, 0), -0.3535534) && equal(rot(2, 1), 0.5656502) &&
         equal(rot(2, 2), 0.7450100));

  eulr_new = rot.toEulerAngleYZX();
  ASSERT(equal(eulr_new(0), eulr(0)) && equal(eulr_new(1), eulr(1)) &&
         equal(eulr_new(2), eulr(2)));

  quat = LibXR::Quaternion(rot);
  quat_new = eulr.toQuaternionYZX();
  ASSERT(equal(quat_new.w(), quat.w()) && equal(quat_new.x(), quat.x()) &&
         equal(quat_new.y(), quat.y()) && equal(quat_new.z(), quat.z()));

  rot_new = quat.toRotationMatrix();
  ASSERT(equal(rot_new(0, 0), rot(0, 0)) && equal(rot_new(0, 1), rot(0, 1)) &&
         equal(rot_new(0, 2), rot(0, 2)) && equal(rot_new(1, 0), rot(1, 0)) &&
         equal(rot_new(1, 1), rot(1, 1)) && equal(rot_new(1, 2), rot(1, 2)) &&
         equal(rot_new(2, 0), rot(2, 0)) && equal(rot_new(2, 1), rot(2, 1)) &&
         equal(rot_new(2, 2), rot(2, 2)));

  eulr_new = quat.toEulerAngleYZX();
  ASSERT(equal(eulr_new(0), eulr(0)) && equal(eulr_new(1), eulr(1)) &&
         equal(eulr_new(2), eulr(2)));

  /* --------------------------------------------------------------- */
  TEST_STEP("Interia");
  double Ixx = 1., Iyy = 1., Izz = 1., Ixy = 0., Ixz = 0., Iyz = 0.;
  pos = LibXR::Position(std::sqrt(0.5), std::sqrt(0.5), 0.);
  eulr = LibXR::EulerAngle(0., 0., M_PI / 4);

  LibXR::Inertia inertia(0.1, Ixx, Iyy, Izz, Ixy, Ixz, Iyz);

  auto inertia_new = inertia.Translate(pos);
  inertia_new = inertia_new.Rotate(eulr.toQuaternion());

  ASSERT(equal(inertia_new(0, 0), 1.1) && equal(inertia_new(0, 1), 0.) &&
         equal(inertia_new(0, 2), 0.) && equal(inertia_new(1, 0), 0.) &&
         equal(inertia_new(1, 1), 1.) && equal(inertia_new(1, 2), 0.) &&
         equal(inertia_new(2, 0), 0.) && equal(inertia_new(2, 1), 0.) &&
         equal(inertia_new(2, 2), 1.1));

  inertia_new = inertia.Translate(pos);
  inertia_new = inertia_new.Rotate(eulr.toRotationMatrix());

  ASSERT(equal(inertia_new(0, 0), 1.1) && equal(inertia_new(0, 1), 0.) &&
         equal(inertia_new(0, 2), 0.) && equal(inertia_new(1, 0), 0.) &&
         equal(inertia_new(1, 1), 1.) && equal(inertia_new(1, 2), 0.) &&
         equal(inertia_new(2, 0), 0.) && equal(inertia_new(2, 1), 0.) &&
         equal(inertia_new(2, 2), 1.1));
  /* --------------------------------------------------------------- */
  TEST_STEP("Kinematics");
  LibXR::Inertia inertia_endpoint(1.0, 0.1, 0.1, 0.1, 0., 0., 0.);
  LibXR::Inertia inertia_midpoint(1.0, 0.1, 0.1, 0.1, 0., 0., 0.);
  LibXR::Inertia inertia_startpoint(1000., 100., 100., 100., 0., 0., 0.);

  LibXR::Position pos_startpoint(0., 0., 1.);
  LibXR::Quaternion quat_startpoint =
      LibXR::EulerAngle(0., 0., 0.).toQuaternion();

  LibXR::Position pos_startpoint2joint(0., 0.0, 0.5);
  LibXR::Position pos_joint2midpoint(1.0, 0.0, 0.0);
  LibXR::Position pos_midpoint2joint(1.0, 0.0, 0.0);
  LibXR::Position pos_joint2endpoint(0.5, 0.0, 0.0);

  LibXR::Quaternion quat_startpoint2joint(1., 0., 0., 0.);
  LibXR::Quaternion quat_joint2midpoint(1., 0., 0., 0.);
  LibXR::Quaternion quat_midpoint2joint(1., 0., 0., 0.);
  LibXR::Quaternion quat_joint2endpoint(1., 0., 0., 0.);

  LibXR::Transform t_startpoint2joint(quat_startpoint2joint,
                                      pos_startpoint2joint);
  LibXR::Transform t_joint2midpoint(quat_joint2midpoint, pos_joint2midpoint);
  LibXR::Transform t_midpoint2joint(quat_midpoint2joint, pos_midpoint2joint);
  LibXR::Transform t_joint2endpoint(quat_joint2endpoint, pos_joint2endpoint);

  LibXR::Kinematic::EndPoint object_endpoint(inertia_endpoint);
  LibXR::Kinematic::Object object_midpoint(inertia_midpoint);
  LibXR::Kinematic::StartPoint object_startpoint(inertia_startpoint);

  object_startpoint.SetPosition(pos_startpoint);
  object_startpoint.SetQuaternion(quat_startpoint);

  LibXR::Kinematic::Joint joint_midpoint(LibXR::Axis<>::Y(), &object_startpoint,
                                         t_startpoint2joint, &object_midpoint,
                                         t_joint2midpoint);

  LibXR::Kinematic::Joint joint_endpoint(LibXR::Axis<>::Y(), &object_midpoint,
                                         t_midpoint2joint, &object_endpoint,
                                         t_joint2endpoint);

  joint_endpoint.SetState(0.);
  joint_midpoint.SetState(M_PI / 2);

  LibXR::Quaternion target_quat(0.7071068, 0., -0.7071068, 0.);
  LibXR::Position target_pos(0., 0., 4.);

  object_endpoint.SetTargetQuaternion(target_quat);
  object_endpoint.SetTargetPosition(target_pos);

  object_startpoint.CalcForward();
  object_startpoint.CalcInertia();

  object_endpoint.CalcBackward(0, 1000, 0.01, 0.1);

  ASSERT(std::abs(std::abs(object_endpoint.target_pos_(0)) -
                  std::abs(object_endpoint.runtime_.target.translation(0))) <
         0.01);

  ASSERT(std::abs(std::abs(object_endpoint.target_pos_(1)) -
                  std::abs(object_endpoint.runtime_.target.translation(1))) <
         0.01);

  ASSERT(std::abs(std::abs(object_endpoint.target_pos_(2)) -
                  std::abs(object_endpoint.runtime_.target.translation(2))) <
         0.01);

  /* --------------------------------------------------------------- */
  TEST_STEP("Terminal");
  LibXR::Terminal terminal(ramfs);
  LibXR::Thread term_thread;
  term_thread.Create(&terminal, terminal.ThreadFun, "terminal", 512,
                     LibXR::Thread::Priority::MEDIUM);
  LibXR::Thread::Sleep(10000);
  printf("\n");
  /* --------------------------------------------------------------- */
  TEST_STEP("End");

  return 0;
}