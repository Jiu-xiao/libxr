#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_signal() {
  LibXR::Thread thread;
  static volatile bool signal_received = false;

  thread.Create<void*>(
      static_cast<void*>(NULL),
      [](void*) {
        LibXR::Signal::Wait(5);
        signal_received = true;
      },
      "signal_thread", 512, LibXR::Thread::Priority::REALTIME);

  LibXR::Thread::Sleep(50);
  LibXR::Signal::Action(thread, 5);
  LibXR::Thread::Sleep(50);

  ASSERT(signal_received);
}