#include "linux_i2c.hpp"

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <new>

#include "libxr_mem.hpp"
#include "logger.hpp"

namespace
{

template <typename OperationType>
LibXR::ErrorCode CompleteImmediate(OperationType& op, bool in_isr,
                                   LibXR::ErrorCode result)
{
  if (op.type != OperationType::OperationType::BLOCK)
  {
    op.UpdateStatus(in_isr, result);
  }
  return result;
}

LibXR::ErrorCode MapErrnoToErrorCode(int error_no)
{
  switch (error_no)
  {
    case 0:
      return LibXR::ErrorCode::OK;
    case ENXIO:
    case EREMOTEIO:
      return LibXR::ErrorCode::NO_RESPONSE;
    case ETIMEDOUT:
      return LibXR::ErrorCode::TIMEOUT;
    case EAGAIN:
    case EBUSY:
      return LibXR::ErrorCode::BUSY;
    case ENOENT:
    case ENODEV:
      return LibXR::ErrorCode::NOT_FOUND;
    case EINVAL:
      return LibXR::ErrorCode::ARG_ERR;
    case EMSGSIZE:
      return LibXR::ErrorCode::SIZE_ERR;
    case ENOMEM:
      return LibXR::ErrorCode::NO_MEM;
    case EACCES:
    case EPERM:
      return LibXR::ErrorCode::INIT_ERR;
    default:
      return LibXR::ErrorCode::FAILED;
  }
}

}  // namespace

