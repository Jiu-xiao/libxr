#include "libxr.hpp"

#include "thread.hpp"

void libxr_fatal_error(const char *file, uint32_t line, bool in_isr)
{
  volatile bool stop = false;
  while (!stop)
  {
    if (LibXR::STDIO::write_ && LibXR::STDIO::write_->Writable())
    {
      printf("Fatal error at %s:%d\r\n", file, static_cast<int>(line));
    }

    if (LibXR::Assert::libxr_fatal_error_callback_)
    {
      LibXR::Assert::libxr_fatal_error_callback_->Run(in_isr, file, line);
    }

    LibXR::Thread::Sleep(500);
  }
}
