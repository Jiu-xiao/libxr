#include "libxr.hpp"

#include "crc.hpp"
#include "libxr_def.hpp"
#include "list.hpp"
#include "message.hpp"
#include "timebase.hpp"
#include "timer.hpp"

/* error callback */
std::optional<LibXR::Callback<const char *, uint32_t>>
    LibXR::Assert::libxr_fatal_error_callback_;

/* stdio */
LibXR::ReadPort *LibXR::STDIO::read_ = nullptr;
LibXR::WritePort *LibXR::STDIO::write_ = nullptr;
char LibXR::STDIO::printf_buff_[LIBXR_PRINTF_BUFFER_SIZE];

/* timer */
LibXR::List *LibXR::Timer::list_;
LibXR::Thread LibXR::Timer::thread_handle_;

/* crc */
uint8_t LibXR::CRC8::tab_[256];
bool LibXR::CRC8::inited_ = false;
uint16_t LibXR::CRC16::tab_[256];
bool LibXR::CRC16::inited_ = false;
uint32_t LibXR::CRC32::tab_[256];
bool LibXR::CRC32::inited_ = false;

/* topic */
LibXR::RBTree<uint32_t> *LibXR::Topic::domain_;
LibXR::SpinLock LibXR::Topic::domain_lock_;
LibXR::Topic::Domain *LibXR::Topic::def_domain_;

/* timebase */
LibXR::Timebase *LibXR::Timebase::timebase = nullptr;

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
  }
}
