#include "libxr.hpp"
#include "crc.hpp"
#include "event.hpp"
#include "libxr_def.hpp"
#include "libxr_platform.hpp"
#include "libxr_time.hpp"
#include "libxr_type.hpp"
#include "list.hpp"
#include "messgae.hpp"
#include "mutex.hpp"
#include "thread.hpp"
#include "timer.hpp"
#include <cstddef>

const LibXR::Callback<const char *, uint32_t>
    *LibXR::Assert::libxr_fatal_error_callback;

LibXR::ReadPort LibXR::STDIO::read = NULL;
LibXR::WritePort LibXR::STDIO::write = NULL;
void (*LibXR::STDIO::error)(const char *log) = NULL;
LibXR::List *LibXR::Timer::list_[LibXR::Thread::PRIORITY_NUMBER];
uint8_t LibXR::CRC8::tab[256];
bool LibXR::CRC8::inited = false;
uint16_t LibXR::CRC16::tab[256];
bool LibXR::CRC16::inited = false;
uint32_t LibXR::CRC32::tab[256];
bool LibXR::CRC32::inited = false;

LibXR::RBTree<uint32_t> *LibXR::Topic::domain_;
LibXR::SpinLock LibXR::Topic::domain_lock_;

LibXR::Topic::Domain *LibXR::Topic::def_domain_;

void LibXR::LibXR_Init() { PlatformInit(); }
