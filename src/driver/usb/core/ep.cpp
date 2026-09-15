#include "ep.hpp"

#include <algorithm>
#include <limits>

#include "ep_pool.hpp"

namespace LibXR::USB
{
Endpoint::Endpoint(EPNumber number, Direction direction, RawData buffer)
    : number_(number),
      avail_direction_(direction),
      hardware_buffer_(buffer),
      storage_(static_cast<uint8_t*>(buffer.addr_)),
      capacity_(buffer.size_)
{
  ASSERT(buffer.addr_ != nullptr && buffer.size_ != 0U);
}

void Endpoint::TxFill::SetSize(size_t size)
{
  ASSERT(!supplied_ && size <= buffer_.size_);
  ASSERT(size != 0U || can_start_);
  supplied_ = true;
  size_ = size;
  if (size != 0U)
  {
    endpoint_.prepared_size_ = size;
  }
}

void Endpoint::InitializeStorage(size_t required)
{
  const size_t count =
      number_ != EPNumber::EP0 && avail_direction_ != Direction::OUT ? 2U : 1U;
  required = std::max(required, static_cast<size_t>(MaxPacketSize()));
  if (storage_initialized_ && required <= capacity_) return;
  if (storage_initialized_ && !IsPlanning())
  {
    SetState(State::ERROR);
    return;
  }
  if (storage_initialized_ && storage_ != hardware_buffer_.addr_) delete[] storage_;
  const size_t whole = hardware_buffer_.size_ / count;
  const size_t available = whole - whole % MaxPacketSize();
  required = std::max(required, static_cast<size_t>(MaxPacketSize()));
  if (!fixed_hardware_buffer_ && available >= required)
  {
    capacity_ = available;
    storage_ = static_cast<uint8_t*>(hardware_buffer_.addr_);
  }
  else
  {
    // Peripherals use HardwareBuffer as their bounce area if this CPU-only
    // allocation is not DMA reachable. No allocation occurs in the transfer path.
    const size_t packet = std::max(size_t{4}, static_cast<size_t>(MaxPacketSize()));
    ASSERT(required <= std::numeric_limits<size_t>::max() - packet);
    capacity_ = ((required + packet - 1U) / packet) * packet;
    ASSERT(capacity_ <= std::numeric_limits<size_t>::max() / count);
    storage_ = new uint8_t[capacity_ * count];
  }
  storage_initialized_ = true;
}

void Endpoint::ResetTransfer()
{
  prepared_size_ = 0;
  active_size_ = 0;
  completed_size_ = 0;
  segment_size_ = 0;
  hardware_active_ = false;
  transfer_buffer_ = {nullptr, 0};
  work_pending_.store(0, std::memory_order_relaxed);
  // Do not erase a completion which an IRQ is still publishing. Service retires
  // that old fact before any new hardware operation is permitted.
}

bool Endpoint::IsPlanning() const { return pool_ != nullptr && pool_->IsPlanning(); }

void Endpoint::Configure(const Config& cfg)
{
  ASSERT(cfg.direction != Direction::BOTH);
  EndpointPool::HardwareScope hardware(pool_);
  if (!IsPlanning() && GetState() != State::DISABLED) CloseHardware();
  ResetTransfer();
  config_ = cfg;
  const Speed speed = pool_ ? pool_->GetSpeed() : Speed::FULL;
  const size_t limit = cfg.type == Type::CONTROL ? (speed == Speed::LOW ? 8U : 64U)
                       : cfg.type == Type::BULK  ? (speed == Speed::HIGH ? 512U : 64U)
                       : cfg.type == Type::ISOCHRONOUS
                           ? (speed == Speed::HIGH ? 1024U : 1023U)
                           : (speed == Speed::HIGH  ? 1024U
                              : speed == Speed::LOW ? 8U
                                                    : 64U);
  if (cfg.max_packet_size == UINT16_MAX)
  {
    const size_t available = std::min(limit, hardware_buffer_.size_);
    config_.max_packet_size = static_cast<uint16_t>(available);
    if (cfg.type == Type::BULK)
      config_.max_packet_size = speed == Speed::HIGH ? 512U
                                : available >= 64U   ? 64U
                                : available >= 32U   ? 32U
                                : available >= 16U   ? 16U
                                                     : 8U;
  }
  if (config_.max_packet_size == 0U || config_.max_packet_size > limit ||
      (cfg.type == Type::BULK &&
       (speed == Speed::LOW ||
        (speed == Speed::HIGH
             ? config_.max_packet_size != 512U
             : config_.max_packet_size != 8U && config_.max_packet_size != 16U &&
                   config_.max_packet_size != 32U && config_.max_packet_size != 64U))))
  {
    SetState(State::ERROR);
    return;
  }
  const uint16_t requested = config_.max_packet_size;
  ConfigureHardware(config_);
  if (config_.max_packet_size != requested)
  {
    SetState(State::ERROR);  // Descriptor and actual endpoint must agree.
    return;
  }
  hardware.Release();
  if (GetState() == State::IDLE)
  {
    InitializeStorage(cfg.transfer_size);
  }
}

void Endpoint::ResetAfterHardwareStop()
{
  EndpointPool::HardwareScope hardware(pool_);
  ResetTransfer();
  SetState(State::IDLE);
}

void Endpoint::Close()
{
  EndpointPool::HardwareScope hardware(pool_);
  if (!IsPlanning()) CloseHardware();
  ResetTransfer();
  SetState(State::DISABLED);
}

ErrorCode Endpoint::Stall()
{
  EndpointPool::HardwareScope hardware(pool_);
  const ErrorCode result = StallHardware();
  if (result == ErrorCode::OK)
  {
    ResetTransfer();
    SetState(State::STALLED);
  }
  return result;
}

ErrorCode Endpoint::ClearStall()
{
  EndpointPool::HardwareScope hardware(pool_);
  const ErrorCode result = ClearStallHardware();
  if (result == ErrorCode::OK)
  {
    ResetTransfer();
    SetState(State::IDLE);
  }
  return result;
}

RawData Endpoint::GetBuffer() const
{
  const size_t index = number_ != EPNumber::EP0 && config_.direction == Direction::IN
                           ? (current_buffer_ ^ 1U)
                           : 0U;
  return {storage_ + capacity_ * index, capacity_};
}

RawData Endpoint::PayloadBuffer() const
{
  const size_t index = number_ != EPNumber::EP0 && config_.direction == Direction::IN
                           ? current_buffer_
                           : 0U;
  return {storage_ + capacity_ * index, capacity_};
}

void Endpoint::SetActiveLength(size_t size)
{
  ASSERT(size <= capacity_);
  prepared_size_ = size;
}

bool Endpoint::CanProgress() const
{
  return pool_ == nullptr ||
         (!pool_->IsPlanning() && !pool_->IsSuspended() && !pool_->HasPendingControl() &&
          (number_ == EPNumber::EP0 || pool_->DataEnabled()));
}

void Endpoint::RequestService(bool in_isr)
{
  work_pending_.store(1U, std::memory_order_release);
  ASSERT(pool_ != nullptr);
  if (pool_ != nullptr) pool_->RequestService(in_isr);
}

ErrorCode Endpoint::StartSegment()
{
  EndpointPool::HardwareScope hardware(pool_);
  // Recheck the producer doorbell at the zero-transfer commitment point. A
  // known newer write gets another Fill opportunity before a pending CDC tail.
  if (active_size_ == 0U && GetState() == State::IDLE &&
      work_pending_.load(std::memory_order_acquire) != 0U)
    return ErrorCode::OK;
  if (!CanProgress() || completion_ready_.load(std::memory_order_acquire) != 0U)
  {
    SetState(State::BUSY);
    hardware_active_ = false;
    return ErrorCode::OK;
  }
  const size_t remaining = active_size_ - completed_size_;
  const size_t limit = MaxHardwareTransferSize();
  ASSERT(limit >= MaxPacketSize());
  size_t size = std::min(remaining, limit);
  if (size < remaining) size -= size % MaxPacketSize();
  auto payload = PayloadBuffer();
  transfer_buffer_ = {static_cast<uint8_t*>(payload.addr_) + completed_size_,
                      payload.size_ - completed_size_};
  segment_size_ = size;
  hardware_active_ = true;
  SetState(State::BUSY);
  const auto result = StartHardware(transfer_buffer_, size);
  if (result != ErrorCode::OK)
  {
    hardware_active_ = false;
    SetState(State::ERROR);
  }
  hardware.Release();
  if (result == ErrorCode::OK && completed_size_ == 0U &&
      config_.direction == Direction::IN)
    on_started_.Run(pool_ ? pool_->CurrentContextIsISR() : false, active_size_);
  return result;
}

ErrorCode Endpoint::StartPrepared()
{
  ASSERT(prepared_size_ != 0U && GetState() == State::IDLE);
  if (!CanProgress() || completion_ready_.load(std::memory_order_acquire) != 0U)
    return ErrorCode::OK;
  if (number_ != EPNumber::EP0) current_buffer_ ^= 1U;
  active_size_ = prepared_size_;
  prepared_size_ = 0;
  completed_size_ = 0;
  return StartSegment();
}

ErrorCode Endpoint::StartZero()
{
  ASSERT(GetState() == State::IDLE && prepared_size_ == 0U);
  active_size_ = 0;
  completed_size_ = 0;
  return StartSegment();
}

ErrorCode Endpoint::Transfer(size_t size)
{
  if (config_.direction == Direction::OUT) return ArmReceive(size);
  if (GetState() != State::IDLE) return ErrorCode::BUSY;
  if (size > capacity_) return ErrorCode::NO_BUFF;
  if (size == 0U) return StartZero();
  prepared_size_ = size;
  return StartPrepared();
}

ErrorCode Endpoint::ArmReceive(size_t size)
{
  if (GetState() != State::IDLE && GetState() != State::RESULT) return ErrorCode::BUSY;
  if (size > capacity_) return ErrorCode::NO_BUFF;
  ASSERT(config_.direction == Direction::OUT);
  active_size_ = size;
  completed_size_ = 0;
  if (!CanProgress())
  {
    SetState(State::BUSY);
    hardware_active_ = false;
    return ErrorCode::OK;
  }
  return StartSegment();
}

ConstRawData Endpoint::ReceiveResult() const
{
  ASSERT(HasReceiveResult());
  return {storage_, completed_size_};
}

void Endpoint::OnTransferCompleteCallback(bool in_isr, size_t actual_size,
                                          ErrorCode result)
{
  if (GetState() != State::BUSY) return;
  uint32_t expected = 0;
  if (!completion_ready_.compare_exchange_strong(expected, 1U, std::memory_order_acquire))
  {
    // A duplicate IRQ must not overwrite an unconsumed exact completion.
    return;
  }
  completion_size_ = actual_size;
  completion_result_ = result;
  completion_ready_.store(2U, std::memory_order_release);
  if (pool_ != nullptr) pool_->RequestService(in_isr);
}

void Endpoint::CompleteSegment(bool in_isr)
{
  if (completion_ready_.load(std::memory_order_acquire) != 2U) return;
  const size_t actual = completion_size_;
  ErrorCode result = completion_result_;
  completion_ready_.store(0U, std::memory_order_release);
  if (!hardware_active_ || GetState() != State::BUSY) return;
  hardware_active_ = false;
  if (actual > segment_size_) result = ErrorCode::OUT_OF_RANGE;
  if (config_.direction == Direction::IN && actual != segment_size_ &&
      result == ErrorCode::OK)
    result = ErrorCode::FAILED;
  if (result != ErrorCode::OK)
  {
    SetState(State::ERROR);
    on_error_.Run(in_isr, result);
    return;
  }
  completed_size_ += actual;
  const bool short_packet = actual < segment_size_;
  if (!short_packet && completed_size_ < active_size_)
  {
    if (CanProgress()) (void)StartSegment();
    return;
  }
  const auto payload = PayloadBuffer();
  ConstRawData data{active_size_ == 0U ? nullptr : payload.addr_, completed_size_};
  SetState(config_.direction == Direction::IN ? State::IDLE : State::RESULT);
  on_transfer_complete_.Run(in_isr, data);
}

void Endpoint::DriveTx(bool in_isr)
{
  if (config_.direction != Direction::IN || !CanProgress()) return;
  if (GetState() != State::IDLE && GetState() != State::BUSY) return;
  if (GetState() == State::BUSY && !hardware_active_)
  {
    (void)StartSegment();
    return;
  }
  if (GetState() == State::IDLE && prepared_size_ != 0U)
  {
    (void)StartPrepared();
  }
  if (on_tx_fill_.Empty() || number_ == EPNumber::EP0) return;
  for (unsigned attempt = 0; attempt != 2U; ++attempt)
  {
    if (!CanProgress() || prepared_size_ != 0U ||
        completion_ready_.load(std::memory_order_acquire) != 0U)
      return;
    const State state = GetState();
    if (state != State::IDLE && state != State::BUSY) return;
    TxFill fill(*this, GetBuffer(), state == State::IDLE);
    on_tx_fill_.Run(in_isr, fill);
    if (!fill.supplied_ || !CanProgress()) return;
    if (completion_ready_.load(std::memory_order_acquire) != 0U) return;
    if (fill.size_ == 0U)
    {
      (void)StartZero();
    }
    else if (GetState() == State::IDLE)
    {
      (void)StartPrepared();
    }
  }
}

void Endpoint::Service(bool in_isr, bool force)
{
  const bool completed = completion_ready_.load(std::memory_order_acquire) == 2U;
  const bool work = work_pending_.exchange(0U, std::memory_order_acq_rel) != 0U || force;
  if (!completed && !work) return;
  if (completed) CompleteSegment(in_isr);
  if (work && CanProgress()) on_work_.Run(in_isr);
  if (config_.direction == Direction::OUT && GetState() == State::BUSY &&
      !hardware_active_ && CanProgress())
    (void)StartSegment();
  DriveTx(in_isr);
}
}  // namespace LibXR::USB
