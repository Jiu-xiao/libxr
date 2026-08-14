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
    if (LibXR::STDIO::read_ != nullptr && LibXR::STDIO::read_->Readable())
    {
      const size_t input_size = strlen(js_input);
      if (input_size == 0U)
      {
        return;
      }
      const auto push_ans = LibXR::STDIO::read_->queue_data_->PushBatch(
          reinterpret_cast<const uint8_t*>(js_input), input_size);
      if (push_ans == LibXR::ErrorCode::OK)
      {
        LibXR::STDIO::read_->ProcessPendingReads(false);
      }
    }
  }
}

static constexpr size_t webasm_stdio_queue_bytes = 4096;

void LibXR::PlatformInit()
{
  static LibXR::WebAsmTimebase libxr_webasm_timebase;

  auto write_fun = [](WritePort& port, bool in_isr)
  {
    static uint8_t write_buff[webasm_stdio_queue_bytes];
    WriteInfoBlock info;
    auto dequeue = port.BeginDequeue(in_isr);
    if (dequeue.PopInfo(info) != LibXR::ErrorCode::OK)
    {
      return LibXR::ErrorCode::EMPTY;
    }

    auto pop_ans = dequeue.PopData(write_buff, info.data.size_);
    if (pop_ans != LibXR::ErrorCode::OK)
    {
      return pop_ans;
    }

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

    return LibXR::ErrorCode::OK;
  };

  LibXR::STDIO::write_ = new LibXR::WritePort(32, webasm_stdio_queue_bytes);

  *LibXR::STDIO::write_ = write_fun;

  LibXR::STDIO::read_ = new LibXR::ReadPort(webasm_stdio_queue_bytes);
}

void LibXR::Timer::RefreshTimerInIdle()
{
  static bool in_timer = false;
  if (in_timer)
  {
    return;
  }

  static auto last_refresh_time = Timebase::GetMilliseconds();

  while (static_cast<uint32_t>(Timebase::GetMilliseconds() - last_refresh_time) > 0u)
  {
    in_timer = true;
    last_refresh_time = (last_refresh_time + 1);
    Timer::Refresh();
    in_timer = false;
  }
}
