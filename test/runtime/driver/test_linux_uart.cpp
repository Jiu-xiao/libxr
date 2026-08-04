#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "linux_uart.hpp"

namespace
{
using Clock = std::chrono::steady_clock;

struct Pty
{
  int master = -1;
  std::string slave;
};

Pty OpenPty()
{
  Pty pty;
  pty.master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
  ASSERT(pty.master >= 0);
  ASSERT(grantpt(pty.master) == 0);
  ASSERT(unlockpt(pty.master) == 0);

  std::array<char, 128> path{};
  ASSERT(ptsname_r(pty.master, path.data(), path.size()) == 0);
  pty.slave = path.data();
  return pty;
}

template <typename Predicate>
bool WaitUntil(Predicate predicate, uint32_t timeout_ms)
{
  const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
  while (Clock::now() < deadline)
  {
    if (predicate())
    {
      return true;
    }
    LibXR::Thread::Sleep(1U);
  }
  return predicate();
}

int RemainingMilliseconds(const Clock::time_point& deadline)
{
  const auto remaining =
      std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now())
          .count();
  if (remaining <= 0)
  {
    return 0;
  }
  return remaining > INT32_MAX ? INT32_MAX : static_cast<int>(remaining);
}

bool WaitForFd(int fd, short events, const Clock::time_point& deadline)
{
  pollfd descriptor{};
  descriptor.fd = fd;
  descriptor.events = events;

  while (true)
  {
    const int timeout = RemainingMilliseconds(deadline);
    if (timeout == 0)
    {
      return false;
    }
    const int result = poll(&descriptor, 1U, timeout);
    if (result > 0)
    {
      return (descriptor.revents & events) != 0;
    }
    if (result == 0)
    {
      return false;
    }
    if (errno != EINTR)
    {
      return false;
    }
  }
}

bool WriteAll(int fd, const uint8_t* data, size_t size, uint32_t timeout_ms)
{
  const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
  size_t offset = 0U;
  while (offset < size)
  {
    const ssize_t written = write(fd, data + offset, size - offset);
    if (written > 0)
    {
      offset += static_cast<size_t>(written);
      continue;
    }
    if (written < 0 && errno == EINTR)
    {
      continue;
    }
    if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
    {
      if (!WaitForFd(fd, POLLOUT, deadline))
      {
        return false;
      }
      continue;
    }
    return false;
  }
  return true;
}

bool ReadExact(int fd, uint8_t* data, size_t size, uint32_t timeout_ms)
{
  const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
  size_t offset = 0U;
  while (offset < size)
  {
    const ssize_t bytes = read(fd, data + offset, size - offset);
    if (bytes > 0)
    {
      offset += static_cast<size_t>(bytes);
      continue;
    }
    if (bytes < 0 && errno == EINTR)
    {
      continue;
    }
    if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
    {
      if (!WaitForFd(fd, POLLIN, deadline))
      {
        return false;
      }
      continue;
    }
    return false;
  }
  return true;
}

std::vector<uint8_t> MakePayload(size_t size, uint32_t seed)
{
  std::vector<uint8_t> payload(size);
  uint32_t state = seed;
  for (auto& byte : payload)
  {
    state = state * 1664525U + 1013904223U;
    byte = static_cast<uint8_t>(state >> 24U);
  }
  return payload;
}

std::string MakeStableLink(const std::string& target, std::string& directory)
{
  std::array<char, 64> path_template{};
  std::strcpy(path_template.data(), "/tmp/libxr_linux_uart_XXXXXX");
  char* created = mkdtemp(path_template.data());
  ASSERT(created != nullptr);
  directory = created;

  const std::string link = directory + "/uart";
  ASSERT(symlink(target.c_str(), link.c_str()) == 0);
  return link;
}

void RebindStableLink(const std::string& link, const std::string& target)
{
  const std::string replacement = link + ".next";
  (void)unlink(replacement.c_str());
  ASSERT(symlink(target.c_str(), replacement.c_str()) == 0);
  ASSERT(rename(replacement.c_str(), link.c_str()) == 0);
}

