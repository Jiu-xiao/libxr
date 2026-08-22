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
      auto queue = LibXR::STDIO::read_->GetReadQueue();
      const auto push_ans =
          queue.PushBatch(reinterpret_cast<const uint8_t*>(js_input), input_size);
      if (push_ans == LibXR::ErrorCode::OK)
      {
        queue.Publish();
      }
    }
  }
}

static constexpr size_t webasm_stdio_queue_bytes = 4096;

namespace
{
// system/webasm is single-threaded; this owner only blocks synchronous completion
// reentry until the current front-plus-next scope settles.
bool stdo_owner_active = false;
bool stdo_async_pending = false;

void NotifyStdoWorker(LibXR::WritePort& port);
void ScheduleStdoWorker(LibXR::WritePort& port);

void ServiceStdoTurn(LibXR::WritePort& port)
{
  ASSERT(!stdo_owner_active);
  stdo_owner_active = true;

  {
    auto queue = port.GetWriteQueue();
    const size_t offered = queue.front_size + queue.next_size;
    if (offered != 0U)
    {
      (void)queue.PopWithWriter(
          offered,
          [](const uint8_t* first, size_t first_size, const uint8_t* second,
             size_t second_size) -> size_t
          {
            auto output = [](const uint8_t* data, size_t size)
            {
              if (size == 0U)
              {
                return;
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
                  reinterpret_cast<uintptr_t>(data), size);
            };

            output(first, first_size);
            output(second, second_size);
            return first_size + second_size;
          });
    }
  }

  bool remaining = false;
  {
    auto queue = port.GetWriteQueue();
    remaining = queue.front_size != 0U;
  }
  stdo_owner_active = false;
  if (remaining)
  {
    ScheduleStdoWorker(port);
  }
}

void RunScheduledStdo(void* arg)
{
  stdo_async_pending = false;
  ServiceStdoTurn(*static_cast<LibXR::WritePort*>(arg));
}

void ScheduleStdoWorker(LibXR::WritePort& port)
{
  if (!stdo_async_pending)
  {
    stdo_async_pending = true;
    emscripten_async_call(RunScheduledStdo, &port, 0);
  }
}

void NotifyStdoWorker(LibXR::WritePort& port)
{
  if (stdo_owner_active)
  {
    ScheduleStdoWorker(port);
    return;
  }
  ServiceStdoTurn(port);
}
}  // namespace

void LibXR::PlatformInit()
{
  static LibXR::WebAsmTimebase libxr_webasm_timebase;

  auto write_fun = [](WritePort& port, bool) { NotifyStdoWorker(port); };

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
