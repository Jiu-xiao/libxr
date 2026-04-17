#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <linux/i2c.h>
#include <memory>
#include <string>

#include "flag.hpp"
#include "i2c.hpp"
#include "mutex.hpp"
#include "semaphore.hpp"
#include "thread.hpp"

namespace LibXR
{

class LinuxI2C : public I2C
{
 public:
  explicit LinuxI2C(const std::string& dev_path, size_t thread_stack_size = 65536);
  explicit LinuxI2C(const char* dev_path, size_t thread_stack_size = 65536)
      : LinuxI2C(std::string(dev_path), thread_stack_size)
  {
  }
  ~LinuxI2C();

  LinuxI2C(const LinuxI2C&) = delete;
  LinuxI2C& operator=(const LinuxI2C&) = delete;

  ErrorCode Read(uint16_t slave_addr, RawData read_data, ReadOperation& op,
                 bool in_isr = false) override;
  ErrorCode Write(uint16_t slave_addr, ConstRawData write_data, WriteOperation& op,
                  bool in_isr = false) override;
  ErrorCode SetConfig(Configuration config) override;
  ErrorCode MemRead(uint16_t slave_addr, uint16_t mem_addr, RawData read_data,
                    ReadOperation& op,
                    MemAddrLength mem_addr_size = MemAddrLength::BYTE_8,
                    bool in_isr = false) override;
  ErrorCode MemWrite(uint16_t slave_addr, uint16_t mem_addr, ConstRawData write_data,
                     WriteOperation& op,
                     MemAddrLength mem_addr_size = MemAddrLength::BYTE_8,
                     bool in_isr = false) override;

 private:
  enum class RequestKind : uint8_t
  {
    READ,
    WRITE,
    MEM_READ,
    MEM_WRITE,
  };

  struct Request
  {
    RequestKind kind = RequestKind::READ;
    uint16_t slave_addr = 0;
    uint16_t mem_addr = 0;
    MemAddrLength mem_addr_size = MemAddrLength::BYTE_8;
    RawData read_data = {};
    ConstRawData write_data = {};
    ReadOperation read_op = {};
    WriteOperation write_op = {};
    AsyncBlockWait block_wait = {};
  };

  template <typename OperationType>
  ErrorCode SubmitRequest(const std::shared_ptr<Request>& request, OperationType& op,
                          bool in_isr);

  ErrorCode OpenDevice();
  void CloseDevice();
  void StartWorkerThread(size_t thread_stack_size);
  void WorkerLoop();
  void WaitForWorkerThreadStopped();
  void DrainPendingRequests(ErrorCode ec);
  std::shared_ptr<Request> AcquirePendingRequest();
  static std::shared_ptr<Request> CreateRequest();
  ErrorCode ExecuteRequest(const std::shared_ptr<Request>& request);
  void CompleteRequest(const std::shared_ptr<Request>& request, ErrorCode ec);
  ErrorCode ValidateAddress(uint16_t slave_addr) const;
  ErrorCode ValidateReadRequest(uint16_t slave_addr, RawData read_data) const;
  ErrorCode ValidateWriteRequest(uint16_t slave_addr, ConstRawData write_data,
                                 size_t prefix_size = 0U) const;
  ErrorCode ValidateReadBuffer(RawData read_data) const;
  ErrorCode ValidateWriteBuffer(ConstRawData write_data) const;
  ErrorCode EnsureTransferLength(size_t size) const;
  ErrorCode Transfer(::i2c_msg* msgs, uint32_t count);
  static void EncodeMemAddr(uint16_t mem_addr, MemAddrLength mem_addr_size,
                            uint8_t* buffer);
  static size_t MemAddrLengthBytes(MemAddrLength mem_addr_size);
  static bool IsReadRequest(RequestKind kind);
  bool SupportsRawI2C() const;

  std::string device_path_;
  int fd_ = -1;
  unsigned long functionality_ = 0;
  bool functionality_known_ = false;
  Mutex request_mutex_;
  Semaphore request_sem_;
  std::shared_ptr<Request> pending_request_;
  Flag::Atomic request_busy_;
  std::atomic<bool> worker_thread_started_{false};
  std::atomic<bool> stop_requested_{false};
  Thread worker_thread_;
};

}  // namespace LibXR
