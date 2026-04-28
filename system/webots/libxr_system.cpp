#include "libxr_system.hpp"

#include <cerrno>
#include <cmath>
#include <list>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "libxr_type.hpp"
#include "thread.hpp"
#include "timer.hpp"
#include "webots/Robot.hpp"
#include "webots_timebase.hpp"

uint64_t _libxr_webots_time_count = 0;
uint32_t _libxr_webots_poll_period_ms = 1;
webots::Robot* _libxr_webots_robot_handle = nullptr;  // NOLINT
static uint32_t basic_time_step_ms = 1;
static uint64_t step_interval_ns = 1000000ULL;
LibXR::condition_var_handle* _libxr_webots_time_notify = nullptr;

static LibXR::Semaphore stdo_sem;

struct LibXR::WebotsRealtimeThreadRegistration
{
  bool started{false};
  bool parked{false};
  bool parked_on_step_wait{false};
  uint64_t parked_epoch{0};
};

namespace
{
// 注册项地址会绑定到线程 TLS，容器不能在插入/删除其他项时搬动已有元素。
std::list<LibXR::WebotsRealtimeThreadRegistration> g_webots_realtime_threads;
pthread_mutex_t g_webots_realtime_threads_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t g_webots_realtime_threads_cond = PTHREAD_COND_INITIALIZER;
uint64_t g_webots_step_epoch = 0;
thread_local LibXR::WebotsRealtimeThreadRegistration* g_current_realtime_thread = nullptr;

void RemoveWebotsRealtimeThreadUnlocked(LibXR::WebotsRealtimeThreadRegistration* registration)
{
  for (auto it = g_webots_realtime_threads.begin(); it != g_webots_realtime_threads.end();
       ++it)
  {
    if (&(*it) == registration)
    {
      g_webots_realtime_threads.erase(it);
      return;
    }
  }
}

bool AllWebotsRealtimeThreadsParkedUnlocked(
    const LibXR::WebotsRealtimeThreadRegistration* excluded)
{
  for (const auto& registration : g_webots_realtime_threads)
  {
    if (!registration.started)
    {
      continue;
    }

    if (&registration == excluded)
    {
      continue;
    }

    if (!registration.parked)
    {
      return false;
    }

    // Thread::Sleep 会被 Webots step 唤醒，必须等它处理完本 step 后重新挂起。
    // Semaphore/topic 等非 step 阻塞不会被 step 唤醒；线程已经挂起即可推进仿真。
    if (registration.parked_on_step_wait &&
        registration.parked_epoch != g_webots_step_epoch)
    {
      return false;
    }
  }
  return true;
}
}  // namespace

void StdiThread(LibXR::ReadPort* read_port)
{
  static uint8_t read_buff[static_cast<size_t>(4 * LIBXR_PRINTF_BUFFER_SIZE)];

  while (true)
  {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);

    int ret = select(STDIN_FILENO + 1, &rfds, NULL, NULL, NULL);

    if (ret > 0 && FD_ISSET(STDIN_FILENO, &rfds))
    {
      int ready = 0;
      if (ioctl(STDIN_FILENO, FIONREAD, &ready) != -1 && ready > 0)
      {
        auto size = fread(read_buff, sizeof(char), ready, stdin);
        if (size < 1)
        {
          continue;
        }
        read_port->queue_data_->PushBatch(read_buff, size);
        read_port->ProcessPendingReads(false);
      }
    }
  }
}

void StdoThread(LibXR::WritePort* write_port)
{
  LibXR::WriteInfoBlock info;
  static uint8_t write_buff[static_cast<size_t>(4 * LIBXR_PRINTF_BUFFER_SIZE)];

  while (true)
  {
    if (stdo_sem.Wait() == LibXR::ErrorCode::OK)
    {
      auto ans = write_port->queue_info_->Pop(info);
      if (ans != LibXR::ErrorCode::OK)
      {
        continue;
      }

      ans = write_port->queue_data_->PopBatch(write_buff, info.data.size_);
      if (ans != LibXR::ErrorCode::OK)
      {
        continue;
      }

      auto write_size = fwrite(write_buff, sizeof(char), info.data.size_, stdout);
      auto fflush_ans = fflush(stdout);

      UNUSED(write_size);
      UNUSED(fflush_ans);

      write_port->Finish(
          false, write_size == info.data.size_ ? LibXR::ErrorCode::OK : LibXR::ErrorCode::FAILED, info);
    }
  }
}

