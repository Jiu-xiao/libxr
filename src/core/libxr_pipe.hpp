#pragma once

/**
 * @file
 * @brief Pipe: one-way byte stream from a WritePort to a ReadPort.
 */

#include "libxr_def.hpp"
#include "libxr_rw.hpp"

namespace LibXR
{

/**
 * @brief One shared byte ring connecting a WritePort producer to a ReadPort consumer.
 *
 * Pipe constructs WritePort(0, buffer_size) admission mode. The WritePort owns
 * the SPSC storage; ReadPort(0) borrows it as the sole consumer. No metadata
 * queue or asynchronous Pipe worker is involved.
 */
class Pipe
{
 public:
  explicit Pipe(size_t buffer_size) : read_port_(0), write_port_(0, buffer_size)
  {
    REQUIRE(write_port_.queue_data_ != nullptr);
    read_port_.BindQueue(write_port_.queue_data_);
    write_port_ = &WriteFun;
  }

  ~Pipe() = default;

  Pipe(const Pipe&) = delete;
  Pipe& operator=(const Pipe&) = delete;

  ReadPort& GetReadPort() { return read_port_; }
  WritePort& GetWritePort() { return write_port_; }

 private:
  static void WriteFun(WritePort& port, bool in_isr)
  {
    auto* pipe = LibXR::ContainerOf(&port, &Pipe::write_port_);
    pipe->read_port_.NotifyDataAvailable(in_isr);
  }

  ReadPort read_port_;
  WritePort write_port_;
};

}  // namespace LibXR
