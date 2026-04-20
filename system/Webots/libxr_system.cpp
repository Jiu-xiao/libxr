#include "libxr_system.hpp"

#include <cerrno>
#include <cmath>
#include <poll.h>
#include <signal.h>
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
static float time_step = 0.0f;
static uint64_t step_interval_ns = 1000000ULL;
LibXR::condition_var_handle* _libxr_webots_time_notify = nullptr;

static LibXR::Semaphore stdo_sem;

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
    if (stdo_sem.Wait() == ErrorCode::OK)
    {
      auto ans = write_port->queue_info_->Pop(info);
      if (ans != ErrorCode::OK)
      {
        continue;
      }

      ans = write_port->queue_data_->PopBatch(write_buff, info.data.size_);
      if (ans != ErrorCode::OK)
      {
        continue;
      }

      auto write_size = fwrite(write_buff, sizeof(char), info.data.size_, stdout);
      auto fflush_ans = fflush(stdout);

      UNUSED(write_size);
      UNUSED(fflush_ans);

      write_port->Finish(
          false, write_size == info.data.size_ ? ErrorCode::OK : ErrorCode::FAILED, info);
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

timespec MonotonicDeadline(uint64_t deadline_ns)
{
  timespec ts = {};
  ts.tv_sec = static_cast<time_t>(deadline_ns / 1000000000ULL);
  ts.tv_nsec = static_cast<long>(deadline_ns % 1000000000ULL);
  return ts;
}

void SleepUntilNanoseconds(uint64_t deadline_ns)
{
  const timespec ts = MonotonicDeadline(deadline_ns);
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

void PlatformInitImpl(webots::Robot* robot, uint32_t timer_pri, uint32_t timer_stack_depth,
                      double sim_flow_rate)
{
  LibXR::Timer::priority_ = static_cast<LibXR::Thread::Priority>(timer_pri);
  LibXR::Timer::stack_depth_ = timer_stack_depth;

  auto write_fun = [](LibXR::WritePort& port, bool)
  {
    UNUSED(port);
    stdo_sem.Post();
    return ErrorCode::PENDING;
  };

  LibXR::STDIO::write_ =
      new LibXR::WritePort(32, static_cast<size_t>(4 * LIBXR_PRINTF_BUFFER_SIZE));
  *LibXR::STDIO::write_ = write_fun;

  auto read_fun = [](LibXR::ReadPort& port, bool)
  {
    UNUSED(port);
    return ErrorCode::PENDING;
  };

  LibXR::STDIO::read_ =
      new LibXR::ReadPort(static_cast<size_t>(4 * LIBXR_PRINTF_BUFFER_SIZE));
  *LibXR::STDIO::read_ = read_fun;

  struct termios tty;
  tcgetattr(STDIN_FILENO, &tty);
  tty.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &tty);

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
    printf("webots robot handle is null.\n");
    exit(-1);
  }

  time_step = _libxr_webots_robot_handle->getBasicTimeStep();
  if (time_step <= 0.0f)
  {
    printf("webots basicTimeStep is invalid: %.3f.\n", time_step);
    exit(-1);
  }

  if (sim_flow_rate <= 0.0)
  {
    printf("webots sim_flow_rate %.6f is invalid, fallback to 1.0.\n", sim_flow_rate);
    sim_flow_rate = 1.0;
  }

  _libxr_webots_poll_period_ms =
      LibXR::max(1, static_cast<int>(std::lround(static_cast<double>(time_step) /
                                                 sim_flow_rate)));
  step_interval_ns =
      LibXR::max<uint64_t>(1ULL, static_cast<uint64_t>(std::llround(
                                     static_cast<double>(time_step) * 1000000.0 /
                                     sim_flow_rate)));

  _libxr_webots_time_notify = new LibXR::condition_var_handle;
  pthread_mutex_init(&_libxr_webots_time_notify->mutex, nullptr);
  pthread_cond_init(&_libxr_webots_time_notify->cond, nullptr);

  auto webots_timebase_thread_fun = [](void*)
  {
    poll(nullptr, 0, static_cast<int>(_libxr_webots_poll_period_ms));
    uint64_t next_deadline_ns = MonotonicNowNanoseconds() + step_interval_ns;

    while (true)
    {
      SleepUntilNanoseconds(next_deadline_ns);
      next_deadline_ns += step_interval_ns;
      const int step_ret = _libxr_webots_robot_handle->step(time_step);
      if (step_ret < 0)
      {
        std::fprintf(stderr, "Webots step returned %d, terminating controller.\n",
                     step_ret);
        std::fflush(stderr);
        ::kill(::getpid(), SIGTERM);
        std::_Exit(EXIT_SUCCESS);
      }

      _libxr_webots_time_count += static_cast<uint64_t>(std::lround(time_step));
      pthread_mutex_lock(&_libxr_webots_time_notify->mutex);
      pthread_cond_broadcast(&_libxr_webots_time_notify->cond);
      pthread_mutex_unlock(&_libxr_webots_time_notify->mutex);
    }
  };

  LibXR::Thread webots_timebase_thread;
  webots_timebase_thread.Create<void*>(
      reinterpret_cast<void*>(0), webots_timebase_thread_fun, "webots_timebase_thread",
      1024, LibXR::Thread::Priority::REALTIME);

  static LibXR::WebotsTimebase timebase;

  printf("Webots PlatformInit: basicTimeStep=%.3f ms sim_flow_rate=%.3f poll=%u ms\n",
         time_step, sim_flow_rate, _libxr_webots_poll_period_ms);
}
}  // namespace

void LibXR::PlatformInit(webots::Robot* robot, uint32_t timer_pri,
                         uint32_t timer_stack_depth)
{
  PlatformInitImpl(robot, timer_pri, timer_stack_depth, 1.0);
}

void LibXR::PlatformInit(webots::Robot* robot, double sim_flow_rate)
{
  PlatformInitImpl(robot, 2, 65536, sim_flow_rate);
}