bool ProcessHasOpenTarget(const std::string& target)
{
  std::error_code error;
  for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fd", error))
  {
    if (error)
    {
      return true;
    }
    const auto resolved = std::filesystem::read_symlink(entry.path(), error);
    if (error)
    {
      error.clear();
      continue;
    }
    if (resolved.string().find(target) != std::string::npos)
    {
      return true;
    }
  }
  return false;
}

bool WaitForInotifyOpen(int fd, uint32_t timeout_ms)
{
  const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
  alignas(inotify_event) std::array<uint8_t, sizeof(inotify_event) + NAME_MAX + 1U>
      buffer{};

  while (WaitForFd(fd, POLLIN, deadline))
  {
    const ssize_t bytes = read(fd, buffer.data(), buffer.size());
    if (bytes < 0 && errno == EINTR)
    {
      continue;
    }
    if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
    {
      continue;
    }
    if (bytes <= 0)
    {
      return false;
    }

    size_t offset = 0U;
    while (offset + sizeof(inotify_event) <= static_cast<size_t>(bytes))
    {
      const auto* event = reinterpret_cast<const inotify_event*>(buffer.data() + offset);
      if ((event->mask & IN_OPEN) != 0U)
      {
        return true;
      }
      offset += sizeof(inotify_event) + event->len;
    }
  }
  return false;
}

void TxWakeAndPartialWriteScenario()
{
  Pty pty = OpenPty();
  auto* uart = new LibXR::LinuxUART(
      pty.slave.c_str(), 115200, LibXR::UART::Parity::NO_PARITY, 8, 1, 4, 256U * 1024U);

  const auto small = MakePayload(257U, 0x12345678U);
  LibXR::OperationPollingStatus small_status;
  LibXR::WriteOperation small_operation(small_status);
  ASSERT(uart->Write({small.data(), small.size()}, small_operation) ==
         LibXR::ErrorCode::OK);
  std::vector<uint8_t> small_rx(small.size());
  ASSERT(ReadExact(pty.master, small_rx.data(), small_rx.size(), 2000U));
  ASSERT(small_rx == small);
  ASSERT(WaitUntil([&]()
                   { return small_status.Load() == LibXR::OperationPollingStatus::DONE; },
                   1000U));

  const auto large = MakePayload(256U * 1024U, 0xCAFEBABEU);
  LibXR::OperationPollingStatus large_status;
  LibXR::WriteOperation large_operation(large_status);
  ASSERT(uart->Write({large.data(), large.size()}, large_operation) ==
         LibXR::ErrorCode::OK);
  LibXR::Thread::Sleep(50U);
  ASSERT(large_status.Load() == LibXR::OperationPollingStatus::RUNNING);

  std::vector<uint8_t> large_rx(large.size());
  ASSERT(ReadExact(pty.master, large_rx.data(), large_rx.size(), 6000U));
  ASSERT(large_rx == large);
  ASSERT(WaitUntil([&]()
                   { return large_status.Load() == LibXR::OperationPollingStatus::DONE; },
                   1000U));
}

void RxBackpressureScenario()
{
  Pty pty = OpenPty();
  auto* uart = new LibXR::LinuxUART(pty.slave.c_str(), 115200,
                                    LibXR::UART::Parity::NO_PARITY, 8, 1, 2, 64U);
  const auto input = MakePayload(96U, 0x10203040U);
  ASSERT(WriteAll(pty.master, input.data(), input.size(), 2000U));
  ASSERT(WaitUntil([&]() { return uart->read_port_->Size() == 64U; }, 2000U));

  std::array<uint8_t, 32> first{};
  LibXR::Semaphore first_sem;
  LibXR::ReadOperation first_operation(first_sem, 2000U);
  ASSERT(uart->Read({first.data(), first.size()}, first_operation) ==
         LibXR::ErrorCode::OK);
  ASSERT(std::memcmp(first.data(), input.data(), first.size()) == 0);

  std::array<uint8_t, 64> remainder{};
  LibXR::Semaphore remainder_sem;
  LibXR::ReadOperation remainder_operation(remainder_sem, 5000U);
  ASSERT(uart->Read({remainder.data(), remainder.size()}, remainder_operation) ==
         LibXR::ErrorCode::OK);
  ASSERT(std::memcmp(remainder.data(), input.data() + first.size(), remainder.size()) ==
         0);
}

