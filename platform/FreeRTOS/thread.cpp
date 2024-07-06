#include "thread.hpp"

#include "libxr_platform.hpp"

using namespace LibXR;

Thread Thread::Current(void) { return Thread(xTaskGetCurrentTaskHandle()); }

void Thread::Sleep(uint32_t milliseconds) { vTaskDelay(milliseconds); }

void Thread::SleepUntil(TimestampMS &last_waskup_time, uint32_t time_to_sleep) {
  vTaskDelayUntil(reinterpret_cast<uint32_t *>(&last_waskup_time),
                  time_to_sleep);
}

uint32_t Thread::GetTime() { return xTaskGetTickCount(); }

void Thread::Yield() { portYIELD(); }
