#include "linux_gpio.hpp"

#include <fcntl.h>
#include <linux/gpio.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>

#include "logger.hpp"

namespace
{
constexpr const char* kLinuxGPIOConsumer = "LinuxGPIO";

LibXR::GPIO::Configuration ResolveRequestConfig(LibXR::GPIO::Configuration desired,
                                                bool interrupt_enabled)
{
  if (!interrupt_enabled &&
      (desired.direction == LibXR::GPIO::Direction::RISING_INTERRUPT ||
       desired.direction == LibXR::GPIO::Direction::FALL_INTERRUPT ||
       desired.direction == LibXR::GPIO::Direction::FALL_RISING_INTERRUPT))
  {
    desired.direction = LibXR::GPIO::Direction::INPUT;
  }

  return desired;
}

template <size_t N>
void CopyConsumer(char (&dst)[N], const char* src)
{
  std::strncpy(dst, src, N - 1U);
  dst[N - 1U] = '\0';
}

uint64_t BuildLineFlagsV2(LibXR::GPIO::Configuration config)
{
  uint64_t flags = 0U;

  switch (config.direction)
  {
    case LibXR::GPIO::Direction::INPUT:
      flags |= GPIO_V2_LINE_FLAG_INPUT;
      break;
    case LibXR::GPIO::Direction::OUTPUT_PUSH_PULL:
      flags |= GPIO_V2_LINE_FLAG_OUTPUT;
      break;
    case LibXR::GPIO::Direction::OUTPUT_OPEN_DRAIN:
      flags |= GPIO_V2_LINE_FLAG_OUTPUT | GPIO_V2_LINE_FLAG_OPEN_DRAIN;
      break;
    case LibXR::GPIO::Direction::RISING_INTERRUPT:
      flags |= GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_EDGE_RISING;
      break;
    case LibXR::GPIO::Direction::FALL_INTERRUPT:
      flags |= GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_EDGE_FALLING;
      break;
    case LibXR::GPIO::Direction::FALL_RISING_INTERRUPT:
      flags |= GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_EDGE_RISING |
               GPIO_V2_LINE_FLAG_EDGE_FALLING;
      break;
  }

  switch (config.pull)
  {
    case LibXR::GPIO::Pull::NONE:
      flags |= GPIO_V2_LINE_FLAG_BIAS_DISABLED;
      break;
    case LibXR::GPIO::Pull::UP:
      flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_UP;
      break;
    case LibXR::GPIO::Pull::DOWN:
      flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN;
      break;
  }

  return flags;
}

uint32_t BuildHandleFlagsV1(LibXR::GPIO::Configuration config)
{
  uint32_t flags = 0U;

  switch (config.direction)
  {
    case LibXR::GPIO::Direction::INPUT:
    case LibXR::GPIO::Direction::RISING_INTERRUPT:
    case LibXR::GPIO::Direction::FALL_INTERRUPT:
    case LibXR::GPIO::Direction::FALL_RISING_INTERRUPT:
      flags |= GPIOHANDLE_REQUEST_INPUT;
      break;
    case LibXR::GPIO::Direction::OUTPUT_PUSH_PULL:
      flags |= GPIOHANDLE_REQUEST_OUTPUT;
      break;
    case LibXR::GPIO::Direction::OUTPUT_OPEN_DRAIN:
      flags |= GPIOHANDLE_REQUEST_OUTPUT | GPIOHANDLE_REQUEST_OPEN_DRAIN;
      break;
  }

  switch (config.pull)
  {
    case LibXR::GPIO::Pull::NONE:
      flags |= GPIOHANDLE_REQUEST_BIAS_DISABLE;
      break;
    case LibXR::GPIO::Pull::UP:
      flags |= GPIOHANDLE_REQUEST_BIAS_PULL_UP;
      break;
    case LibXR::GPIO::Pull::DOWN:
      flags |= GPIOHANDLE_REQUEST_BIAS_PULL_DOWN;
      break;
  }

  return flags;
}

uint32_t BuildEventFlagsV1(LibXR::GPIO::Direction direction)
{
  switch (direction)
  {
    case LibXR::GPIO::Direction::RISING_INTERRUPT:
      return GPIOEVENT_REQUEST_RISING_EDGE;
    case LibXR::GPIO::Direction::FALL_INTERRUPT:
      return GPIOEVENT_REQUEST_FALLING_EDGE;
    case LibXR::GPIO::Direction::FALL_RISING_INTERRUPT:
      return GPIOEVENT_REQUEST_BOTH_EDGES;
    default:
      return 0U;
  }
}

int SetNonBlocking(int fd)
{
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0)
  {
    return -1;
  }

  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int PollReadable(int fd, int timeout_ms)
{
  pollfd pfd = {
      .fd = fd,
      .events = POLLIN,
      .revents = 0,
  };

  while (true)
  {
    const int ret = poll(&pfd, 1, timeout_ms);
    if ((ret < 0) && (errno == EINTR))
    {
      continue;
    }
    return ret;
  }
}

bool IsKnownGPIOEvent(uint32_t event_id)
{
  if (event_id == GPIO_V2_LINE_EVENT_RISING_EDGE ||
      event_id == GPIOEVENT_EVENT_RISING_EDGE)
  {
    return true;
  }

  if (event_id == GPIO_V2_LINE_EVENT_FALLING_EDGE ||
      event_id == GPIOEVENT_EVENT_FALLING_EDGE)
  {
    return true;
  }

  XR_LOG_WARN("Unknown GPIO event id: %u", event_id);
  return false;
}

}  // namespace