void RxNonfullDequeueScenario()
{
  Pty pty = OpenPty();
  auto* uart = new LibXR::LinuxUART(pty.slave.c_str(), 115200,
                                    LibXR::UART::Parity::NO_PARITY, 8, 1, 2, 1024U);
  const auto input = MakePayload(256U, 0xA0B0C0D0U);
  ASSERT(WriteAll(pty.master, input.data(), input.size(), 2000U));
  ASSERT(WaitUntil([&]() { return uart->read_port_->Size() == input.size(); }, 2000U));

  for (size_t index = 0U; index < input.size(); ++index)
  {
    uint8_t byte = 0U;
    LibXR::Semaphore semaphore;
    LibXR::ReadOperation operation(semaphore, 1000U);
    ASSERT(uart->Read({&byte, 1U}, operation) == LibXR::ErrorCode::OK);
    ASSERT(byte == input[index]);
  }
}

void RxSpaceWakeStressScenario()
{
  Pty pty = OpenPty();
  auto* uart = new LibXR::LinuxUART(pty.slave.c_str(), 115200,
                                    LibXR::UART::Parity::NO_PARITY, 8, 1, 2, 2U);
  const auto input = MakePayload(97U, 0x0F1E2D3CU);
  ASSERT(WriteAll(pty.master, input.data(), input.size(), 2000U));

  for (size_t index = 0U; index < input.size(); ++index)
  {
    const size_t expected_size = std::min<size_t>(2U, input.size() - index);
    ASSERT(WaitUntil([&]() { return uart->read_port_->Size() == expected_size; }, 2000U));

    uint8_t byte = 0U;
    LibXR::Semaphore semaphore;
    LibXR::ReadOperation operation(semaphore, 1000U);
    ASSERT(uart->Read({&byte, 1U}, operation) == LibXR::ErrorCode::OK);
    ASSERT(byte == input[index]);
  }
}

bool BaudIs(int fd, uint32_t baudrate)
{
  struct termios2 config{};
  return ioctl(fd, TCGETS2, &config) == 0 && config.c_ispeed == baudrate &&
         config.c_ospeed == baudrate;
}

