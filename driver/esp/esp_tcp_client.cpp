#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>

#include "esp_vfs_eventfd.h"
#include "net/tcp.hpp"

namespace LibXR
{
static TaskHandle_t recv_task;
static TaskHandle_t send_task;
static TcpClient* tcp_clients[CONFIG_VFS_MAX_COUNT] = {};
static const uint64_t dummy = 'x';
static int pipe_read_fds[2], pipe_write_fds[2];  // pipe_fds[0] 是读端, pipe_fds[1] 是写端

static void CloseTcp(TcpClient* client)
{
  if (client->handle_.fd >= 0)
  {
    ::close(client->handle_.fd);
    int index = client->handle_.fd - LWIP_SOCKET_OFFSET;
    if (index >= 0 && index < CONFIG_VFS_MAX_COUNT)
    {
      tcp_clients[index] = nullptr;
    }
    else
    {
      ASSERT(false);
    }
    client->handle_.fd = -1;
  }
}

// Read 函数适配器
ErrorCode StaticRead(ReadPort& port)
{
  UNUSED(port);
  return ErrorCode::EMPTY;
}

// Write 函数适配器
ErrorCode StaticWrite(WritePort& port)
{
  if (port.queue_info_->Size() > 1)
  {
    return ErrorCode::BUSY;
  }

  TcpClient* client = static_cast<TcpClient*>(&port);

  WriteInfoBlock info;

  if (port.queue_info_->Peek(info) != ErrorCode::OK)
  {
    ASSERT(false);
    return ErrorCode::EMPTY;
  }

  if (client->handle_.fd < 0)
  {
    port.queue_data_->PopBatch(nullptr, info.data.size_);
    port.queue_info_->Pop();
    port.Finish(false, ErrorCode::FAILED, info, 0);
    return ErrorCode::FAILED;
  }

  int ret = ::send(client->handle_.fd, info.data.addr_, info.data.size_, 0);

  if (ret == info.data.size_)
  {
    port.queue_data_->PopBatch(nullptr, info.data.size_);
    port.queue_info_->Pop();
    return ErrorCode::OK;
  }

  if (ret < 0)
  {
    port.queue_data_->PopBatch(nullptr, info.data.size_);
    port.queue_info_->Pop();
    port.Finish(false, ErrorCode::FAILED, info, 0);
    CloseTcp(client);
    return ErrorCode::FAILED;
  }

  info.data.addr_ = reinterpret_cast<const uint8_t*>(info.data.addr_) + ret;
  info.data.size_ -= ret;
  port.queue_data_->PopBatch(nullptr, ret);

  client->handle_.written = ret;

  write(pipe_write_fds[1], &dummy, sizeof(dummy));

  return ErrorCode::NO_BUFF;
}

void ReadThread(void*)
{
  fd_set readfds;

  static uint8_t read_buff[TCP_WND];

  while (true)
  {
    FD_ZERO(&readfds);
    int max_fd = pipe_read_fds[0];
    FD_SET(pipe_read_fds[0], &readfds);
    for (int i = 0; i < CONFIG_VFS_MAX_COUNT; i++)
    {
      auto client = tcp_clients[i];
      auto fd = client ? client->handle_.fd : -1;
      if (client && fd >= 0)
      {
        FD_SET(client->handle_.fd, &readfds);
        max_fd = LibXR::max(max_fd, client->handle_.fd);
      }
    }

    struct timeval timeout = {
        .tv_sec = 10,
        .tv_usec = 0,
    };

    int ret = select(max_fd + 1, &readfds, NULL, NULL, &timeout);

    if (ret > 0)
    {
      for (int i = 0; i < CONFIG_VFS_MAX_COUNT; i++)
      {
        auto client = tcp_clients[i];
        auto fd = client ? client->handle_.fd : -1;
        if (client && fd >= 0 && FD_ISSET(fd, &readfds))
        {
          int available = 0;
          ioctl(fd, FIONREAD, &available);

          if (available > 0)
          {
            int ret = ::recv(fd, read_buff, available, 0);
            if (ret <= 0)
            {
              client->Close();
            }
            else
            {
              client->ReadPort::queue_data_->PushBatch(read_buff, ret);
              client->ProcessPendingReads(false);
            }
          }
          else
          {
            client->Close();
          }
        }
      }
      if (FD_ISSET(pipe_read_fds[0], &readfds))
      {
        uint64_t val;
        while (read(pipe_read_fds[0], &val, sizeof(val)) > 0 && val != 0)
        {
        }
      }
    }
  }
}

void WriteThread(void*)
{
  static uint8_t write_buffer[TCP_SND_BUF];

  WriteInfoBlock info;

  fd_set writefds;
  fd_set readfds;

  while (true)
  {
    FD_ZERO(&writefds);
    FD_ZERO(&readfds);
    FD_SET(pipe_write_fds[0], &readfds);
    int max_fd = pipe_write_fds[0];
    for (int i = 0; i < CONFIG_VFS_MAX_COUNT; i++)
    {
      auto client = tcp_clients[i];
      auto fd = client ? client->handle_.fd : -1;
      if (client && fd >= 0 && client->queue_info_->Size() > 0)
      {
        FD_SET(client->handle_.fd, &writefds);
        max_fd = LibXR::max(max_fd, client->handle_.fd);
      }
    }

    struct timeval timeout = {
        .tv_sec = 10,
        .tv_usec = 0,
    };

    int ret = select(max_fd + 1, &readfds, &writefds, NULL, &timeout);

    if (ret > 0)
    {
      for (int i = 0; i < CONFIG_VFS_MAX_COUNT; i++)
      {
        auto client = tcp_clients[i];
        auto fd = client ? client->handle_.fd : -1;
        if (client && fd >= 0 && FD_ISSET(fd, &writefds))
        {
          LibXR::Mutex::LockGuard lock_guard(client->WritePort::mutex_);
          while (client->queue_info_->Peek(info) == ErrorCode::OK)
          {
            auto need_write =
                LibXR::min(TCP_SND_BUF, info.data.size_ - client->handle_.written);

            if (client->WritePort::queue_data_->PeekBatch(write_buffer, need_write) !=
                ErrorCode::OK)
            {
              ASSERT(false);
              break;
            }

            int sent = ::send(fd, write_buffer, need_write, 0);
            if (sent == need_write)
            {
              client->WritePort::queue_data_->PopBatch(nullptr, sent);
              client->queue_info_->Pop();
              client->handle_.written = 0;
              client->WritePort::Finish(false, ErrorCode::OK, info, info.data.size_);
            }
            else if (sent < 0)
            {
              client->WritePort::queue_data_->PopBatch(nullptr, info.data.size_);
              client->queue_info_->Pop();
              client->handle_.written = 0;
              client->WritePort::Finish(false, ErrorCode::FAILED, info, sent);
              client->ReadPort::mutex_.Lock();
              CloseTcp(client);
              client->ReadPort::mutex_.Unlock();
              break;
            }

            client->WritePort::queue_data_->PopBatch(nullptr, sent);
            client->handle_.written += sent;
            break;
          }
        }
      }

      if (FD_ISSET(pipe_write_fds[0], &readfds))
      {
        uint64_t val;
        while (read(pipe_write_fds[0], &val, sizeof(val)) > 0 && val != 0)
        {
        }
      }
    }
  }
}

TcpClient::TcpClient(size_t read_buffer_size, size_t write_queue_size,
                     size_t write_buffer_size)
    : Socket(read_buffer_size, write_queue_size, write_buffer_size),
      remote_port_(0),
      handle_{-1, 0}
{
  static bool thread_inited = false;

  if (!thread_inited)
  {
    esp_vfs_eventfd_config_t cfg = ESP_VFS_EVENTD_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_vfs_eventfd_register(&cfg));
    pipe_read_fds[0] = eventfd(0, 0);
    pipe_read_fds[1] = pipe_read_fds[0];  // 同一个 fd
    pipe_write_fds[0] = eventfd(0, 0);
    pipe_write_fds[1] = pipe_write_fds[0];

    xTaskCreate(ReadThread, "TCP Read Thread",
                LIBXR_ESP_IDF_SOCKET_READ_THREAD_STACK_SIZE, nullptr,
                LIBXR_ESP_IDF_SOCKET_READ_THREAD_PRIORITY, &recv_task);
    xTaskCreate(WriteThread, "TCP Write Thread",
                LIBXR_ESP_IDF_SOCKET_WRITE_THREAD_STACK_SIZE, nullptr,
                LIBXR_ESP_IDF_SOCKET_WRITE_THREAD_PRIORITY, &send_task);
    thread_inited = true;
  }

