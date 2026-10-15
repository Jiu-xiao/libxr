#include "ep.hpp"

#include <algorithm>

#include "ep_pool.hpp"
#include "libxr_mem.hpp"

namespace LibXR::USB
{
Endpoint::Endpoint(EPNumber number, Direction direction, RawData slots,
                   uint8_t slot_count)
    : number_(number), avail_direction_(direction), memory_(slots)
{
  REQUIRE(slots.addr_ != nullptr && slots.size_ != 0U);
  REQUIRE(reinterpret_cast<uintptr_t>(slots.addr_) % HW_CACHE_LINE_SIZE == 0U);
  REQUIRE(slots.size_ % HW_CACHE_LINE_SIZE == 0U);
  if (slot_count == 0U) slot_count = number == EPNumber::EP0 ? 1U : 2U;
  REQUIRE(slot_count == 1U || (slot_count == 2U && number != EPNumber::EP0));
  slot_count_ = slot_count;
  slot_size_ = slots.size_ / slot_count;
  slot_size_ -= slot_size_ % HW_CACHE_LINE_SIZE;
  REQUIRE(slot_size_ != 0U);
}

uint32_t Endpoint::Enqueue(uint32_t word, uint8_t index)
{
  const uint8_t other = index ^ 1U;
  const uint8_t head = SlotOf(word, other) == QUEUED ? other : index;
  word = WithSlot(word, index, QUEUED);
  return head == 1U ? (word | HEAD_BIT) : (word & ~HEAD_BIT);
}

RawData Endpoint::SlotData(uint8_t index, size_t capacity) const
{
  return {static_cast<uint8_t*>(memory_.addr_) + index * slot_size_, capacity};
}

bool Endpoint::IsPlanning() const { return pool_ != nullptr && pool_->IsPlanning(); }

bool Endpoint::CanStart() const
{
  return pool_ == nullptr || pool_->CanStart(number_ == EPNumber::EP0);
}

void Endpoint::NotifyOwner(bool in_isr)
{
  if (pool_ != nullptr) pool_->NotifyEndpoint(pending_bit_, in_isr);
}

uint32_t Endpoint::NextTag(uint8_t index)
{
  sequence_ = (sequence_ + 1U) & 0x3FFFFFFFU;
  if (sequence_ == 0U) sequence_ = 1U;
  return (sequence_ << 2U) | index;
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

namespace
{
// Largest packet of the type at the speed (USB 2.0 5.5.3 / 5.7.3 / 5.8.3 / 5.6.3).
size_t SpeedLimit(Endpoint::Type type, Speed speed)
{
  switch (type)
  {
    case Endpoint::Type::CONTROL:
      return speed == Speed::LOW ? 8U : 64U;
    case Endpoint::Type::BULK:
      return speed == Speed::HIGH ? 512U : 64U;
    case Endpoint::Type::ISOCHRONOUS:
      return speed == Speed::HIGH ? 1024U : 1023U;
    default:
      return speed == Speed::HIGH ? 1024U : speed == Speed::LOW ? 8U : 64U;
  }
}

bool IsPowerOfTwo8To64(size_t packet)
{
  return packet == 8U || packet == 16U || packet == 32U || packet == 64U;
}
}  // namespace

bool Endpoint::IsLegalPacket(Type type, Speed speed, size_t packet)
{
  if (packet == 0U || packet > SpeedLimit(type, speed)) return false;
  if (type == Type::CONTROL)
    return speed == Speed::HIGH  ? packet == 64U
           : speed == Speed::LOW ? packet == 8U
                                 : IsPowerOfTwo8To64(packet);
  if (type == Type::BULK)
    return speed == Speed::HIGH ? packet == 512U
                                : speed != Speed::LOW && IsPowerOfTwo8To64(packet);
  return true;
}

uint16_t Endpoint::LargestPacketSize(Type type) const
{
  const Speed speed = pool_ ? pool_->GetSpeed() : Speed::FULL;
  const size_t room = std::min(slot_size_, MaxPacketSizeLimit(type));
  if (type == Type::CONTROL || type == Type::BULK)
  {
    // The legal sizes are a short list: take the largest one that fits.
    for (size_t packet = SpeedLimit(type, speed); packet >= 8U; packet /= 2U)
      if (packet <= room && IsLegalPacket(type, speed, packet))
        return static_cast<uint16_t>(packet);
    return 0U;
  }
  return static_cast<uint16_t>(std::min(room, SpeedLimit(type, speed)));
}

ErrorCode Endpoint::Configure(const Config& cfg, bool in_isr)
{
  // A configured endpoint is never touched by a rejected Configure.
  const bool planning = IsPlanning();
  if (!planning && (stopping_ || GetState() != State::DISABLED))
    return ErrorCode::STATE_ERR;
  if (cfg.direction == Direction::BOTH ||
      (avail_direction_ != Direction::BOTH && cfg.direction != avail_direction_))
  {
    SetState(State::ERROR);
    return ErrorCode::ARG_ERR;
  }

  const Speed speed = pool_ ? pool_->GetSpeed() : Speed::FULL;
  const size_t packet = cfg.max_packet_size;
  if (!IsLegalPacket(cfg.type, speed, packet) || packet > MaxPacketSizeLimit(cfg.type))
  {
    SetState(State::ERROR);
    return ErrorCode::ARG_ERR;
  }

  // The slot count is fixed at construction; a slot that cannot hold one packet is
  // NO_BUFF, never a silent switch to one slot.
  if (packet > slot_size_)
  {
    SetState(State::ERROR);
    return ErrorCode::NO_BUFF;
  }
  const size_t capacity =
      number_ == EPNumber::EP0 ? packet : (slot_size_ / packet) * packet;
  size_t receive = cfg.receive_size == 0U ? capacity : cfg.receive_size;
  if (receive > capacity || receive % packet != 0U)
  {
    SetState(State::ERROR);
    return ErrorCode::ARG_ERR;
  }

  config_ = cfg;
  config_.max_packet_size = static_cast<uint16_t>(packet);
  config_.receive_size = receive;
  granted_ = {static_cast<uint16_t>(packet), cfg.mult, cfg.burst, capacity, slot_count_};
  if (planning)
  {
    SetState(State::IDLE);
    return ErrorCode::OK;
  }

  ErrorCode result;
  {
    EndpointPool::HardwareScope hardware(pool_);
    result = ConfigureHardware(config_, granted_);
  }
  UNUSED(in_isr);
  if (result != ErrorCode::OK)
  {
    SetState(State::ERROR);
    return result;
  }

  capacity_.store(static_cast<uint32_t>(capacity), std::memory_order_relaxed);
  packet_.store(static_cast<uint32_t>(packet), std::memory_order_relaxed);
  receive_size_.store(static_cast<uint32_t>(receive), std::memory_order_relaxed);
  SetState(State::IDLE);

  uint32_t old = word_.load(std::memory_order_acquire);
  uint32_t next;
  do
  {
    // A stale slot keeps its memory until its holder returns it; the slots never move,
    // so it blocks only itself.
    next = (old & (SLOT_FIELDS | COPY_BIT)) | ENABLED_BIT;
    next += (old & ~(GEN_ONE - 1U)) + GEN_ONE;
    if (cfg.direction == Direction::IN) next |= DIR_IN_BIT;
  } while (!word_.compare_exchange_weak(old, next, std::memory_order_acq_rel,
                                        std::memory_order_acquire));
  return ErrorCode::OK;
}

void Endpoint::Close(bool in_isr)
{
  const State state = GetState();
  if (IsPlanning() || state == State::DISABLED || state == State::ERROR)
  {
    SetState(State::DISABLED);
    return;
  }
  stall_after_stop_ = false;
  close_after_stop_ = true;
  SetState(State::DISABLED);
  EndRequests(in_isr, true);
  if (!stopping_ && close_after_stop_) ApplyAfterStop(in_isr);
}

void Endpoint::Flush(bool in_isr)
{
  const State state = GetState();
  if (state == State::DISABLED || state == State::ERROR) return;
  EndRequests(in_isr, false);
}

ErrorCode Endpoint::Stall(bool in_isr)
{
  const State state = GetState();
  if (state == State::DISABLED || state == State::ERROR) return ErrorCode::STATE_ERR;
  if (state == State::STALLED) return ErrorCode::OK;
  SetState(State::STALLED);
  stall_after_stop_ = true;
  EndRequests(in_isr, false);
  if (!stopping_ && stall_after_stop_) ApplyAfterStop(in_isr);
  return ErrorCode::OK;
}

ErrorCode Endpoint::ClearStall(bool in_isr)
{
  const State state = GetState();
  if (state == State::DISABLED || state == State::ERROR) return ErrorCode::STATE_ERR;
  ErrorCode result = ErrorCode::OK;
  if (stall_after_stop_)
  {
    stall_after_stop_ = false;  // The halt never reached the hardware.
  }
  else
  {
    EndpointPool::HardwareScope hardware(pool_);
    result = ClearStallHardware(in_isr);
  }
  if (result != ErrorCode::OK) return result;
  SetState(State::IDLE);
  NotifyOwner(in_isr);
  return ErrorCode::OK;
}

// ---------------------------------------------------------------------------
// Ending requests and stopping the hardware
// ---------------------------------------------------------------------------

void Endpoint::EndRequests(bool in_isr, bool disable)
{
  // A completion the hardware already reported wins over the stop.
  RetireMailbox(in_isr);

  uint32_t old = word_.load(std::memory_order_acquire);
  uint32_t next;
  do
  {
    next = old & ~ZLP_QUEUED_BIT;
    if (disable) next &= ~ENABLED_BIT;
    for (uint8_t i = 0; i < 2U; ++i)
    {
      const uint32_t state = SlotOf(old, i);
      if (state == QUEUED)
      {
        next = WithSlot(next, i, FREE);
      }
      else if (state == LENT && disable)
      {
        next = WithSlot(next, i, STALE);
      }
    }
  } while (!word_.compare_exchange_weak(old, next, std::memory_order_acq_rel,
                                        std::memory_order_acquire));

  const bool copy = (old & COPY_LIVE_BIT) != 0U;
  if (!copy)
  {
    for (uint8_t i = 0; i < 2U; ++i)
      if (SlotOf(old, i) == QUEUED) ++pending_aborts_;
    if (old & ZLP_QUEUED_BIT) ++pending_aborts_;
  }

  if (inflight_ == NO_REQUEST)
  {
    if (copy) CompleteCopy(in_isr, ErrorCode::ABORTED);
    DeliverAborts(in_isr);
    return;
  }
  if (stopping_) return;  // An earlier stop is still pending; it ends everything.

  StopResult stop;
  {
    EndpointPool::HardwareScope hardware(pool_);
    stop = StopHardware(in_isr);
  }
  if (stop == StopResult::DONE)
  {
    FinishStop(in_isr);
    return;
  }
  stopping_ = true;
  if (pool_ != nullptr) pool_->AddStopping();
}

void Endpoint::FinishStop(bool in_isr)
{
  stop_confirmed_.store(0U, std::memory_order_relaxed);
  const bool was_stopping = stopping_;
  if (inflight_ != NO_REQUEST)
  {
    bool retired = false;
    size_t actual;
    {
      EndpointPool::HardwareScope hardware(pool_);
      actual = ReapProgress(inflight_request_, retired);
    }
    ErrorCode result = ErrorCode::ABORTED;
    if (inflight_ != ZLP_INDEX)
    {
      const SlotMeta& meta = meta_[inflight_];
      if (inflight_tail_)
      {
        result = retired ? ErrorCode::OK : ErrorCode::ABORTED;
        actual = meta.length;
      }
      else if (config_.direction == Direction::IN && retired && actual == meta.length &&
               !(meta.zlp && meta.length != 0U && meta.length % MaxPacketSize() == 0U))
      {
        result = ErrorCode::OK;
      }
    }
    else if (retired)
    {
      result = ErrorCode::OK;
      actual = 0U;
    }
    stopping_ = true;  // The request ends with the stop; no continuation.
    FinishInflight(in_isr, result, actual);
  }
  stopping_ = false;
  DeliverAborts(in_isr);
  ApplyAfterStop(in_isr);
  if (was_stopping && pool_ != nullptr) pool_->StopFinished(in_isr);
}

void Endpoint::ApplyAfterStop(bool in_isr)
{
  if (close_after_stop_)
  {
    close_after_stop_ = false;
    EndpointPool::HardwareScope hardware(pool_);
    CloseHardware(in_isr);
  }
  if (stall_after_stop_)
  {
    stall_after_stop_ = false;
    EndpointPool::HardwareScope hardware(pool_);
    (void)StallHardware(in_isr);
  }
}

void Endpoint::DeliverAborts(bool in_isr)
{
  while (pending_aborts_ != 0U)
  {
    --pending_aborts_;
    Deliver(in_isr, ErrorCode::ABORTED, 0U);
  }
}

// ---------------------------------------------------------------------------
// Owner: start and retire
// ---------------------------------------------------------------------------

void Endpoint::Service(bool in_isr)
{
  do
  {
    // On the normal path the next queued request starts before the completion callback
    // runs, so the callback's work does not delay the wire.
    start_before_deliver_ = true;
    RetireMailbox(in_isr);
    start_before_deliver_ = false;
    if (stopping_ && stop_confirmed_.load(std::memory_order_acquire) != 0U)
      FinishStop(in_isr);
    StartNext(in_isr);
    // Work posted for this endpoint meanwhile (often by its own callback) is done here.
  } while (pool_ != nullptr && pool_->TakeEndpointBit(pending_bit_));
}

void Endpoint::RetireCompletion(bool in_isr) { RetireMailbox(in_isr); }

void Endpoint::StartNext(bool in_isr)
{
  // A failed start completes at once; then the next queued request is tried.
  while (StartOne(in_isr, false))
  {
  }
}

bool Endpoint::StartOne(bool in_isr, bool defer_failure)
{
  if (stopping_ || inflight_ != NO_REQUEST || GetState() != State::IDLE || !CanStart() ||
      completion_ready_.load(std::memory_order_acquire) != 0U)
    return false;
  uint32_t old = word_.load(std::memory_order_acquire);
  uint32_t next;
  uint8_t pick;
  do
  {
    if (old & ZLP_QUEUED_BIT)
    {
      pick = ZLP_INDEX;
      next = (old & ~ZLP_QUEUED_BIT) | ZLP_INFLIGHT_BIT;
    }
    else
    {
      const bool q0 = SlotOf(old, 0U) == QUEUED;
      const bool q1 = SlotOf(old, 1U) == QUEUED;
      if (!q0 && !q1) return false;
      pick = (q0 && q1) ? ((old & HEAD_BIT) ? 1U : 0U) : (q0 ? 0U : 1U);
      next = WithSlot(old, pick, INFLIGHT);
    }
  } while (!word_.compare_exchange_weak(old, next, std::memory_order_acq_rel,
                                        std::memory_order_acquire));
  inflight_ = pick;
  inflight_tail_ = false;
  StartRequest(in_isr, pick, pick == ZLP_INDEX ? 0U : meta_[pick].length, defer_failure);
  return true;
}

void Endpoint::StartRequest(bool in_isr, uint8_t index, size_t length, bool defer_failure)
{
  const RawData slot = index == ZLP_INDEX ? RawData{memory_.addr_, 0U}
                                          : SlotData(index, granted_.slot_capacity);
  inflight_request_ = {slot, length, NextTag(index)};
  inflight_tag_.store(inflight_request_.tag, std::memory_order_release);
  ErrorCode result;
  {
    EndpointPool::HardwareScope hardware(pool_);
    result = StartHardware(inflight_request_, in_isr);
  }
  if (result != ErrorCode::OK)
  {
    inflight_tag_.store(0U, std::memory_order_release);
    if (defer_failure)
      start_failed_ = true;  // Completed after the completion being delivered now.
    else
      FinishInflight(in_isr, ErrorCode::FAILED, 0U);
  }
}

void Endpoint::RetireMailbox(bool in_isr)
{
  if (completion_ready_.load(std::memory_order_acquire) != 2U) return;
  const uint32_t tag = completion_tag_;
  size_t actual = completion_actual_;
  ErrorCode result = completion_result_;
  completion_ready_.store(0U, std::memory_order_release);
  if (inflight_ == NO_REQUEST || tag != inflight_request_.tag) return;

  if (inflight_ == ZLP_INDEX || inflight_tail_)
  {
    if (result == ErrorCode::OK && actual != 0U) result = ErrorCode::OUT_OF_RANGE;
    FinishInflight(in_isr, result, inflight_ == ZLP_INDEX ? 0U : meta_[inflight_].length);
    return;
  }

  const SlotMeta& meta = meta_[inflight_];
  const bool in = config_.direction == Direction::IN;
  if (result == ErrorCode::OK)
  {
    if (actual > meta.length)
      result = ErrorCode::OUT_OF_RANGE;
    else if (in && actual != meta.length)
      result = ErrorCode::FAILED;
  }
  if (result == ErrorCode::OK && in && meta.zlp && meta.length != 0U &&
      meta.length % MaxPacketSize() == 0U)
  {
    if (Ending())
    {
      FinishInflight(in_isr, ErrorCode::ABORTED, actual);
      return;
    }
    inflight_tail_ = true;
    inflight_request_ = {inflight_request_.slot, 0U, NextTag(inflight_)};
    inflight_tag_.store(inflight_request_.tag, std::memory_order_release);
    ErrorCode start;
    {
      EndpointPool::HardwareScope hardware(pool_);
      start = StartHardware(inflight_request_, in_isr);
    }
    if (start != ErrorCode::OK)
    {
      inflight_tag_.store(0U, std::memory_order_release);
      FinishInflight(in_isr, ErrorCode::FAILED, meta.length);
    }
    return;
  }
  FinishInflight(in_isr, result, actual);
}

void Endpoint::FinishInflight(bool in_isr, ErrorCode result, size_t actual)
{
  const uint8_t index = inflight_;
  inflight_ = NO_REQUEST;
  inflight_tail_ = false;
  start_failed_ = false;  // A failed early start is ended by whoever retires it first.
  inflight_tag_.store(0U, std::memory_order_release);
  const bool in = config_.direction == Direction::IN;
  // OUT data that arrives while the endpoint is being stopped belongs to nobody.
  if (stopping_ && !in && result == ErrorCode::OK) result = ErrorCode::ABORTED;

  if (index == ZLP_INDEX)
  {
    word_.fetch_and(~ZLP_INFLIGHT_BIT, std::memory_order_acq_rel);
    const bool early = StartEarly(in_isr);
    Deliver(in_isr, result, 0U);
    DeliverAborts(in_isr);
    if (early) FinishFailedStart(in_isr);
    return;
  }

  if (word_.load(std::memory_order_acquire) & COPY_LIVE_BIT)
  {
    if (in)
      FinishCopyWrite(in_isr, index, result, actual);
    else
      FinishCopyRead(in_isr, index, result, actual);
    DeliverAborts(in_isr);
    return;
  }

  uint32_t old = word_.load(std::memory_order_acquire);
  const bool lease = !in && result == ErrorCode::OK;
  while (!word_.compare_exchange_weak(old, WithSlot(old, index, lease ? LENT : FREE),
                                      std::memory_order_acq_rel,
                                      std::memory_order_acquire))
  {
  }
  const bool early = StartEarly(in_isr);
  if (lease)
    Deliver(in_isr, result, actual, {RawData{SlotData(index, 0U).addr_, actual}, index});
  else
    Deliver(in_isr, result, actual);
  DeliverAborts(in_isr);
  if (early) FinishFailedStart(in_isr);
}

bool Endpoint::StartEarly(bool in_isr)
{
  if (!start_before_deliver_) return false;
  start_before_deliver_ = false;  // Only the first retirement of a pass starts early.
  StartOne(in_isr, true);
  return true;
}

void Endpoint::FinishFailedStart(bool in_isr)
{
  if (!start_failed_) return;
  start_failed_ = false;
  FinishInflight(in_isr, ErrorCode::FAILED, 0U);
}

void Endpoint::Deliver(bool in_isr, ErrorCode result, size_t actual)
{
  Deliver(in_isr, result, actual, Slot{});
}

void Endpoint::Deliver(bool in_isr, ErrorCode result, size_t actual, const Slot& slot)
{
  const Completion completion{result, actual, slot};
  on_complete_.Run(in_isr, completion);
}

// ---------------------------------------------------------------------------
// IRQ side
// ---------------------------------------------------------------------------

void Endpoint::OnHardwareComplete(bool in_isr, uint32_t tag, size_t actual,
                                  ErrorCode result)
{
  if (tag == 0U || inflight_tag_.load(std::memory_order_acquire) != tag) return;
  uint32_t expected = 0U;
  if (!completion_ready_.compare_exchange_strong(expected, 1U, std::memory_order_acquire))
    return;  // A duplicate must not overwrite an unconsumed completion.
  completion_tag_ = tag;
  completion_actual_ = actual;
  completion_result_ = result;
  completion_ready_.store(2U, std::memory_order_release);
  NotifyOwner(in_isr);
}

void Endpoint::OnHardwareStopped(bool in_isr)
{
  stop_confirmed_.store(1U, std::memory_order_release);
  NotifyOwner(in_isr);
}

// ---------------------------------------------------------------------------
// Borrowing, any context
// ---------------------------------------------------------------------------

ErrorCode Endpoint::AcquireSlot(Slot& slot, bool in_isr)
{
  UNUSED(in_isr);
  uint32_t old = word_.load(std::memory_order_acquire);
  uint32_t next;
  uint8_t pick;
  size_t capacity;
  do
  {
    if ((old & ENABLED_BIT) == 0U || (old & DIR_IN_BIT) == 0U)
      return ErrorCode::STATE_ERR;
    if (old & COPY_BIT) return ErrorCode::BUSY;
    pick = Usable(old, 0U) ? 0U : Usable(old, 1U) ? 1U : NO_REQUEST;
    if (pick == NO_REQUEST) return ErrorCode::BUSY;
    capacity = capacity_.load(std::memory_order_relaxed);
    next = WithSlot(old, pick, LENT);
  } while (!word_.compare_exchange_weak(old, next, std::memory_order_acq_rel,
                                        std::memory_order_acquire));
  slot.data = SlotData(pick, capacity);
  slot.index = pick;
  return ErrorCode::OK;
}

ErrorCode Endpoint::Submit(const Slot& slot, size_t length, bool zlp, bool in_isr)
{
  if (slot.index > 1U) return ErrorCode::STATE_ERR;
  const uint8_t index = slot.index;
  uint32_t old = word_.load(std::memory_order_acquire);
  uint32_t next;
  do
  {
    const uint32_t state = SlotOf(old, index);
    if (state == STALE)
    {
      next = WithSlot(old, index, FREE);
      if (word_.compare_exchange_weak(old, next, std::memory_order_acq_rel,
                                      std::memory_order_acquire))
        return ErrorCode::ABORTED;
      continue;
    }
    if (state != LENT || (old & DIR_IN_BIT) == 0U) return ErrorCode::STATE_ERR;
    if (length > capacity_.load(std::memory_order_relaxed)) return ErrorCode::SIZE_ERR;
    meta_[index] = {length, zlp};
    next = Enqueue(old, index);
    if (word_.compare_exchange_weak(old, next, std::memory_order_acq_rel,
                                    std::memory_order_acquire))
      break;
  } while (true);
  NotifyOwner(in_isr);
  return ErrorCode::OK;
}

ErrorCode Endpoint::SubmitZeroLength(bool in_isr)
{
  uint32_t old = word_.load(std::memory_order_acquire);
  uint32_t next;
  do
  {
    if ((old & ENABLED_BIT) == 0U || (old & DIR_IN_BIT) == 0U)
      return ErrorCode::STATE_ERR;
    if (old & (COPY_BIT | ZLP_QUEUED_BIT | ZLP_INFLIGHT_BIT)) return ErrorCode::BUSY;
    for (uint8_t i = 0; i < 2U; ++i)
    {
      const uint32_t state = SlotOf(old, i);
      if (state == QUEUED || state == INFLIGHT) return ErrorCode::BUSY;
    }
    next = old | ZLP_QUEUED_BIT;
  } while (!word_.compare_exchange_weak(old, next, std::memory_order_acq_rel,
                                        std::memory_order_acquire));
  NotifyOwner(in_isr);
  return ErrorCode::OK;
}

void Endpoint::ReleaseSlot(const Slot& slot, bool in_isr)
{
  UNUSED(in_isr);
  if (slot.index > 1U) return;
  uint32_t old = word_.load(std::memory_order_acquire);
  do
  {
    const uint32_t state = SlotOf(old, slot.index);
    if (state != LENT && state != STALE) return;
  } while (!word_.compare_exchange_weak(old, WithSlot(old, slot.index, FREE),
                                        std::memory_order_acq_rel,
                                        std::memory_order_acquire));
}

ErrorCode Endpoint::Receive(size_t bytes, bool in_isr)
{
  uint32_t old = word_.load(std::memory_order_acquire);
  uint32_t next;
  uint8_t pick;
  do
  {
    if ((old & ENABLED_BIT) == 0U || (old & DIR_IN_BIT) != 0U)
      return ErrorCode::STATE_ERR;
    if (old & COPY_BIT) return ErrorCode::BUSY;
    const size_t packet = packet_.load(std::memory_order_relaxed);
    if (bytes > capacity_.load(std::memory_order_relaxed) ||
        (bytes == 0U ? number_ != EPNumber::EP0 : bytes % packet != 0U))
      return ErrorCode::SIZE_ERR;
    pick = Usable(old, 0U) ? 0U : Usable(old, 1U) ? 1U : NO_REQUEST;
    if (pick == NO_REQUEST) return ErrorCode::BUSY;
    next = WithSlot(old, pick, LENT);
  } while (!word_.compare_exchange_weak(old, next, std::memory_order_acq_rel,
                                        std::memory_order_acquire));
  // The claimed slot is private until it is queued.
  meta_[pick] = {bytes, false};
  old = next;
  do
  {
    if (SlotOf(old, pick) == STALE)
    {
      next = WithSlot(old, pick, FREE);
      if (word_.compare_exchange_weak(old, next, std::memory_order_acq_rel,
                                      std::memory_order_acquire))
        return ErrorCode::STATE_ERR;
      continue;
    }
    next = Enqueue(old, pick);
    if (word_.compare_exchange_weak(old, next, std::memory_order_acq_rel,
                                    std::memory_order_acquire))
      break;
  } while (true);
  NotifyOwner(in_isr);
  return ErrorCode::OK;
}

ErrorCode Endpoint::ReturnSlot(const Slot& slot, bool in_isr)
{
  if (slot.index > 1U) return ErrorCode::STATE_ERR;
  const uint8_t index = slot.index;
  uint32_t old = word_.load(std::memory_order_acquire);
  uint32_t next;
  do
  {
    const uint32_t state = SlotOf(old, index);
    if (state == STALE)
    {
      next = WithSlot(old, index, FREE);
      if (word_.compare_exchange_weak(old, next, std::memory_order_acq_rel,
                                      std::memory_order_acquire))
        return ErrorCode::ABORTED;
      continue;
    }
    if (state != LENT || (old & DIR_IN_BIT) != 0U) return ErrorCode::STATE_ERR;
    meta_[index] = {receive_size_.load(std::memory_order_relaxed), false};
    next = Enqueue(old, index);
    if (word_.compare_exchange_weak(old, next, std::memory_order_acq_rel,
                                    std::memory_order_acquire))
      break;
  } while (true);
  NotifyOwner(in_isr);
  return ErrorCode::OK;
}

// ---------------------------------------------------------------------------
// Copy mode
// ---------------------------------------------------------------------------

ErrorCode Endpoint::ClaimForCopy(bool in, size_t size, uint8_t& claimed, size_t& capacity,
                                 size_t& packet)
{
  uint32_t old = word_.load(std::memory_order_acquire);
  uint32_t next;
  do
  {
    if ((old & ENABLED_BIT) == 0U || ((old & DIR_IN_BIT) != 0U) != in)
      return ErrorCode::STATE_ERR;
    if (old & (COPY_BIT | ZLP_QUEUED_BIT | ZLP_INFLIGHT_BIT)) return ErrorCode::BUSY;
    for (uint8_t i = 0; i < 2U; ++i)
    {
      const uint32_t state = SlotOf(old, i);
      if (state == LENT || state == QUEUED || state == INFLIGHT) return ErrorCode::BUSY;
    }
    capacity = capacity_.load(std::memory_order_relaxed);
    packet = packet_.load(std::memory_order_relaxed);
    claimed = 0U;
    for (uint8_t i = 0; i < 2U; ++i)
      if (Usable(old, i)) claimed |= static_cast<uint8_t>(1U << i);
    if (claimed == 0U) return ErrorCode::BUSY;
    // Reads use one slot at a time; a write needing one slot leaves the other free.
    if ((!in || size <= capacity) && claimed == 3U) claimed = 1U;
    next = old | COPY_BIT;
    for (uint8_t i = 0; i < 2U; ++i)
      if (claimed & (1U << i)) next = WithSlot(next, i, LENT);
  } while (!word_.compare_exchange_weak(old, next, std::memory_order_acq_rel,
                                        std::memory_order_acquire));
  return ErrorCode::OK;
}

ErrorCode Endpoint::PublishCopy(const uint8_t* order, uint8_t count, uint8_t claimed)
{
  uint32_t old = word_.load(std::memory_order_acquire);
  uint32_t next;
  do
  {
    next = old;
    if (SlotOf(old, order[0]) == STALE)
    {
      // Unbound before acceptance: the copy never existed for the owner.
      for (uint8_t i = 0; i < 2U; ++i)
        if (claimed & (1U << i)) next = WithSlot(next, i, FREE);
      next &= ~COPY_BIT;
      if (word_.compare_exchange_weak(old, next, std::memory_order_acq_rel,
                                      std::memory_order_acquire))
        return ErrorCode::ABORTED;
      continue;
    }
    for (uint8_t i = 0; i < count; ++i) next = Enqueue(next, order[i]);
    for (uint8_t i = 0; i < 2U; ++i)
      if ((claimed & (1U << i)) && SlotOf(next, i) == LENT)
        next = WithSlot(next, i, FREE);
    next |= COPY_LIVE_BIT;
  } while (!word_.compare_exchange_weak(old, next, std::memory_order_acq_rel,
                                        std::memory_order_acquire));
  return ErrorCode::OK;
}

ErrorCode Endpoint::Write(ConstRawData data, bool zlp, bool in_isr)
{
  if (data.size_ != 0U && data.addr_ == nullptr) return ErrorCode::PTR_NULL;
  uint8_t claimed = 0U;
  size_t capacity = 0U;
  size_t packet = 0U;
  const ErrorCode claim = ClaimForCopy(true, data.size_, claimed, capacity, packet);
  if (claim != ErrorCode::OK) return claim;

  copy_source_ = static_cast<const uint8_t*>(data.addr_);
  copy_target_ = nullptr;
  copy_size_ = data.size_;
  copy_offset_ = 0U;
  copy_done_ = 0U;
  copy_zlp_ = zlp;
  uint8_t order[2];
  uint8_t count = 0U;
  for (uint8_t i = 0; i < 2U; ++i)
  {
    if ((claimed & (1U << i)) == 0U) continue;
    const size_t length = std::min(capacity, copy_size_ - copy_offset_);
    if (length != 0U)
      Memory::FastCopy(SlotData(i, capacity).addr_, copy_source_ + copy_offset_, length);
    copy_offset_ += length;
    meta_[i] = {length, copy_offset_ == copy_size_ && zlp};
    order[count++] = i;
    if (copy_offset_ == copy_size_) break;
  }
  const ErrorCode result = PublishCopy(order, count, claimed);
  if (result == ErrorCode::OK) NotifyOwner(in_isr);
  return result;
}

ErrorCode Endpoint::Read(RawData buffer, bool in_isr)
{
  if (buffer.size_ != 0U && buffer.addr_ == nullptr) return ErrorCode::PTR_NULL;
  uint8_t claimed = 0U;
  size_t capacity = 0U;
  size_t packet = 0U;
  const ErrorCode claim = ClaimForCopy(false, buffer.size_, claimed, capacity, packet);
  if (claim != ErrorCode::OK) return claim;

  copy_source_ = nullptr;
  copy_target_ = static_cast<uint8_t*>(buffer.addr_);
  copy_size_ = buffer.size_;
  copy_offset_ = 0U;
  copy_done_ = 0U;
  copy_zlp_ = false;
  const uint8_t index = (claimed & 1U) ? 0U : 1U;
  meta_[index] = {CopyReadArm(capacity, packet), false};
  const ErrorCode result = PublishCopy(&index, 1U, claimed);
  if (result == ErrorCode::OK) NotifyOwner(in_isr);
  return result;
}

size_t Endpoint::CopyReadArm(size_t capacity, size_t packet) const
{
  const size_t remaining = copy_size_ - copy_done_;
  const size_t arm =
      remaining == 0U ? packet : ((remaining + packet - 1U) / packet) * packet;
  return std::min(arm, capacity);
}

void Endpoint::CompleteCopy(bool in_isr, ErrorCode result)
{
  // Read before the claim is released: a new copy may start right after it.
  const size_t done = copy_done_;
  word_.fetch_and(~(COPY_BIT | COPY_LIVE_BIT), std::memory_order_acq_rel);
  Deliver(in_isr, result, done);
}

void Endpoint::FinishCopyWrite(bool in_isr, uint8_t index, ErrorCode result,
                               size_t actual)
{
  copy_done_ += actual;
  uint32_t old = word_.load(std::memory_order_acquire);
  uint32_t next;
  const uint8_t other = index ^ 1U;
  const bool more = copy_offset_ < copy_size_;
  if (result != ErrorCode::OK || Ending())
  {
    do
    {
      next = WithSlot(old, index, FREE);
      if (SlotOf(old, other) == QUEUED) next = WithSlot(next, other, FREE);
    } while (!word_.compare_exchange_weak(old, next, std::memory_order_acq_rel,
                                          std::memory_order_acquire));
    if (result == ErrorCode::OK &&
        (more || SlotOf(old, other) == QUEUED || copy_done_ != copy_size_))
      result = ErrorCode::ABORTED;
    CompleteCopy(in_isr, result);
    return;
  }
  if (more)
  {
    const size_t capacity = granted_.slot_capacity;
    const size_t length = std::min(capacity, copy_size_ - copy_offset_);
    Memory::FastCopy(SlotData(index, capacity).addr_, copy_source_ + copy_offset_,
                     length);
    copy_offset_ += length;
    meta_[index] = {length, copy_offset_ == copy_size_ && copy_zlp_};
    while (!word_.compare_exchange_weak(old, Enqueue(WithSlot(old, index, FREE), index),
                                        std::memory_order_acq_rel,
                                        std::memory_order_acquire))
    {
    }
    return;
  }
  while (!word_.compare_exchange_weak(old, WithSlot(old, index, FREE),
                                      std::memory_order_acq_rel,
                                      std::memory_order_acquire))
  {
  }
  if (SlotOf(old, other) != QUEUED && SlotOf(old, other) != INFLIGHT)
    CompleteCopy(in_isr, ErrorCode::OK);
}

void Endpoint::FinishCopyRead(bool in_isr, uint8_t index, ErrorCode result, size_t actual)
{
  uint32_t old = word_.load(std::memory_order_acquire);
  const size_t room = copy_size_ - copy_done_;
  bool done = true;
  if (result == ErrorCode::OK && !Ending())
  {
    const size_t amount = std::min(actual, room);
    if (amount != 0U)
      Memory::FastCopy(copy_target_ + copy_done_,
                       SlotData(index, granted_.slot_capacity).addr_, amount);
    copy_done_ += amount;
    if (actual > room)
      result = ErrorCode::SIZE_ERR;
    else if (actual == meta_[index].length && copy_done_ != copy_size_)
      done = false;
  }
  else if (result == ErrorCode::OK)
  {
    result = ErrorCode::ABORTED;
  }
  if (!done)
  {
    meta_[index] = {CopyReadArm(granted_.slot_capacity, MaxPacketSize()), false};
    while (!word_.compare_exchange_weak(old, Enqueue(WithSlot(old, index, FREE), index),
                                        std::memory_order_acq_rel,
                                        std::memory_order_acquire))
    {
    }
    return;
  }
  while (!word_.compare_exchange_weak(old, WithSlot(old, index, FREE),
                                      std::memory_order_acq_rel,
                                      std::memory_order_acquire))
  {
  }
  CompleteCopy(in_isr, result);
}

}  // namespace LibXR::USB