namespace LibXR
{

LinuxI2C::LinuxI2C(const std::string& dev_path, size_t thread_stack_size)
    : device_path_(dev_path), request_sem_(0)
{
  ASSERT(!device_path_.empty());

  const ErrorCode open_ec = OpenDevice();
  if (open_ec != ErrorCode::OK)
  {
    ASSERT(false);
    return;
  }

  StartWorkerThread(thread_stack_size);
}

LinuxI2C::~LinuxI2C()
{
  stop_requested_.store(true);
  request_sem_.Post();
  WaitForWorkerThreadStopped();
  DrainPendingRequests(ErrorCode::STATE_ERR);
  CloseDevice();
}

ErrorCode LinuxI2C::Read(uint16_t slave_addr, RawData read_data, ReadOperation& op,
                         bool in_isr)
{
  if (in_isr)
  {
    return CompleteImmediate(op, in_isr, ErrorCode::NOT_SUPPORT);
  }

  const ErrorCode validate_ec = ValidateReadRequest(slave_addr, read_data);
  if ((validate_ec != ErrorCode::OK) || (read_data.size_ == 0U))
  {
    return CompleteImmediate(op, in_isr, validate_ec);
  }

  auto request = CreateRequest();
  if (!request)
  {
    return CompleteImmediate(op, in_isr, ErrorCode::NO_MEM);
  }

  request->kind = RequestKind::READ;
  request->slave_addr = slave_addr;
  request->read_data = read_data;
  request->read_op = op;
  return SubmitRequest(request, op, in_isr);
}

ErrorCode LinuxI2C::Write(uint16_t slave_addr, ConstRawData write_data, WriteOperation& op,
                          bool in_isr)
{
  if (in_isr)
  {
    return CompleteImmediate(op, in_isr, ErrorCode::NOT_SUPPORT);
  }

  const ErrorCode validate_ec = ValidateWriteRequest(slave_addr, write_data);
  if ((validate_ec != ErrorCode::OK) || (write_data.size_ == 0U))
  {
    return CompleteImmediate(op, in_isr, validate_ec);
  }

  auto request = CreateRequest();
  if (!request)
  {
    return CompleteImmediate(op, in_isr, ErrorCode::NO_MEM);
  }

  request->kind = RequestKind::WRITE;
  request->slave_addr = slave_addr;
  request->write_data = write_data;
  request->write_op = op;
  return SubmitRequest(request, op, in_isr);
}

ErrorCode LinuxI2C::SetConfig(Configuration config)
{
  UNUSED(config);
  return ErrorCode::NOT_SUPPORT;
}

ErrorCode LinuxI2C::MemRead(uint16_t slave_addr, uint16_t mem_addr, RawData read_data,
                            ReadOperation& op, MemAddrLength mem_addr_size, bool in_isr)
{
  if (in_isr)
  {
    return CompleteImmediate(op, in_isr, ErrorCode::NOT_SUPPORT);
  }

  const ErrorCode validate_ec = ValidateReadRequest(slave_addr, read_data);
  if ((validate_ec != ErrorCode::OK) || (read_data.size_ == 0U))
  {
    return CompleteImmediate(op, in_isr, validate_ec);
  }

  auto request = CreateRequest();
  if (!request)
  {
    return CompleteImmediate(op, in_isr, ErrorCode::NO_MEM);
  }

  request->kind = RequestKind::MEM_READ;
  request->slave_addr = slave_addr;
  request->mem_addr = mem_addr;
  request->mem_addr_size = mem_addr_size;
  request->read_data = read_data;
  request->read_op = op;
  return SubmitRequest(request, op, in_isr);
}

ErrorCode LinuxI2C::MemWrite(uint16_t slave_addr, uint16_t mem_addr,
                             ConstRawData write_data, WriteOperation& op,
                             MemAddrLength mem_addr_size, bool in_isr)
{
  if (in_isr)
  {
    return CompleteImmediate(op, in_isr, ErrorCode::NOT_SUPPORT);
  }

  const size_t mem_len = MemAddrLengthBytes(mem_addr_size);
  const ErrorCode validate_ec = ValidateWriteRequest(slave_addr, write_data, mem_len);
  if (validate_ec != ErrorCode::OK)
  {
    return CompleteImmediate(op, in_isr, validate_ec);
  }

  auto request = CreateRequest();
  if (!request)
  {
    return CompleteImmediate(op, in_isr, ErrorCode::NO_MEM);
  }

  request->kind = RequestKind::MEM_WRITE;
  request->slave_addr = slave_addr;
  request->mem_addr = mem_addr;
  request->mem_addr_size = mem_addr_size;
  request->write_data = write_data;
  request->write_op = op;
  return SubmitRequest(request, op, in_isr);
}

template <typename OperationType>
ErrorCode LinuxI2C::SubmitRequest(const std::shared_ptr<Request>& request, OperationType& op,
                                  bool in_isr)
{
  const bool is_block = op.type == OperationType::OperationType::BLOCK;
  if (is_block)
  {
    request->block_wait.Start(*op.data.sem_info.sem);
  }

  const auto fail_submit = [&](ErrorCode ec)
  {
    if (is_block)
    {
      request->block_wait.Cancel();
    }
    return CompleteImmediate(op, in_isr, ec);
  };

  if (request_busy_.TestAndSet())
  {
    return fail_submit(ErrorCode::BUSY);
  }

  if (request_mutex_.Lock() != ErrorCode::OK)
  {
    request_busy_.Clear();
    return fail_submit(ErrorCode::FAILED);
  }

  if (stop_requested_.load())
  {
    request_mutex_.Unlock();
    request_busy_.Clear();
    return fail_submit(ErrorCode::STATE_ERR);
  }

  ASSERT(pending_request_ == nullptr);
  pending_request_ = request;
  request_mutex_.Unlock();
  request_sem_.Post();

  if (is_block)
  {
    ASSERT(!in_isr);
    return request->block_wait.Wait(op.data.sem_info.timeout);
  }

  return ErrorCode::OK;
}

ErrorCode LinuxI2C::OpenDevice()
{
  fd_ = open(device_path_.c_str(), O_RDWR | O_CLOEXEC);
  if (fd_ < 0)
  {
    XR_LOG_ERROR("Failed to open I2C device %s: %s", device_path_.c_str(),
                 std::strerror(errno));
    return MapErrnoToErrorCode(errno);
  }

  if (ioctl(fd_, I2C_FUNCS, &functionality_) == 0)
  {
    functionality_known_ = true;
  }
  else
  {
    functionality_known_ = false;
    functionality_ = 0;
    XR_LOG_WARN("Failed to query I2C adapter functions on %s: %s",
                device_path_.c_str(), std::strerror(errno));
  }

  XR_LOG_PASS("Open I2C device: %s", device_path_.c_str());
  return ErrorCode::OK;
}

void LinuxI2C::CloseDevice()
{
  if (fd_ >= 0)
  {
    close(fd_);
    fd_ = -1;
  }
}

void LinuxI2C::StartWorkerThread(size_t thread_stack_size)
{
  worker_thread_.Create<LinuxI2C*>(
      this, [](LinuxI2C* self) { self->WorkerLoop(); }, "i2c_worker",
      thread_stack_size, Thread::Priority::REALTIME);
}

void LinuxI2C::WorkerLoop()
{
  worker_thread_started_.store(true);

  while (true)
  {
    const ErrorCode wait_ec = request_sem_.Wait(UINT32_MAX);
    if (wait_ec != ErrorCode::OK)
    {
      XR_LOG_WARN("LinuxI2C worker wait failed: %d", static_cast<int>(wait_ec));
      continue;
    }

    while (true)
    {
      auto request = AcquirePendingRequest();
      if (!request)
      {
        break;
      }

      if (IsReadRequest(request->kind))
      {
        request->read_op.MarkAsRunning();
      }
      else
      {
        request->write_op.MarkAsRunning();
      }

      if (stop_requested_.load())
      {
        CompleteRequest(request, ErrorCode::STATE_ERR);
        request_busy_.Clear();
        continue;
      }

      const ErrorCode ec = ExecuteRequest(request);
      CompleteRequest(request, ec);
      request_busy_.Clear();
    }

    if (stop_requested_.load())
    {
      break;
    }
  }

  DrainPendingRequests(ErrorCode::STATE_ERR);
  worker_thread_started_.store(false);
}

void LinuxI2C::WaitForWorkerThreadStopped()
{
  while (worker_thread_started_.load())
  {
    Thread::Sleep(1);
  }
}

void LinuxI2C::DrainPendingRequests(ErrorCode ec)
{
  while (true)
  {
    auto request = AcquirePendingRequest();
    if (!request)
    {
      return;
    }
    CompleteRequest(request, ec);
    request_busy_.Clear();
  }
}

std::shared_ptr<LinuxI2C::Request> LinuxI2C::AcquirePendingRequest()
{
  if (request_mutex_.Lock() != ErrorCode::OK)
  {
    return {};
  }

  if (!pending_request_)
  {
    request_mutex_.Unlock();
    return {};
  }

  auto request = pending_request_;
  pending_request_.reset();
  request_mutex_.Unlock();
  return request;
}

std::shared_ptr<LinuxI2C::Request> LinuxI2C::CreateRequest()
{
  return std::shared_ptr<Request>(new (std::nothrow) Request{});
}

ErrorCode LinuxI2C::ExecuteRequest(const std::shared_ptr<Request>& request)
{
  if (fd_ < 0)
  {
    return ErrorCode::STATE_ERR;
  }

  if (!SupportsRawI2C())
  {
    return ErrorCode::NOT_SUPPORT;
  }

  switch (request->kind)
  {
    case RequestKind::READ:
    {
      ::i2c_msg msg = {};
      msg.addr = request->slave_addr;
      msg.flags = I2C_M_RD;
      msg.len = static_cast<__u16>(request->read_data.size_);
      msg.buf = static_cast<__u8*>(request->read_data.addr_);
      return Transfer(&msg, 1U);
    }
    case RequestKind::WRITE:
    {
      ::i2c_msg msg = {};
      msg.addr = request->slave_addr;
      msg.flags = 0U;
      msg.len = static_cast<__u16>(request->write_data.size_);
      msg.buf = const_cast<__u8*>(
          static_cast<const __u8*>(request->write_data.addr_));
      return Transfer(&msg, 1U);
    }
    case RequestKind::MEM_READ:
    {
      const size_t mem_len = MemAddrLengthBytes(request->mem_addr_size);
      std::array<uint8_t, 2> mem_raw = {};
      EncodeMemAddr(request->mem_addr, request->mem_addr_size, mem_raw.data());

      std::array<::i2c_msg, 2> msgs = {};
      msgs[0].addr = request->slave_addr;
      msgs[0].flags = 0U;
      msgs[0].len = static_cast<__u16>(mem_len);
      msgs[0].buf = mem_raw.data();
      msgs[1].addr = request->slave_addr;
      msgs[1].flags = I2C_M_RD;
      msgs[1].len = static_cast<__u16>(request->read_data.size_);
      msgs[1].buf = static_cast<__u8*>(request->read_data.addr_);
      return Transfer(msgs.data(), 2U);
    }
    case RequestKind::MEM_WRITE:
    {
      const size_t mem_len = MemAddrLengthBytes(request->mem_addr_size);
      const size_t total_size = mem_len + request->write_data.size_;
      auto tx = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[total_size]);
      if (!tx)
      {
        return ErrorCode::NO_MEM;
      }

      EncodeMemAddr(request->mem_addr, request->mem_addr_size, tx.get());

      if (request->write_data.size_ > 0U)
      {
        Memory::FastCopy(tx.get() + mem_len, request->write_data.addr_,
                         request->write_data.size_);
      }

      ::i2c_msg msg = {};
      msg.addr = request->slave_addr;
      msg.flags = 0U;
      msg.len = static_cast<__u16>(total_size);
      msg.buf = tx.get();
      return Transfer(&msg, 1U);
    }
    default:
      return ErrorCode::ARG_ERR;
  }
}