namespace
{
uint64_t MonotonicNowNanoseconds()
{
  timespec ts = {};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
         static_cast<uint64_t>(ts.tv_nsec);
}

timespec NanosecondsToTimespec(uint64_t nanoseconds)
{
  timespec ts = {};
  ts.tv_sec = static_cast<time_t>(nanoseconds / 1000000000ULL);
  ts.tv_nsec = static_cast<long>(nanoseconds % 1000000000ULL);
  return ts;
}

void SleepUntilNanoseconds(uint64_t deadline_ns)
{
  const timespec ts = NanosecondsToTimespec(deadline_ns);
  while (true)
  {
    const int ans = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr);
    if (ans == 0)
    {
      return;
    }
    if (ans != EINTR)
    {
      return;
    }
  }
}
}  // namespace

void LibXR::PlatformInit(webots::Robot* robot, uint32_t timer_pri,
                         uint32_t timer_stack_depth, double sim_flow_rate)
{
  LibXR::Timer::priority_ = static_cast<LibXR::Thread::Priority>(timer_pri);
  LibXR::Timer::stack_depth_ = timer_stack_depth;
  auto write_fun = [](WritePort& port, bool)
  {
    UNUSED(port);
    stdo_sem.Post();
    return LibXR::ErrorCode::PENDING;
  };

  LibXR::STDIO::write_ =
      new LibXR::WritePort(32, static_cast<size_t>(4 * LIBXR_PRINTF_BUFFER_SIZE));

  *LibXR::STDIO::write_ = write_fun;

  auto read_fun = [](ReadPort& port, bool)
  {
    UNUSED(port);
    return LibXR::ErrorCode::PENDING;
  };

  LibXR::STDIO::read_ =
      new LibXR::ReadPort(static_cast<size_t>(4 * LIBXR_PRINTF_BUFFER_SIZE));

  *LibXR::STDIO::read_ = read_fun;

  struct termios tty;
  tcgetattr(STDIN_FILENO, &tty);           // 获取当前终端属性
  tty.c_lflag &= ~(ICANON | ECHO);         // 禁用规范模式和回显
  tcsetattr(STDIN_FILENO, TCSANOW, &tty);  // 立即生效

  LibXR::Thread stdi_thread, stdo_thread;
  stdi_thread.Create<LibXR::ReadPort*>(LibXR::STDIO::read_, StdiThread, "STDIO.read_",
                                       1024, LibXR::Thread::Priority::MEDIUM);

  stdo_thread.Create<LibXR::WritePort*>(LibXR::STDIO::write_, StdoThread, "STDIO.write_",
                                        1024, LibXR::Thread::Priority::MEDIUM);

  if (robot == nullptr)
  {
    _libxr_webots_robot_handle = new webots::Robot();
  }
  else
  {
    _libxr_webots_robot_handle = robot;
  }

  if (_libxr_webots_robot_handle == nullptr)
  {
    std::fprintf(stderr, "webots robot handle is null.\n");
    std::exit(EXIT_FAILURE);
  }

  const double basic_time_step = _libxr_webots_robot_handle->getBasicTimeStep();
  if (basic_time_step <= 0.0)
  {
    std::fprintf(stderr, "webots basicTimeStep is invalid: %.3f.\n", basic_time_step);
    std::exit(EXIT_FAILURE);
  }

  if (sim_flow_rate <= 0.0)
  {
    std::fprintf(stderr, "webots sim_flow_rate %.6f is invalid, fallback to 1.0.\n",
                 sim_flow_rate);
    sim_flow_rate = 1.0;
  }

  basic_time_step_ms =
      static_cast<uint32_t>(LibXR::max(1LL, std::llround(basic_time_step)));
  _libxr_webots_poll_period_ms = static_cast<uint32_t>(
      LibXR::max(1LL, std::llround(basic_time_step / sim_flow_rate)));
  step_interval_ns = static_cast<uint64_t>(
      LibXR::max(1LL, std::llround(basic_time_step * 1000000.0 / sim_flow_rate)));

  _libxr_webots_time_notify = new condition_var_handle;
  pthread_mutex_init(&_libxr_webots_time_notify->mutex, nullptr);
  pthread_cond_init(&_libxr_webots_time_notify->cond, nullptr);

  auto webots_timebase_thread_fun = [](void*)
  {
    uint64_t next_deadline_ns = MonotonicNowNanoseconds() + step_interval_ns;

    while (true)
    {
      SleepUntilNanoseconds(next_deadline_ns);
      next_deadline_ns += step_interval_ns;

      if (_libxr_webots_robot_handle->step(static_cast<int>(basic_time_step_ms)) < 0)
      {
        std::exit(EXIT_SUCCESS);
      }

      _libxr_webots_time_count += basic_time_step_ms;
      LibXR::WebotsAdvanceStepEpoch();
      pthread_mutex_lock(&_libxr_webots_time_notify->mutex);
      pthread_cond_broadcast(&_libxr_webots_time_notify->cond);
      pthread_mutex_unlock(&_libxr_webots_time_notify->mutex);
      LibXR::WebotsWaitUntilRealtimeThreadsParked();
    }
  };

  LibXR::Thread webots_timebase_thread;
  webots_timebase_thread.Create<void*>(
      reinterpret_cast<void*>(0), webots_timebase_thread_fun, "webots_timebase",
      1024, Thread::Priority::REALTIME);

  static LibXR::WebotsTimebase timebase;
}