void ConfigSerializationScenario()
{
  Pty pty = OpenPty();
  const int observer =
      open(pty.slave.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
  ASSERT(observer >= 0);
  auto* uart = new LibXR::LinuxUART(
      pty.slave.c_str(), 115200, LibXR::UART::Parity::NO_PARITY, 8, 1, 4, 256U * 1024U);

  const auto payload = MakePayload(256U * 1024U, 0xA5A5A5A5U);
  LibXR::OperationPollingStatus status;
  LibXR::WriteOperation operation(status);
  ASSERT(uart->Write({payload.data(), payload.size()}, operation) ==
         LibXR::ErrorCode::OK);

  int available = 0;
  ASSERT(WaitUntil(
      [&]() { return ioctl(pty.master, FIONREAD, &available) == 0 && available > 0; },
      2000U));

  const auto queued_payload = MakePayload(128U * 1024U, 0x5A5A5A5AU);
  LibXR::OperationPollingStatus queued_status;
  LibXR::WriteOperation queued_operation(queued_status);
  ASSERT(uart->Write({queued_payload.data(), queued_payload.size()}, queued_operation) ==
         LibXR::ErrorCode::OK);

  const LibXR::UART::Configuration first_config = {57600U, LibXR::UART::Parity::NO_PARITY,
                                                   8U, 1U};
  const LibXR::UART::Configuration second_config = {
      38400U, LibXR::UART::Parity::NO_PARITY, 8U, 1U};
  ASSERT(uart->SetConfig(first_config) == LibXR::ErrorCode::OK);
  ASSERT(uart->SetConfig(second_config) == LibXR::ErrorCode::BUSY);
  ASSERT(uart->SetConfig({0U, LibXR::UART::Parity::NO_PARITY, 8U, 1U}) ==
         LibXR::ErrorCode::ARG_ERR);

  std::vector<uint8_t> received(payload.size());
  ASSERT(ReadExact(pty.master, received.data(), received.size(), 6000U));
  ASSERT(received == payload);
  ASSERT(WaitUntil([&]() { return status.Load() == LibXR::OperationPollingStatus::DONE; },
                   1000U));
  ASSERT(WaitUntil([&]() { return BaudIs(observer, first_config.baudrate); }, 2000U));

  std::vector<uint8_t> queued_received(queued_payload.size());
  ASSERT(ReadExact(pty.master, queued_received.data(), queued_received.size(), 4000U));
  ASSERT(queued_received == queued_payload);
  ASSERT(WaitUntil(
      [&]() { return queued_status.Load() == LibXR::OperationPollingStatus::DONE; },
      1000U));

  const auto guard = MakePayload(17U, 0x0BADF00DU);
  ASSERT(WriteAll(pty.master, guard.data(), guard.size(), 1000U));
  ASSERT(WaitUntil([&]() { return uart->read_port_->Size() == guard.size(); }, 1000U));

  LibXR::ErrorCode second_result = LibXR::ErrorCode::BUSY;
  ASSERT(WaitUntil(
      [&]()
      {
        second_result = uart->SetConfig(second_config);
        return second_result != LibXR::ErrorCode::BUSY;
      },
      1000U));
  ASSERT(second_result == LibXR::ErrorCode::OK);
  ASSERT(WaitUntil([&]() { return BaudIs(observer, second_config.baudrate); }, 2000U));

  std::array<uint8_t, 17> guard_rx{};
  LibXR::Semaphore guard_sem;
  LibXR::ReadOperation guard_operation(guard_sem, 1000U);
  ASSERT(uart->Read({guard_rx.data(), guard_rx.size()}, guard_operation) ==
         LibXR::ErrorCode::OK);
  ASSERT(std::memcmp(guard_rx.data(), guard.data(), guard.size()) == 0);
}

struct CallbackWriteConfigState
{
  LibXR::LinuxUART* uart = nullptr;
  const std::vector<uint8_t>* payload = nullptr;
  LibXR::WriteOperation* operation = nullptr;
  LibXR::UART::Configuration first_config{};
  LibXR::UART::Configuration second_config{};
  std::atomic<uint32_t> count{0U};
  std::atomic<int32_t> completion_result{static_cast<int32_t>(LibXR::ErrorCode::PENDING)};
  std::atomic<int32_t> write_result{static_cast<int32_t>(LibXR::ErrorCode::PENDING)};
  std::atomic<int32_t> first_config_result{
      static_cast<int32_t>(LibXR::ErrorCode::PENDING)};
  std::atomic<int32_t> second_config_result{
      static_cast<int32_t>(LibXR::ErrorCode::PENDING)};
};

void QueueWriteAndConfig(bool, CallbackWriteConfigState* state, LibXR::ErrorCode result)
{
  state->completion_result.store(static_cast<int32_t>(result), std::memory_order_relaxed);
  const LibXR::ErrorCode write_result = state->uart->Write(
      {state->payload->data(), state->payload->size()}, *state->operation);
  state->write_result.store(static_cast<int32_t>(write_result),
                            std::memory_order_relaxed);
  const LibXR::ErrorCode first_config_result =
      state->uart->SetConfig(state->first_config);
  state->first_config_result.store(static_cast<int32_t>(first_config_result),
                                   std::memory_order_relaxed);
  const LibXR::ErrorCode second_config_result =
      state->uart->SetConfig(state->second_config);
  state->second_config_result.store(static_cast<int32_t>(second_config_result),
                                    std::memory_order_relaxed);
  state->count.fetch_add(1U, std::memory_order_release);
}

void CallbackWriteConfigScenario()
{
  Pty pty = OpenPty();
  const int observer =
      open(pty.slave.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
  ASSERT(observer >= 0);
  auto* uart = new LibXR::LinuxUART(
      pty.slave.c_str(), 115200, LibXR::UART::Parity::NO_PARITY, 8, 1, 4, 256U * 1024U);

  const auto original = MakePayload(256U * 1024U, 0x71717171U);
  const auto nested = MakePayload(256U * 1024U, 0x72727272U);
  LibXR::OperationPollingStatus nested_status;
  LibXR::WriteOperation nested_operation(nested_status);
  CallbackWriteConfigState state;
  state.uart = uart;
  state.payload = &nested;
  state.operation = &nested_operation;
  state.first_config = {57600U, LibXR::UART::Parity::NO_PARITY, 8U, 1U};
  state.second_config = {38400U, LibXR::UART::Parity::NO_PARITY, 8U, 1U};

  auto callback = LibXR::Callback<LibXR::ErrorCode>::Create(QueueWriteAndConfig, &state);
  LibXR::WriteOperation original_operation(callback);
  ASSERT(uart->Write({original.data(), original.size()}, original_operation) ==
         LibXR::ErrorCode::OK);

  int available = 0;
  ASSERT(WaitUntil(
      [&]() { return ioctl(pty.master, FIONREAD, &available) == 0 && available > 0; },
      2000U));
  ASSERT(state.count.load(std::memory_order_acquire) == 0U);

  std::vector<uint8_t> original_rx(original.size());
  ASSERT(ReadExact(pty.master, original_rx.data(), original_rx.size(), 6000U));
  ASSERT(original_rx == original);
  ASSERT(WaitUntil([&]() { return state.count.load(std::memory_order_acquire) == 1U; },
                   1000U));
  ASSERT(static_cast<LibXR::ErrorCode>(state.completion_result.load(
             std::memory_order_relaxed)) == LibXR::ErrorCode::OK);
  ASSERT(static_cast<LibXR::ErrorCode>(
             state.write_result.load(std::memory_order_relaxed)) == LibXR::ErrorCode::OK);
  ASSERT(static_cast<LibXR::ErrorCode>(state.first_config_result.load(
             std::memory_order_relaxed)) == LibXR::ErrorCode::OK);
  ASSERT(static_cast<LibXR::ErrorCode>(state.second_config_result.load(
             std::memory_order_relaxed)) == LibXR::ErrorCode::BUSY);
  ASSERT(nested_status.Load() == LibXR::OperationPollingStatus::RUNNING);

  ASSERT(
      WaitUntil([&]() { return BaudIs(observer, state.first_config.baudrate); }, 2000U));

  std::vector<uint8_t> nested_rx(nested.size());
  ASSERT(ReadExact(pty.master, nested_rx.data(), nested_rx.size(), 6000U));
  ASSERT(nested_rx == nested);
  ASSERT(WaitUntil(
      [&]() { return nested_status.Load() == LibXR::OperationPollingStatus::DONE; },
      1000U));
}

void ReconnectScenario()
{
  Pty first = OpenPty();
  std::string directory;
  const std::string stable_link = MakeStableLink(first.slave, directory);
  auto* uart = new LibXR::LinuxUART(stable_link.c_str(), 115200,
                                    LibXR::UART::Parity::NO_PARITY, 8, 1, 4, 4096U);

  const auto first_payload = MakePayload(73U, 0x11111111U);
  LibXR::OperationPollingStatus first_status;
  LibXR::WriteOperation first_operation(first_status);
  ASSERT(uart->Write({first_payload.data(), first_payload.size()}, first_operation) ==
         LibXR::ErrorCode::OK);
  std::vector<uint8_t> first_rx(first_payload.size());
  ASSERT(ReadExact(first.master, first_rx.data(), first_rx.size(), 2000U));
  ASSERT(first_rx == first_payload);

  ASSERT(close(first.master) == 0);
  first.master = -1;
  ASSERT(WaitUntil([&]() { return !ProcessHasOpenTarget(first.slave); }, 2000U));

  const auto queued = MakePayload(1021U, 0x22222222U);
  LibXR::OperationPollingStatus queued_status;
  LibXR::WriteOperation queued_operation(queued_status);
  ASSERT(uart->Write({queued.data(), queued.size()}, queued_operation) ==
         LibXR::ErrorCode::OK);
  LibXR::Thread::Sleep(50U);
  ASSERT(queued_status.Load() == LibXR::OperationPollingStatus::RUNNING);

  Pty second = OpenPty();
  RebindStableLink(stable_link, second.slave);
  std::vector<uint8_t> queued_rx(queued.size());
  ASSERT(ReadExact(second.master, queued_rx.data(), queued_rx.size(), 4000U));
  ASSERT(queued_rx == queued);
  ASSERT(WaitUntil(
      [&]() { return queued_status.Load() == LibXR::OperationPollingStatus::DONE; },
      1000U));

  const auto inbound = MakePayload(61U, 0x33333333U);
  ASSERT(WriteAll(second.master, inbound.data(), inbound.size(), 1000U));
  std::vector<uint8_t> inbound_rx(inbound.size());
  LibXR::Semaphore inbound_sem;
  LibXR::ReadOperation inbound_operation(inbound_sem, 2000U);
  ASSERT(uart->Read({inbound_rx.data(), inbound_rx.size()}, inbound_operation) ==
         LibXR::ErrorCode::OK);
  ASSERT(inbound_rx == inbound);

  (void)unlink(stable_link.c_str());
  (void)rmdir(directory.c_str());
}

void ReconnectBackoffScenario()
{
  Pty first = OpenPty();
  std::string directory;
  const std::string stable_link = MakeStableLink(first.slave, directory);
  auto* uart = new LibXR::LinuxUART(stable_link.c_str(), 115200,
                                    LibXR::UART::Parity::NO_PARITY, 8, 1, 4, 4096U);

  Pty second = OpenPty();
  const auto queued = MakePayload(257U, 0x13572468U);
  LibXR::OperationPollingStatus queued_status;
  LibXR::WriteOperation queued_operation(queued_status);
  const LibXR::UART::Configuration reconnect_config = {
      57600U, LibXR::UART::Parity::NO_PARITY, 8U, 1U};

  const std::string probe_path = directory + "/probe";
  const int probe = open(probe_path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
  ASSERT(probe >= 0);
  ASSERT(close(probe) == 0);

  const int inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  ASSERT(inotify_fd >= 0);
  ASSERT(inotify_add_watch(inotify_fd, probe_path.c_str(), IN_OPEN) >= 0);

  RebindStableLink(stable_link, probe_path);
  ASSERT(close(first.master) == 0);
  first.master = -1;
  ASSERT(WaitForInotifyOpen(inotify_fd, 2000U));
  const auto failure_seen = Clock::now();

  RebindStableLink(stable_link, second.slave);
  ASSERT(uart->Write({queued.data(), queued.size()}, queued_operation) ==
         LibXR::ErrorCode::OK);
  ASSERT(uart->SetConfig(reconnect_config) == LibXR::ErrorCode::OK);

  const auto minimum_deadline = failure_seen + std::chrono::milliseconds(700U);
  ASSERT(Clock::now() < minimum_deadline);
  while (Clock::now() < minimum_deadline)
  {
    ASSERT(!ProcessHasOpenTarget(second.slave));
    int available = 0;
    ASSERT(ioctl(second.master, FIONREAD, &available) == 0);
    ASSERT(available == 0);
    LibXR::Thread::Sleep(5U);
  }
  const int observer =
      open(second.slave.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
  ASSERT(observer >= 0);
  ASSERT(WaitUntil([&]() { return BaudIs(observer, reconnect_config.baudrate); }, 1000U));

  std::vector<uint8_t> received(queued.size());
  ASSERT(ReadExact(second.master, received.data(), received.size(), 2000U));
  ASSERT(received == queued);
  ASSERT(WaitUntil(
      [&]() { return queued_status.Load() == LibXR::OperationPollingStatus::DONE; },
      1000U));

  ASSERT(close(observer) == 0);
  ASSERT(close(inotify_fd) == 0);
  ASSERT(unlink(stable_link.c_str()) == 0);
  ASSERT(unlink(probe_path.c_str()) == 0);
  ASSERT(rmdir(directory.c_str()) == 0);
}

void ConfigReconnectScenario()
{
  Pty first = OpenPty();
  std::string directory;
  const std::string stable_link = MakeStableLink(first.slave, directory);
  auto* uart = new LibXR::LinuxUART(
      stable_link.c_str(), 115200, LibXR::UART::Parity::NO_PARITY, 8, 1, 4, 256U * 1024U);

  const auto active = MakePayload(256U * 1024U, 0x61616161U);
  LibXR::OperationPollingStatus active_status;
  LibXR::WriteOperation active_operation(active_status);
  ASSERT(uart->Write({active.data(), active.size()}, active_operation) ==
         LibXR::ErrorCode::OK);

  int available = 0;
  ASSERT(WaitUntil(
      [&]() { return ioctl(first.master, FIONREAD, &available) == 0 && available > 0; },
      2000U));

  const LibXR::UART::Configuration reconnect_config = {
      57600U, LibXR::UART::Parity::NO_PARITY, 8U, 1U};
  const LibXR::UART::Configuration later_config = {38400U, LibXR::UART::Parity::NO_PARITY,
                                                   8U, 1U};
  ASSERT(uart->SetConfig(reconnect_config) == LibXR::ErrorCode::OK);
  ASSERT(uart->SetConfig(later_config) == LibXR::ErrorCode::BUSY);

  ASSERT(close(first.master) == 0);
  first.master = -1;
  ASSERT(WaitUntil(
      [&]() { return active_status.Load() == LibXR::OperationPollingStatus::ERROR; },
      2000U));
  ASSERT(WaitUntil([&]() { return !ProcessHasOpenTarget(first.slave); }, 2000U));

  const auto queued = MakePayload(2049U, 0x62626262U);
  LibXR::OperationPollingStatus queued_status;
  LibXR::WriteOperation queued_operation(queued_status);
  ASSERT(uart->Write({queued.data(), queued.size()}, queued_operation) ==
         LibXR::ErrorCode::OK);

  Pty second = OpenPty();
  const int observer =
      open(second.slave.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
  ASSERT(observer >= 0);
  RebindStableLink(stable_link, second.slave);

  ASSERT(WaitUntil([&]() { return BaudIs(observer, reconnect_config.baudrate); }, 4000U));
  std::vector<uint8_t> queued_rx(queued.size());
  ASSERT(ReadExact(second.master, queued_rx.data(), queued_rx.size(), 4000U));
  ASSERT(queued_rx == queued);
  ASSERT(WaitUntil(
      [&]() { return queued_status.Load() == LibXR::OperationPollingStatus::DONE; },
      1000U));

  LibXR::ErrorCode later_result = LibXR::ErrorCode::BUSY;
  ASSERT(WaitUntil(
      [&]()
      {
        later_result = uart->SetConfig(later_config);
        return later_result != LibXR::ErrorCode::BUSY;
      },
      1000U));
  ASSERT(later_result == LibXR::ErrorCode::OK);
  ASSERT(WaitUntil([&]() { return BaudIs(observer, later_config.baudrate); }, 2000U));

  (void)unlink(stable_link.c_str());
  (void)rmdir(directory.c_str());
}

struct CallbackState
{
  std::atomic<uint32_t> count{0U};
  std::atomic<int32_t> result{static_cast<int32_t>(LibXR::ErrorCode::PENDING)};
};

void RecordCompletion(bool, CallbackState* state, LibXR::ErrorCode result)
{
  state->result.store(static_cast<int32_t>(result), std::memory_order_relaxed);
  state->count.fetch_add(1U, std::memory_order_release);
}

void HupScenario()
{
  Pty pty = OpenPty();
  auto* uart = new LibXR::LinuxUART(
      pty.slave.c_str(), 115200, LibXR::UART::Parity::NO_PARITY, 8, 1, 2, 256U * 1024U);
  const auto payload = MakePayload(256U * 1024U, 0x44444444U);
  CallbackState state;
  auto callback = LibXR::Callback<LibXR::ErrorCode>::Create(RecordCompletion, &state);
  LibXR::WriteOperation operation(callback);
  ASSERT(uart->Write({payload.data(), payload.size()}, operation) ==
         LibXR::ErrorCode::OK);

  int available = 0;
  ASSERT(WaitUntil(
      [&]() { return ioctl(pty.master, FIONREAD, &available) == 0 && available > 0; },
      2000U));
  ASSERT(close(pty.master) == 0);
  pty.master = -1;
  ASSERT(WaitUntil([&]() { return state.count.load(std::memory_order_acquire) == 1U; },
                   2000U));
  ASSERT(static_cast<LibXR::ErrorCode>(state.result.load(std::memory_order_relaxed)) ==
         LibXR::ErrorCode::FAILED);
  LibXR::Thread::Sleep(50U);
  ASSERT(state.count.load(std::memory_order_acquire) == 1U);
}

void RunScenario(const char* name)
{
  std::fprintf(stderr, "\tLinuxUART scenario [%s]...\n", name);
  std::fflush(stderr);

  std::array<char, PATH_MAX> executable_path{};
  const ssize_t path_size =
      readlink("/proc/self/exe", executable_path.data(), executable_path.size() - 1U);
  ASSERT(path_size > 0);
  executable_path[static_cast<size_t>(path_size)] = '\0';
  const std::string scenario_executable =
      (std::filesystem::path(executable_path.data()).parent_path() / "linux_uart_test")
          .string();

  const pid_t child = fork();
  ASSERT(child >= 0);
  if (child == 0)
  {
    execl(scenario_executable.c_str(), scenario_executable.c_str(), name,
          static_cast<char*>(nullptr));
    _exit(127);
  }

  int status = 0;
  const auto deadline = Clock::now() + std::chrono::seconds(20);
  while (true)
  {
    const pid_t wait_result = waitpid(child, &status, WNOHANG);
    if (wait_result == child)
    {
      break;
    }
    if (wait_result < 0 && errno == EINTR)
    {
      continue;
    }
    ASSERT(wait_result == 0);
    if (Clock::now() >= deadline)
    {
      ASSERT(kill(child, SIGKILL) == 0);
      ASSERT(waitpid(child, &status, 0) == child);
      ASSERT(false);
    }
    LibXR::Thread::Sleep(1U);
  }
  ASSERT(WIFEXITED(status));
  ASSERT(WEXITSTATUS(status) == 0);
  std::fprintf(stderr, "\tLinuxUART scenario [%s] passed.\n", name);
  std::fflush(stderr);
}
}  // namespace

int RunLinuxUartScenario(const char* name)
{
  if (std::strcmp(name, "tx_wake_partial") == 0)
  {
    TxWakeAndPartialWriteScenario();
  }
  else if (std::strcmp(name, "rx_backpressure") == 0)
  {
    RxBackpressureScenario();
  }
  else if (std::strcmp(name, "rx_nonfull_dequeue") == 0)
  {
    RxNonfullDequeueScenario();
  }
  else if (std::strcmp(name, "rx_space_wake_stress") == 0)
  {
    RxSpaceWakeStressScenario();
  }
  else if (std::strcmp(name, "config_serialization") == 0)
  {
    ConfigSerializationScenario();
  }
  else if (std::strcmp(name, "callback_write_config") == 0)
  {
    CallbackWriteConfigScenario();
  }
  else if (std::strcmp(name, "reconnect") == 0)
  {
    ReconnectScenario();
  }
  else if (std::strcmp(name, "reconnect_backoff") == 0)
  {
    ReconnectBackoffScenario();
  }
  else if (std::strcmp(name, "config_reconnect") == 0)
  {
    ConfigReconnectScenario();
  }
  else if (std::strcmp(name, "active_tx_hup") == 0)
  {
    HupScenario();
  }
  else
  {
    return 2;
  }
  return 0;
}

void test_linux_uart()
{
  RunScenario("tx_wake_partial");
  RunScenario("rx_backpressure");
  RunScenario("rx_nonfull_dequeue");
  RunScenario("rx_space_wake_stress");
  RunScenario("config_serialization");
  RunScenario("callback_write_config");
  RunScenario("reconnect");
  RunScenario("reconnect_backoff");
  RunScenario("config_reconnect");
  RunScenario("active_tx_hup");
}