namespace LibXR
{

LinuxGPIO::LinuxGPIO(const std::string& chip_path, unsigned int line_offset)
    : chip_path_(chip_path), line_offset_(line_offset)
{
  UNUSED(OpenChip());
}

LinuxGPIO::~LinuxGPIO()
{
  CloseRequest();
  CloseChip();
}

bool LinuxGPIO::Read()
{
  if (EnsureConfigured() != ErrorCode::OK)
  {
    return false;
  }

  const int request_fd = request_fd_.load();
  if (abi_version_.load() == AbiVersion::V2)
  {
    gpio_v2_line_values values = {};
    values.mask = 1ULL;
    if (ioctl(request_fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &values) < 0)
    {
      XR_LOG_ERROR("Failed to read GPIO value: %s", std::strerror(errno));
      return false;
    }
    return (values.bits & 1ULL) != 0U;
  }

  gpiohandle_data values = {};
  if (ioctl(request_fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &values) < 0)
  {
    XR_LOG_ERROR("Failed to read GPIO value: %s", std::strerror(errno));
    return false;
  }

  return values.values[0] != 0U;
}

void LinuxGPIO::Write(bool value)
{
  if (EnsureConfigured() != ErrorCode::OK)
  {
    return;
  }

  const int request_fd = request_fd_.load();
  if (abi_version_.load() == AbiVersion::V2)
  {
    gpio_v2_line_values values = {};
    values.mask = 1ULL;
    values.bits = value ? 1ULL : 0ULL;
    if (ioctl(request_fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &values) < 0)
    {
      XR_LOG_WARN("Failed to write GPIO value: %s", std::strerror(errno));
    }
    return;
  }

  gpiohandle_data values = {};
  values.values[0] = value ? 1U : 0U;
  if (ioctl(request_fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &values) < 0)
  {
    XR_LOG_WARN("Failed to write GPIO value: %s", std::strerror(errno));
  }
}

ErrorCode LinuxGPIO::EnableInterrupt()
{
  if (EnsureConfigured() != ErrorCode::OK)
  {
    return ErrorCode::STATE_ERR;
  }

  if (!IsInterruptDirection(current_config_.direction))
  {
    return ErrorCode::ARG_ERR;
  }

  if (interrupt_enabled_.load())
  {
    return ErrorCode::OK;
  }

  ErrorCode ec = ErrorCode::OK;
  if (NeedsRequestReopen(current_config_))
  {
    ec = ReopenRequest(current_config_);
  }
  else if (abi_version_.load() == AbiVersion::V2)
  {
    ec = ReconfigureRequestV2(current_config_);
  }
  else
  {
    ec = ReconfigureRequestV1(current_config_);
  }
  if (ec != ErrorCode::OK)
  {
    return ec;
  }

  has_config_ = true;
  interrupt_enabled_.store(true);
  StartInterruptThread();
  return ErrorCode::OK;
}

ErrorCode LinuxGPIO::DisableInterrupt()
{
  if (EnsureConfigured() != ErrorCode::OK)
  {
    return ErrorCode::STATE_ERR;
  }

  if (!IsInterruptDirection(current_config_.direction))
  {
    return ErrorCode::ARG_ERR;
  }

  if (!interrupt_enabled_.load())
  {
    return ErrorCode::OK;
  }

  const Configuration inactive_config = ResolveRequestConfig(current_config_, false);
  ErrorCode ec = ErrorCode::OK;
  if (NeedsRequestReopen(inactive_config))
  {
    ec = ReopenRequest(inactive_config);
  }
  else if (abi_version_.load() == AbiVersion::V2)
  {
    ec = ReconfigureRequestV2(inactive_config);
  }
  else
  {
    ec = ReconfigureRequestV1(inactive_config);
  }
  if (ec != ErrorCode::OK)
  {
    return ec;
  }

  has_config_ = true;
  interrupt_enabled_.store(false);
  return ErrorCode::OK;
}

ErrorCode LinuxGPIO::SetConfig(Configuration config)
{
  const ErrorCode chip_ready = OpenChip();
  if (chip_ready != ErrorCode::OK)
  {
    return chip_ready;
  }

  const bool keep_interrupt_enabled =
      interrupt_enabled_.load() && IsInterruptDirection(config.direction);
  const Configuration request_config =
      ResolveRequestConfig(config, keep_interrupt_enabled);

  ErrorCode ec = ErrorCode::OK;
  if (request_fd_.load() < 0)
  {
    ec = ReopenRequest(request_config);
  }
  else if (NeedsRequestReopen(request_config))
  {
    ec = ReopenRequest(request_config);
  }
  else if (abi_version_.load() == AbiVersion::V2)
  {
    ec = ReconfigureRequestV2(request_config);
  }
  else
  {
    ec = ReconfigureRequestV1(request_config);
  }

  if (ec != ErrorCode::OK)
  {
    return ec;
  }

  current_config_ = config;
  has_config_ = true;
  interrupt_enabled_.store(keep_interrupt_enabled);
  if (keep_interrupt_enabled)
  {
    StartInterruptThread();
  }
  return ErrorCode::OK;
}

void LinuxGPIO::StartInterruptThread()
{
  if (interrupt_thread_started_.exchange(true))
  {
    return;
  }

  interrupt_thread_.Create<LinuxGPIO*>(
      this, [](LinuxGPIO* self) { self->InterruptLoop(); }, "irq_gpio",
      INTERRUPT_THREAD_STACK_SIZE, Thread::Priority::MEDIUM);
}

void LinuxGPIO::InterruptLoop()
{
  while (true)
  {
    if (!interrupt_enabled_.load())
    {
      Thread::Sleep(1);
      continue;
    }

    if (request_kind_.load() != RequestKind::EVENT)
    {
      Thread::Sleep(1);
      continue;
    }

    const int fd = request_fd_.load();
    if (fd < 0)
    {
      Thread::Sleep(1);
      continue;
    }

    size_t event_count = 0;
    const ErrorCode pump = PumpEventQueue(fd, abi_version_.load(), event_count, 100);
    if (pump == ErrorCode::OK)
    {
      for (size_t i = 0; i < event_count; ++i)
      {
        callback_.Run(false);
      }
      continue;
    }

    if (pump != ErrorCode::EMPTY)
    {
      Thread::Sleep(1);
    }
  }
}

ErrorCode LinuxGPIO::OpenChip()
{
  if (chip_fd_ >= 0)
  {
    return ErrorCode::OK;
  }

  chip_fd_ = open(chip_path_.c_str(), O_RDONLY | O_CLOEXEC);
  if (chip_fd_ < 0)
  {
    XR_LOG_ERROR("Failed to open GPIO chip: %s (%s)", chip_path_.c_str(),
                 std::strerror(errno));
    ASSERT(false);
    return ErrorCode::INIT_ERR;
  }

  return DetectAbiVersion();
}

void LinuxGPIO::CloseChip()
{
  if (chip_fd_ >= 0)
  {
    close(chip_fd_);
    chip_fd_ = -1;
  }
  abi_version_.store(AbiVersion::UNKNOWN);
}

void LinuxGPIO::CloseRequest()
{
  const int request_fd = request_fd_.exchange(-1);
  if (request_fd >= 0)
  {
    close(request_fd);
  }

  request_kind_.store(RequestKind::NONE);
  interrupt_enabled_.store(false);
  has_config_ = false;
}

ErrorCode LinuxGPIO::DetectAbiVersion()
{
  if (abi_version_.load() != AbiVersion::UNKNOWN)
  {
    return ErrorCode::OK;
  }

  gpiochip_info chip_info = {};
  if (ioctl(chip_fd_, GPIO_GET_CHIPINFO_IOCTL, &chip_info) < 0)
  {
    XR_LOG_ERROR("Failed to read GPIO chip info: %s", std::strerror(errno));
    return ErrorCode::FAILED;
  }

  if (chip_info.lines == 0U)
  {
    XR_LOG_ERROR("GPIO chip exposes zero lines");
    return ErrorCode::FAILED;
  }

  gpio_v2_line_info info = {};
  info.offset = (line_offset_ < chip_info.lines) ? line_offset_ : 0U;
  if (ioctl(chip_fd_, GPIO_V2_GET_LINEINFO_IOCTL, &info) == 0)
  {
    abi_version_.store(AbiVersion::V2);
    return ErrorCode::OK;
  }

  if ((errno == ENOTTY) || (errno == EINVAL)
#ifdef EOPNOTSUPP
      || (errno == EOPNOTSUPP)
#endif
  )
  {
    abi_version_.store(AbiVersion::V1);
    return ErrorCode::OK;
  }

  XR_LOG_ERROR("Failed to probe GPIO chardev ABI: %s", std::strerror(errno));
  return ErrorCode::FAILED;
}

ErrorCode LinuxGPIO::ReopenRequest(Configuration config)
{
  CloseRequest();

  if (abi_version_.load() == AbiVersion::V2)
  {
    return OpenRequestV2(config);
  }

  return OpenRequestV1(config);
}

ErrorCode LinuxGPIO::OpenRequestV2(Configuration config)
{
  gpio_v2_line_request request = {};
  request.offsets[0] = line_offset_;
  request.num_lines = 1U;
  request.event_buffer_size =
      IsInterruptDirection(config.direction) ? EVENT_BUFFER_CAPACITY : 0U;
  CopyConsumer(request.consumer, kLinuxGPIOConsumer);
  request.config.flags = BuildLineFlagsV2(config);

  if (ioctl(chip_fd_, GPIO_V2_GET_LINE_IOCTL, &request) < 0)
  {
    XR_LOG_ERROR("Failed to request GPIO line (v2): %s", std::strerror(errno));
    return ErrorCode::FAILED;
  }

  request_fd_.store(request.fd);
  request_kind_.store(IsInterruptDirection(config.direction) ? RequestKind::EVENT
                                                             : RequestKind::HANDLE);
  if (SetNonBlocking(request.fd) < 0)
  {
    XR_LOG_ERROR("Failed to enable non-blocking GPIO request fd: %s",
                 std::strerror(errno));
    CloseRequest();
    return ErrorCode::FAILED;
  }

  return ErrorCode::OK;
}

ErrorCode LinuxGPIO::ReconfigureRequestV2(Configuration config)
{
  gpio_v2_line_config line_config = {};
  line_config.flags = BuildLineFlagsV2(config);

  if (ioctl(request_fd_.load(), GPIO_V2_LINE_SET_CONFIG_IOCTL, &line_config) < 0)
  {
    XR_LOG_ERROR("Failed to reconfigure GPIO line (v2): %s", std::strerror(errno));
    return ErrorCode::FAILED;
  }

  request_kind_.store(IsInterruptDirection(config.direction) ? RequestKind::EVENT
                                                             : RequestKind::HANDLE);
  return ErrorCode::OK;
}

ErrorCode LinuxGPIO::OpenRequestV1(Configuration config)
{
  if (IsInterruptDirection(config.direction))
  {
    gpioevent_request request = {};
    request.lineoffset = line_offset_;
    request.handleflags = BuildHandleFlagsV1(config);
    request.eventflags = BuildEventFlagsV1(config.direction);
    CopyConsumer(request.consumer_label, kLinuxGPIOConsumer);

    if (ioctl(chip_fd_, GPIO_GET_LINEEVENT_IOCTL, &request) < 0)
    {
      XR_LOG_ERROR("Failed to request GPIO event line (v1): %s", std::strerror(errno));
      return ErrorCode::FAILED;
    }

    request_fd_.store(request.fd);
    request_kind_.store(RequestKind::EVENT);
  }
  else
  {
    gpiohandle_request request = {};
    request.lineoffsets[0] = line_offset_;
    request.lines = 1U;
    request.flags = BuildHandleFlagsV1(config);
    CopyConsumer(request.consumer_label, kLinuxGPIOConsumer);

    if (ioctl(chip_fd_, GPIO_GET_LINEHANDLE_IOCTL, &request) < 0)
    {
      XR_LOG_ERROR("Failed to request GPIO handle line (v1): %s", std::strerror(errno));
      return ErrorCode::FAILED;
    }

    request_fd_.store(request.fd);
    request_kind_.store(RequestKind::HANDLE);
  }

  if (SetNonBlocking(request_fd_.load()) < 0)
  {
    XR_LOG_ERROR("Failed to enable non-blocking GPIO request fd: %s",
                 std::strerror(errno));
    CloseRequest();
    return ErrorCode::FAILED;
  }

  return ErrorCode::OK;
}

ErrorCode LinuxGPIO::ReconfigureRequestV1(Configuration config)
{
  if (request_kind_.load() != RequestKind::HANDLE)
  {
    return ReopenRequest(config);
  }

  gpiohandle_config handle_config = {};
  handle_config.flags = BuildHandleFlagsV1(config);
  if (ioctl(request_fd_.load(), GPIOHANDLE_SET_CONFIG_IOCTL, &handle_config) < 0)
  {
    XR_LOG_ERROR("Failed to reconfigure GPIO line (v1): %s", std::strerror(errno));
    return ErrorCode::FAILED;
  }

  return ErrorCode::OK;
}

ErrorCode LinuxGPIO::PumpEventQueue(int fd, AbiVersion abi_version, size_t& event_count,
                                    int timeout_ms) const
{
  if (fd < 0)
  {
    return ErrorCode::STATE_ERR;
  }

  const int ready = PollReadable(fd, timeout_ms);
  if (ready < 0)
  {
    if (errno == EBADF)
    {
      return ErrorCode::EMPTY;
    }
    XR_LOG_ERROR("Failed to poll GPIO fd: %s", std::strerror(errno));
    return ErrorCode::FAILED;
  }

  if (ready == 0)
  {
    return ErrorCode::EMPTY;
  }

  if (abi_version == AbiVersion::V2)
  {
    return ReadEventsV2(fd, event_count);
  }

  return ReadEventsV1(fd, event_count);
}

ErrorCode LinuxGPIO::ReadEventsV2(int fd, size_t& event_count) const
{
  bool received = false;
  while (true)
  {
    std::array<gpio_v2_line_event, EVENT_BUFFER_CAPACITY> events = {};
    const ssize_t bytes = read(fd, events.data(), sizeof(events));
    if (bytes < 0)
    {
      if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
      {
        break;
      }
      if (errno == EBADF)
      {
        return received ? ErrorCode::OK : ErrorCode::EMPTY;
      }

      XR_LOG_ERROR("Failed to read GPIO v2 events: %s", std::strerror(errno));
      return ErrorCode::FAILED;
    }

    if (bytes == 0)
    {
      break;
    }

    if ((bytes % static_cast<ssize_t>(sizeof(gpio_v2_line_event))) != 0)
    {
      XR_LOG_ERROR("Corrupted GPIO v2 event payload length");
      return ErrorCode::FAILED;
    }

    const size_t count = static_cast<size_t>(bytes / sizeof(gpio_v2_line_event));
    for (size_t i = 0; i < count; ++i)
    {
      if (!IsKnownGPIOEvent(events[i].id))
      {
        continue;
      }

      ++event_count;
      received = true;
    }
  }

  return received ? ErrorCode::OK : ErrorCode::EMPTY;
}

ErrorCode LinuxGPIO::ReadEventsV1(int fd, size_t& event_count) const
{
  bool received = false;
  while (true)
  {
    gpioevent_data event = {};
    const ssize_t bytes = read(fd, &event, sizeof(event));
    if (bytes < 0)
    {
      if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
      {
        break;
      }
      if (errno == EBADF)
      {
        return received ? ErrorCode::OK : ErrorCode::EMPTY;
      }

      XR_LOG_ERROR("Failed to read GPIO v1 events: %s", std::strerror(errno));
      return ErrorCode::FAILED;
    }

    if (bytes == 0)
    {
      break;
    }

    if (bytes != static_cast<ssize_t>(sizeof(event)))
    {
      XR_LOG_ERROR("Corrupted GPIO v1 event payload length");
      return ErrorCode::FAILED;
    }

    if (!IsKnownGPIOEvent(event.id))
    {
      continue;
    }

    ++event_count;
    received = true;
  }

  return received ? ErrorCode::OK : ErrorCode::EMPTY;
}

ErrorCode LinuxGPIO::EnsureConfigured() const
{
  if (!has_config_ || (request_fd_.load() < 0))
  {
    XR_LOG_ERROR("GPIO is not configured");
    ASSERT(false);
    return ErrorCode::STATE_ERR;
  }

  return ErrorCode::OK;
}
bool LinuxGPIO::IsInterruptDirection(Direction direction)
{
  return direction == Direction::RISING_INTERRUPT ||
         direction == Direction::FALL_INTERRUPT ||
         direction == Direction::FALL_RISING_INTERRUPT;
}

bool LinuxGPIO::NeedsRequestReopen(Configuration config) const
{
  if (request_fd_.load() < 0)
  {
    return true;
  }

  const bool old_interrupt =
      interrupt_enabled_.load() && IsInterruptDirection(current_config_.direction);
  const bool new_interrupt = IsInterruptDirection(config.direction);

  if (abi_version_.load() == AbiVersion::V2)
  {
    return old_interrupt != new_interrupt;
  }

  if (new_interrupt)
  {
    return true;
  }

  if (old_interrupt)
  {
    return true;
  }

  return request_kind_.load() != RequestKind::HANDLE;
}

}  // namespace LibXR