void LinuxI2C::CompleteRequest(const std::shared_ptr<Request>& request, ErrorCode ec)
{
  if (IsReadRequest(request->kind))
  {
    if (request->read_op.type == ReadOperation::OperationType::BLOCK)
    {
      (void)request->block_wait.TryPost(false, ec);
    }
    else
    {
      request->read_op.UpdateStatus(false, ec);
    }
    return;
  }

  if (request->write_op.type == WriteOperation::OperationType::BLOCK)
  {
    (void)request->block_wait.TryPost(false, ec);
  }
  else
  {
    request->write_op.UpdateStatus(false, ec);
  }
}

ErrorCode LinuxI2C::ValidateAddress(uint16_t slave_addr) const
{
  if (slave_addr > 0x7FU)
  {
    return ErrorCode::ARG_ERR;
  }
  return ErrorCode::OK;
}

ErrorCode LinuxI2C::ValidateReadRequest(uint16_t slave_addr, RawData read_data) const
{
  const ErrorCode addr_ec = ValidateAddress(slave_addr);
  if (addr_ec != ErrorCode::OK)
  {
    return addr_ec;
  }

  const ErrorCode buffer_ec = ValidateReadBuffer(read_data);
  if (buffer_ec != ErrorCode::OK)
  {
    return buffer_ec;
  }

  return EnsureTransferLength(read_data.size_);
}

