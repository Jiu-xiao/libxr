#include "rs485.hpp"
#include "test.hpp"

namespace
{

class TestRS485 : public LibXR::RS485
{
 public:
  LibXR::ErrorCode SetConfig(const Configuration& config) override
  {
    config_ = config;
    return LibXR::ErrorCode::OK;
  }

  LibXR::ErrorCode Write(LibXR::ConstRawData frame, LibXR::WriteOperation& op,
                         bool in_isr = false) override
  {
    last_write_ = frame;
    last_write_op_type_ = op.type;
    last_write_timeout_ = (op.type == LibXR::WriteOperation::OperationType::BLOCK)
                              ? op.data.sem_info.timeout
                              : 0;
    if (op.type != LibXR::WriteOperation::OperationType::NONE)
    {
      op.UpdateStatus(in_isr, LibXR::ErrorCode::OK);
    }
    return LibXR::ErrorCode::OK;
  }

  void Publish(LibXR::ConstRawData frame, bool in_isr = false) { OnFrame(frame, in_isr); }

  void Reset() override { reset_count_++; }

  Configuration config_;
  LibXR::ConstRawData last_write_;
  LibXR::WriteOperation::OperationType last_write_op_type_ =
      LibXR::WriteOperation::OperationType::NONE;
  uint32_t last_write_timeout_ = 0;
  int reset_count_ = 0;
};

}  // namespace

void test_rs485()
{
  TestRS485 bus;

  LibXR::RS485::Configuration config;
  config.baudrate = 1000000;
  config.parity = LibXR::UART::Parity::EVEN;
  config.data_bits = 9;
  config.stop_bits = 2;
  config.tx_active_level = true;
  config.assert_time_us = 2;
  config.deassert_time_us = 3;
  ASSERT(bus.SetConfig(config) == LibXR::ErrorCode::OK);
  ASSERT(bus.config_.baudrate == 1000000);
  ASSERT(bus.config_.parity == LibXR::UART::Parity::EVEN);
  ASSERT(bus.config_.data_bits == 9);
  ASSERT(bus.config_.stop_bits == 2);
  ASSERT(bus.config_.tx_active_level == true);
  ASSERT(bus.config_.assert_time_us == 2);
  ASSERT(bus.config_.deassert_time_us == 3);
  bus.Reset();
  ASSERT(bus.reset_count_ == 1);

  LibXR::Semaphore sem;
  LibXR::WriteOperation write_op(sem, 23);
  const uint8_t tx[] = {0x55, 0xAA};
  ASSERT(bus.Write({tx, sizeof(tx)}, write_op) == LibXR::ErrorCode::OK);
  ASSERT(bus.last_write_.addr_ == tx);
  ASSERT(bus.last_write_.size_ == sizeof(tx));
  ASSERT(bus.last_write_op_type_ == LibXR::WriteOperation::OperationType::BLOCK);
  ASSERT(bus.last_write_timeout_ == 23);

  static int wildcard_hits = 0;
  static int pattern_hits = 0;
  static int masked_hits = 0;
  static bool last_in_isr = false;

  wildcard_hits = 0;
  pattern_hits = 0;
  masked_hits = 0;
  last_in_isr = false;

  auto wildcard_cb = LibXR::RS485::Callback::Create(
      [](bool in_isr, void*, LibXR::ConstRawData)
      {
        last_in_isr = in_isr;
        wildcard_hits++;
      },
      reinterpret_cast<void*>(0));

  auto pattern_cb = LibXR::RS485::Callback::Create(
      [](bool, void*, LibXR::ConstRawData frame)
      {
        ASSERT(frame.size_ == 5);
        pattern_hits++;
      },
      reinterpret_cast<void*>(0));

  auto masked_cb = LibXR::RS485::Callback::Create(
      [](bool, void*, LibXR::ConstRawData frame)
      {
        ASSERT(frame.size_ == 5);
        masked_hits++;
      },
      reinterpret_cast<void*>(0));

  bus.Register(wildcard_cb);
  const uint8_t pattern[] = {0x12, 0x03};
  bus.Register(pattern_cb, LibXR::RS485::Filter{.offset = 2, .data = {pattern, 2}});
  const uint8_t masked_data[] = {0xA0};
  const uint8_t masked_mask[] = {0xF0};
  bus.Register(masked_cb,
               LibXR::RS485::Filter{
                   .offset = 0, .data = {masked_data, 1}, .mask = {masked_mask, 1}});

  const uint8_t raw[] = {0xAA, 0x55, 0x12, 0x03, 0x00};
  bus.Publish({raw, sizeof(raw)}, true);
  ASSERT(wildcard_hits == 1);
  ASSERT(pattern_hits == 1);
  ASSERT(masked_hits == 1);
  ASSERT(last_in_isr == true);

  const uint8_t other_raw[] = {0x11, 0x55, 0x12, 0x04, 0x00};
  bus.Publish({other_raw, sizeof(other_raw)}, false);
  ASSERT(wildcard_hits == 2);
  ASSERT(pattern_hits == 1);
  ASSERT(masked_hits == 1);

  const uint8_t short_raw[] = {0x12};
  bus.Publish({short_raw, sizeof(short_raw)}, false);
  ASSERT(wildcard_hits == 3);
  ASSERT(pattern_hits == 1);
  ASSERT(masked_hits == 1);
}
