#pragma once

#include <cstddef>
#include <cstdint>

#include "core.hpp"
#include "ep.hpp"
#include "serialized_service.hpp"

namespace LibXR::USB
{

/**
 * @brief USB端点池类 / USB endpoint pool class
 *
 * 按「端点号 × 方向」二维索引管理固定的 Endpoint 指针。所有 Endpoint 对象由后端在
 * 初始化时通过 Put() 填入，池只负责登记、分配、查找与回收，不负责对象生命周期。
 * 端点 0 的 IN/OUT 不进入索引数组，由 SetEndpoint0/GetEndpoint0* 单独管理。
 *
 * Manages fixed Endpoint pointers indexed by (endpoint-number, direction). All Endpoint
 * objects are populated by the backend at init time via Put(); the pool only registers,
 * allocates, looks up and recycles them, and does not own their lifetime. Endpoint 0's
 * IN/OUT are kept out of the index array and managed by SetEndpoint0/GetEndpoint0*.
 *
 * @note 分配/回收发生在配置切换（SetConfiguration/Reset）这类低频、非并发路径，
 *       因此使用普通数组与非原子状态，无需无锁结构。
 *       Allocation/recycling happens on low-frequency, non-concurrent paths
 *       (SetConfiguration/Reset), so a plain array with non-atomic state is used.
 */
class EndpointPool
{
 public:
  /**
   * @brief 构造函数 / Constructor
   * @param endpoint_num 端点总数（包含端点0，必须 >=2）/ Total number of endpoints
   * (including EP0, must >=2)
   *
   * @note 端点对象本身由后端通过 Put() 填入；该参数仅用于合法性校验。
   *       Endpoint objects are populated by the backend via Put(); this argument is only
   *       used for a validity check.
   */
  explicit EndpointPool(size_t endpoint_num);

  EndpointPool(const EndpointPool&) = delete;
  EndpointPool& operator=(const EndpointPool&) = delete;

  /**
   * @brief 登记一个端点到池中 / Register an endpoint into the pool
   *
   * 按端点自身的号（GetNumber）与可用方向（AvailableDirection）入位。端点 0 不应
   * 通过此接口登记（应使用 SetEndpoint0）。
   * Registered by the endpoint's own number (GetNumber) and available direction
   * (AvailableDirection). Endpoint 0 must not be registered here (use SetEndpoint0).
   *
   * @param ep 待登记的端点对象指针 / Endpoint pointer to register
   * @retval ErrorCode 操作结果（OK/ARG_ERR/FULL）/ Operation result
   */
  ErrorCode Put(Endpoint* ep);

  /**
   * @brief 分配端点 / Allocate endpoint
   *
   * 按指定端点号与方向精确匹配一个已登记且空闲的端点。调用方必须显式指定端点号，
   * 不再支持自动分配。
   * Precisely matches a registered, available endpoint by the given number and direction.
   * The caller must specify the endpoint number explicitly; auto-allocation is no longer
   * supported.
   *
   * @param[out] ep_info 分配得到的端点对象指针 / Allocated endpoint pointer
   * @param direction 端点方向（IN/OUT）/ Endpoint direction
   * @param ep_num 指定端点号（必填）/ Endpoint number (required)
   * @retval ErrorCode 操作结果（OK/NOT_FOUND）/ Operation result
   */
  ErrorCode Get(Endpoint*& ep_info, Endpoint::Direction direction,
                Endpoint::EPNumber ep_num);

  /**
   * @brief 回收端点 / Release endpoint
   * @param ep_info 待回收的端点对象指针 / Endpoint pointer to release
   * @retval ErrorCode 操作结果（OK/NOT_FOUND）/ Operation result
   */
  ErrorCode Release(Endpoint* ep_info);

  /**
   * @brief 查找端点/ Lookup endpoint
   *
   * 端点 0 走 ep0_in_/ep0_out_ 旁路；其余端点仅在「已分配（in-use）」状态下可被查到，
   * 与原实现语义一致（未分配端点收到端点级请求视为异常）。
   * Endpoint 0 goes through the ep0_in_/ep0_out_ bypass; other endpoints are visible only
   * while allocated (in-use), matching the original semantics (an endpoint-level request
   * for an unallocated endpoint is treated as an error).
   *
   * @param ep_addr 端点地址，IN端点高位需加0x80 / Endpoint address (0x80 for IN
   * endpoints)
   * @param[out] ans 查找到的端点对象指针 / Found endpoint pointer
   * @retval ErrorCode 操作结果（OK/NOT_FOUND）/ Operation result
   */
  ErrorCode FindEndpoint(uint8_t ep_addr, Endpoint*& ans);

  /**
   * @brief 获取端点0的OUT对象 / Get Endpoint 0's OUT object
   * @return Endpoint* 端点0 OUT对象指针 / Endpoint 0 OUT pointer
   */
  Endpoint* GetEndpoint0Out();

  /**
   * @brief 获取端点0的IN对象 / Get Endpoint 0's IN object
   * @return Endpoint* 端点0 IN对象指针 / Endpoint 0 IN pointer
   */
  Endpoint* GetEndpoint0In();

  /**
   * @brief 设置端点0的IN/OUT对象 / Set Endpoint 0 IN/OUT objects
   * @param ep0_in 端点0 IN对象指针 / Endpoint 0 IN pointer
   * @param ep0_out 端点0 OUT对象指针 / Endpoint 0 OUT pointer
   */
  void SetEndpoint0(Endpoint* ep0_in, Endpoint* ep0_out);