ErrorCode LinuxI2C::ValidateWriteRequest(uint16_t slave_addr, ConstRawData write_data,
                                         size_t prefix_size) const
{
  const ErrorCode addr_ec = ValidateAddress(slave_addr);
  if (addr_ec != ErrorCode::OK)
  {
    return addr_ec;
  }

  const ErrorCode buffer_ec = ValidateWriteBuffer(write_data);
  if (buffer_ec != ErrorCode::OK)
  {
    return buffer_ec;
  }

  if (prefix_size > 0xFFFFU || write_data.size_ > (0xFFFFU - prefix_size))
  {
    return ErrorCode::SIZE_ERR;
  }

  return ErrorCode::OK;
}

ErrorCode LinuxI2C::ValidateReadBuffer(RawData read_data) const
{
  if ((read_data.size_ > 0U) && (read_data.addr_ == nullptr))
  {
    return ErrorCode::PTR_NULL;
  }
  return ErrorCode::OK;
}

ErrorCode LinuxI2C::ValidateWriteBuffer(ConstRawData write_data) const
{
  if ((write_data.size_ > 0U) && (write_data.addr_ == nullptr))
  {
    return ErrorCode::PTR_NULL;
  }
  return ErrorCode::OK;
}

ErrorCode LinuxI2C::EnsureTransferLength(size_t size) const
{
  if (size > 0xFFFFU)
  {
    return ErrorCode::SIZE_ERR;
  }
  return ErrorCode::OK;
}

ErrorCode LinuxI2C::Transfer(::i2c_msg* msgs, uint32_t count)
{
  i2c_rdwr_ioctl_data ioctl_data = {};
  ioctl_data.msgs = msgs;
  ioctl_data.nmsgs = count;

  if (ioctl(fd_, I2C_RDWR, &ioctl_data) < 0)
  {
    const int error_no = errno;
    XR_LOG_WARN("LinuxI2C I2C_RDWR failed on %s (nmsgs=%u): %s",
                device_path_.c_str(), static_cast<unsigned>(count),
                std::strerror(error_no));
    return MapErrnoToErrorCode(error_no);
  }

  return ErrorCode::OK;
}

void LinuxI2C::EncodeMemAddr(uint16_t mem_addr, MemAddrLength mem_addr_size,
                             uint8_t* buffer)
{
  ASSERT(buffer != nullptr);

  if (MemAddrLengthBytes(mem_addr_size) == 1U)
  {
    buffer[0] = static_cast<uint8_t>(mem_addr & 0xFFU);
    return;
  }

  buffer[0] = static_cast<uint8_t>((mem_addr >> 8) & 0xFFU);
  buffer[1] = static_cast<uint8_t>(mem_addr & 0xFFU);
}

size_t LinuxI2C::MemAddrLengthBytes(MemAddrLength mem_addr_size)
{
  return (mem_addr_size == MemAddrLength::BYTE_16) ? 2U : 1U;
}

bool LinuxI2C::IsReadRequest(RequestKind kind)
{
  return kind == RequestKind::READ || kind == RequestKind::MEM_READ;
}

bool LinuxI2C::SupportsRawI2C() const
{
  if (!functionality_known_)
  {
    return true;
  }
  return (functionality_ & I2C_FUNC_I2C) != 0U;
}

}  // namespace LibXR
