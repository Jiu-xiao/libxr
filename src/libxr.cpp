#include "libxr.hpp"
#include "crc.hpp"
#include "libxr_def.hpp"
#include "libxr_platform.hpp"
#include "libxr_time.hpp"
#include "libxr_type.hpp"
#include "list.hpp"
#include "system/timer.hpp"
#include "thread.hpp"
#include "timer.hpp"

LibXR::Callback<void, const char *, uint32_t>
    LibXR::Assert::libxr_fatal_error_callback;

LibXR::ReadFunction LibXR::STDIO::read;
LibXR::WriteFunction LibXR::STDIO::write;
LibXR::List<LibXR::Timer::ControlBlock>
    LibXR::Timer::list_[LibXR::Thread::PRIORITY_NUMBER];
uint8_t LibXR::CRC8::tab[256];
bool LibXR::CRC8::inited = false;
uint16_t LibXR::CRC16::tab[256];
bool LibXR::CRC16::inited = false;
uint32_t LibXR::CRC32::tab[256];
bool LibXR::CRC32::inited = false;

void LibXR::LibXR_Init() { PlatformInit(); }