  WritePort::operator=(StaticWrite);
  ReadPort::operator=(StaticRead);
}

bool TcpClient::Open(IPAddressRaw remote, uint16_t port)
{
  if (handle_.fd >= 0) Close();  // 清理旧连接

  handle_.fd = socket(AF_INET, SOCK_STREAM, 0);
  if (handle_.fd < 0) return false;

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  std::memcpy(&addr.sin_addr, remote.bytes, sizeof(remote.bytes));

  if (connect(handle_.fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
  {
    close(handle_.fd);
    handle_.fd = -1;
    return false;
  }

  int flags = fcntl(handle_.fd, F_GETFL, 0);
  fcntl(handle_.fd, F_SETFL, flags | O_NONBLOCK);

  remote_ip_ = remote;
  remote_port_ = port;
  tcp_clients[handle_.fd - LWIP_SOCKET_OFFSET] = this;

  write(pipe_read_fds[1], &dummy, sizeof(dummy));
  write(pipe_write_fds[1], &dummy, sizeof(dummy));
  return true;
}

bool TcpClient::IsOpen() const
{
  if (handle_.fd < 0) return false;

  char buf;
  int result = recv(handle_.fd, &buf, 1, MSG_PEEK | MSG_DONTWAIT);
  return !(result == 0 || (result < 0 && errno != EAGAIN && errno != EWOULDBLOCK));
}

bool TcpClient::IsConnected() const { return IsOpen(); }

void TcpClient::Close()
{
  Mutex::LockGuard lock_r(ReadPort::mutex_);
  Mutex::LockGuard lock_w(WritePort::mutex_);
  CloseTcp(this);
}

}  // namespace LibXR