LibXR::WebotsRealtimeThreadRegistration* LibXR::WebotsRegisterRealtimeThread()
{
  pthread_mutex_lock(&g_webots_realtime_threads_mutex);
  g_webots_realtime_threads.emplace_back();
  auto* registration = &g_webots_realtime_threads.back();
  pthread_mutex_unlock(&g_webots_realtime_threads_mutex);
  pthread_cond_broadcast(&g_webots_realtime_threads_cond);
  return registration;
}

void LibXR::WebotsBindCurrentRealtimeThread(WebotsRealtimeThreadRegistration* registration)
{
  if (registration == nullptr)
  {
    return;
  }

  pthread_mutex_lock(&g_webots_realtime_threads_mutex);
  registration->started = true;
  registration->parked = false;
  g_current_realtime_thread = registration;
  pthread_mutex_unlock(&g_webots_realtime_threads_mutex);
  pthread_cond_broadcast(&g_webots_realtime_threads_cond);
}

void LibXR::WebotsReleaseRealtimeThread(WebotsRealtimeThreadRegistration* registration)
{
  if (registration == nullptr)
  {
    return;
  }

  pthread_mutex_lock(&g_webots_realtime_threads_mutex);
  if (g_current_realtime_thread == registration)
  {
    g_current_realtime_thread = nullptr;
  }
  RemoveWebotsRealtimeThreadUnlocked(registration);
  pthread_mutex_unlock(&g_webots_realtime_threads_mutex);
  pthread_cond_broadcast(&g_webots_realtime_threads_cond);
}

void LibXR::WebotsMarkCurrentRealtimeThreadRunning()
{
  if (g_current_realtime_thread == nullptr)
  {
    return;
  }

  pthread_mutex_lock(&g_webots_realtime_threads_mutex);
  g_current_realtime_thread->parked = false;
  g_current_realtime_thread->parked_on_step_wait = false;
  pthread_mutex_unlock(&g_webots_realtime_threads_mutex);
}

void LibXR::WebotsMarkCurrentRealtimeThreadParked(bool waiting_for_step)
{
  if (g_current_realtime_thread == nullptr)
  {
    return;
  }

  pthread_mutex_lock(&g_webots_realtime_threads_mutex);
  g_current_realtime_thread->parked = true;
  g_current_realtime_thread->parked_on_step_wait = waiting_for_step;
  g_current_realtime_thread->parked_epoch = g_webots_step_epoch;
  pthread_mutex_unlock(&g_webots_realtime_threads_mutex);
  pthread_cond_broadcast(&g_webots_realtime_threads_cond);
}

void LibXR::WebotsAdvanceStepEpoch()
{
  pthread_mutex_lock(&g_webots_realtime_threads_mutex);
  ++g_webots_step_epoch;
  pthread_mutex_unlock(&g_webots_realtime_threads_mutex);
}

void LibXR::WebotsWaitUntilRealtimeThreadsParked()
{
  pthread_mutex_lock(&g_webots_realtime_threads_mutex);
  while (!AllWebotsRealtimeThreadsParkedUnlocked(g_current_realtime_thread))
  {
    pthread_cond_wait(&g_webots_realtime_threads_cond, &g_webots_realtime_threads_mutex);
  }
  pthread_mutex_unlock(&g_webots_realtime_threads_mutex);
}
