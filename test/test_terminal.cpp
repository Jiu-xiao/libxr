#include <pthread.h>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_terminal()
{
  static auto ramfs = LibXR::RamFS();
  static LibXR::Terminal terminal(ramfs);
  LibXR::Thread term_thread;
  term_thread.Create(&terminal, terminal.ThreadFun, "terminal", 512,
                     LibXR::Thread::Priority::MEDIUM);
  LibXR::Thread::Sleep(1000);
  printf("\n");
  pthread_cancel(term_thread);
}