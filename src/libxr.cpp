#include "libxr.hpp"

#include "crc.hpp"
#include "event.hpp"
#include "libxr_def.hpp"
#include "libxr_system.hpp"
#include "libxr_time.hpp"
#include "libxr_type.hpp"
#include "list.hpp"
#include "message.hpp"
#include "mutex.hpp"
#include "ramfs.hpp"
#include "thread.hpp"
#include "timebase.hpp"
#include "timer.hpp"

/* error callback */
std::optional<LibXR::Callback<const char *, uint32_t>>
    LibXR::Assert::libxr_fatal_error_callback;

/* stdio */
LibXR::ReadPort *LibXR::STDIO::read = nullptr;
LibXR::WritePort *LibXR::STDIO::write = nullptr;

/* timer */
LibXR::List *LibXR::Timer::list_;

/* crc */
uint8_t LibXR::CRC8::tab[256];
bool LibXR::CRC8::inited = false;
uint16_t LibXR::CRC16::tab[256];
bool LibXR::CRC16::inited = false;
uint32_t LibXR::CRC32::tab[256];
bool LibXR::CRC32::inited = false;

/* topic */
LibXR::RBTree<uint32_t> *LibXR::Topic::domain_;
LibXR::SpinLock LibXR::Topic::domain_lock_;
LibXR::Topic::Domain *LibXR::Topic::def_domain_;

/* timebase */
LibXR::Timebase *LibXR::Timebase::timebase = nullptr;

void _LibXR_FatalError(const char *file, uint32_t line, bool in_isr) {
  volatile bool stop = false;
  while (!stop) {
    if (LibXR::STDIO::write && LibXR::STDIO::write->Writable()) {
      printf("Fatal error at %s:%d\r\n", file, int(line));
    }

    if (LibXR::Assert::libxr_fatal_error_callback) {
      LibXR::Assert::libxr_fatal_error_callback->Run(in_isr, file, line);
    }
  }
}
