#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_terminal() {
  auto ramfs = LibXR::RamFS();
  LibXR::Terminal terminal(ramfs);
  LibXR::Thread term_thread;
  term_thread.Create(&terminal, terminal.ThreadFun, "terminal", 512,
                     LibXR::Thread::Priority::MEDIUM);
  LibXR::Thread::Sleep(10000);
  printf("\n");
}