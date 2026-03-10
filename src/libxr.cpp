#include "libxr.hpp"

#include "thread.hpp"

extern "C" void libxr_fatal_error(const char* file, uint32_t line, bool in_isr)
{
  volatile bool stop = false;
  while (!stop)
  {
    if (in_isr)
    {
      *(volatile int*)0 = 0;  // NOLINT
    }
    else
    {
      if (LibXR::STDIO::write_ && LibXR::STDIO::write_->Writable())
      {
        LibXR::STDIO::Printf("Fatal error at %s:%d\r\n", file, static_cast<int>(line));
      }

      if (!LibXR::Assert::libxr_fatal_error_callback_.Empty())
      {
        LibXR::Assert::libxr_fatal_error_callback_.Run(in_isr, file, line);
      }

      LibXR::Thread::Sleep(500);
    }
  }
}