  using HardwareEnter = uintptr_t (*)(void*);
  using HardwareLeave = void (*)(void*, uintptr_t);
  void SetHardwareGuard(void* context, HardwareEnter enter, HardwareLeave leave)
  {
    hardware_context_ = context;
    hardware_enter_ = enter;
    hardware_leave_ = leave;
  }

  // Short hardware/control-mailbox critical region; never class callbacks or
  // blocking work. Backends mask local IRQs and, on SMP, serialize register access.
  class HardwareScope
  {
   public:
    explicit HardwareScope(EndpointPool* pool) : pool_(pool)
    {
      if (pool_ && pool_->hardware_enter_)
        token_ = pool_->hardware_enter_(pool_->hardware_context_);
    }
    ~HardwareScope() { Release(); }
    HardwareScope(const HardwareScope&) = delete;
    HardwareScope& operator=(const HardwareScope&) = delete;
    void Release()
    {
      if (pool_ && pool_->hardware_leave_)
        pool_->hardware_leave_(pool_->hardware_context_, token_);
      pool_ = nullptr;
    }

   private:
    EndpointPool* pool_;
    uintptr_t token_ = 0;
  };

  // Capture the complete raw IRQ batch and clear W1C sources before callbacks can
  // rearm the same hardware. Exiting this scope releases the hardware guard BEFORE
  // entering ordinary controller/class work; it does not add a worker thread.
  class InterruptScope
  {
   public:
    explicit InterruptScope(EndpointPool& pool) : pool_(pool), hardware_(&pool)
    {
      pool_.interrupt_depth_.fetch_add(1U, std::memory_order_acq_rel);
    }
    ~InterruptScope()
    {
      hardware_.Release();
      if (pool_.interrupt_depth_.fetch_sub(1U, std::memory_order_acq_rel) == 1U)
        pool_.RequestService(true);
    }
    InterruptScope(const InterruptScope&) = delete;
    InterruptScope& operator=(const InterruptScope&) = delete;

   private:
    EndpointPool& pool_;
    HardwareScope hardware_;
  };

  /// Invoke synchronously if free; otherwise retain work for the current owner.
  void RequestService(bool in_isr);
  void SetControlHandler(Callback<uint32_t> callback) { control_handler_ = callback; }
  // Publish atomically with a hardware/mailbox change, then RequestService after
  // releasing the short guard. Publication itself never invokes callbacks.
  void PublishControl(uint32_t events)
  {
    control_pending_.fetch_or(events, std::memory_order_release);
  }
  void PostControl(uint32_t events, bool in_isr);
  bool HasPendingControl(uint32_t mask = UINT32_MAX) const
  {
    return (control_pending_.load(std::memory_order_acquire) & mask) != 0U;
  }
  bool CurrentContextIsISR() const { return current_in_isr_; }
  bool DataEnabled() const { return data_enabled_; }
  void SetDataEnabled(bool enabled) { data_enabled_ = enabled; }
  bool IsPlanning() const { return planning_; }
  void SetPlanning(bool planning) { planning_ = planning; }
  Speed GetSpeed() const { return speed_; }
  void SetSpeed(Speed speed) { speed_ = speed; }
  bool ConfigurationValid() const;
  bool IsSuspended() const { return suspended_; }
  void SetSuspended(bool suspended, bool in_isr);

 private:
  /// 方向数量（OUT=0, IN=1）/ Number of direction slots (OUT=0, IN=1)
  static constexpr size_t DIR_COUNT = 2;
  /// 索引数组容量（端点号 0..EP_MAX_NUM-1）/ Index array size (endpoint numbers)
  static constexpr size_t SLOT_COUNT =
      static_cast<size_t>(Endpoint::EPNumber::EP_MAX_NUM);

  /// 槽占用状态 / Slot usage state
  enum class SlotUse : uint8_t
  {
    AVAILABLE,  ///< 已登记、空闲可分配 / Registered and free to allocate
    IN_USE      ///< 已分配、正在使用 / Allocated and in use
  };

  /**
   * @brief 方向枚举转索引下标 / Convert direction enum to array index
   * @param dir 端点方向（IN/OUT）/ Endpoint direction
   * @return size_t 下标（OUT=0, IN=1）/ Index (OUT=0, IN=1)
   */
  static size_t DirIndex(Endpoint::Direction dir)
  {
    return (dir == Endpoint::Direction::IN) ? 1U : 0U;
  }

  Endpoint* slots_[SLOT_COUNT][DIR_COUNT] = {};  ///< [端点号][方向] 端点指针 / [num][dir]
  SlotUse use_[SLOT_COUNT][DIR_COUNT] = {};  ///< 对应槽占用状态 / Per-slot usage

  void* hardware_context_ = nullptr;
  HardwareEnter hardware_enter_ = nullptr;
  HardwareLeave hardware_leave_ = nullptr;
  std::atomic<uint32_t> interrupt_depth_{0};
  SerializedService service_;
  std::atomic<uint32_t> control_pending_{0};
  Callback<uint32_t> control_handler_;
  bool current_in_isr_ = false;
  bool data_enabled_ = true;
  bool planning_ = false;
  Speed speed_ = Speed::FULL;
  bool suspended_ = false;       ///< Controller-owner state.
  Endpoint* ep0_in_ = nullptr;   ///< 端点0 IN对象 / Endpoint 0 IN pointer
  Endpoint* ep0_out_ = nullptr;  ///< 端点0 OUT对象 / Endpoint 0 OUT pointer
};

}  // namespace LibXR::USB
