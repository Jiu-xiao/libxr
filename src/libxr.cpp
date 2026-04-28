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
        LibXR::STDIO::Print<"Fatal error at {}:{}\r\n">(file, static_cast<int>(line));
      }

      if (!LibXR::Assert::libxr_fatal_error_callback_.Empty())
      {
        // The fatal callback is executed only on the non-ISR fatal path here.
        // Normalize the user callback to thread context instead of forwarding the
        // original fault source flag.
        LibXR::Assert::libxr_fatal_error_callback_.Run(false, file, line);
      }

      LibXR::Thread::Sleep(500);
    }
  }
}
