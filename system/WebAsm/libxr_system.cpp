#include "libxr_system.hpp"

#include <emscripten.h>

#include "libxr_assert.hpp"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "libxr_type.hpp"
#include "list.hpp"
#include "queue.hpp"
#include "semaphore.hpp"
#include "thread.hpp"
#include "timebase.hpp"
#include "timer.hpp"
#include "webasm_timebase.hpp"

extern "C"
{
  // JS 会调用它，传字符串进来
  void receive_input(const char* js_input)
  {
    if (LibXR::STDIO::read_ && LibXR::STDIO::read_->Readable())
    {
      LibXR::STDIO::read_->queue_data_->PushBatch(
          reinterpret_cast<const uint8_t*>(js_input), strlen(js_input));
      LibXR::STDIO::read_->ProcessPendingReads(false);
    }
  }
}

void LibXR::PlatformInit()
{
  static LibXR::WebAsmTimebase libxr_webasm_timebase;

  auto write_fun = [](WritePort& port, bool)
  {
    static uint8_t write_buff[1024];
    WriteInfoBlock info;
    while (true)
    {
      if (port.queue_info_->Pop(info) != ErrorCode::OK)
      {
        return ErrorCode::OK;
      }

      port.queue_data_->PopBatch(write_buff, info.data.size_);
      EM_ASM(
          {
            var ptr = $0;
            var len = $1;
            for (var i = 0; i < len; i++)
            {
              Module.put_char(String.fromCharCode(HEAPU8[ptr + i]));
            }
          },
          reinterpret_cast<uintptr_t>(write_buff), info.data.size_);

      port.queue_info_->Pop(info);

      port.Finish(false, ErrorCode::OK, info);
    }

    return ErrorCode::OK;
  };

  LibXR::STDIO::write_ =
      new LibXR::WritePort(32, static_cast<size_t>(4 * LIBXR_PRINTF_BUFFER_SIZE));

  *LibXR::STDIO::write_ = write_fun;

  auto read_fun = [](ReadPort&, bool) { return ErrorCode::EMPTY; };

  LibXR::STDIO::read_ =
      new LibXR::ReadPort(static_cast<size_t>(4 * LIBXR_PRINTF_BUFFER_SIZE));

  *LibXR::STDIO::read_ = read_fun;
}

void LibXR::Timer::RefreshTimerInIdle()
{
  static bool in_timer = false;
  if (in_timer)
  {
    return;
  }

  static auto last_refresh_time = Timebase::GetMilliseconds();

  while (true)
  {
    if (last_refresh_time >= Timebase::GetMilliseconds())
    {
      return;
    }

    in_timer = true;
    last_refresh_time = (last_refresh_time + 1);
    Timer::Refresh();
    in_timer = false;
  }
}
