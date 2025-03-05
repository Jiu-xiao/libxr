#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_condition_var() {
  LibXR::ConditionVar cv;
  static LibXR::Semaphore sem(0);

  void (*func1)(LibXR::ConditionVar *) = [](LibXR::ConditionVar *cv) {
    ASSERT(cv->Wait(100) == ErrorCode::OK);
    sem.Post();
    return;
  };

  void (*func2)(LibXR::ConditionVar *) = [](LibXR::ConditionVar *cv) {
    ASSERT(cv->Wait(100) == ErrorCode::OK);
    sem.Post();
    return;
  };

  LibXR::Thread thread1, thread2;
  thread1.Create(&cv, func1, "cv_thread1", 512,
                 LibXR::Thread::Priority::REALTIME);

  thread2.Create(&cv, func2, "cv_thread2", 512,
                 LibXR::Thread::Priority::REALTIME);

  LibXR::Thread::Sleep(80);
  cv.Broadcast();
  ASSERT(sem.Wait(20) == ErrorCode::OK);
  ASSERT(sem.Wait(20) == ErrorCode::OK);

  pthread_join(thread1, nullptr);
  pthread_join(thread2, nullptr);
}
