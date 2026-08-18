#pragma once

#include <cstddef>
#include <cstdint>

#include "libxr_type.hpp"
#include "usb/core/desc_cfg.hpp"
#include "usb_execution_policy.hpp"

namespace LibXR::USB
{
class DeviceComposition;
class DeviceCore;

/**
 * @brief USB 设备类接口基类 / USB device class interface base
 *
 * 所有自定义 USB 类（HID/CDC/MSC 等）应派生自本类。
 * All custom USB classes (HID/CDC/MSC, etc.) should derive from this class.
 */
class DeviceClass : public ConfigDescriptorItem
{
 public:
  /**
   * @brief 默认构造 / Default constructor
   */
  DeviceClass() = default;

  /**
   * @brief 返回本类暴露的第 N 个接口字符串
   *        Return the string for the Nth local interface exposed by this class.
   *
   * @param local_interface_index 类内局部接口序号 / Class-local interface index
   * @return UTF-8 字符串，返回 nullptr 表示不提供 / UTF-8 string, or nullptr if unused
   */
  virtual const char* GetInterfaceString(size_t local_interface_index) const
  {
    UNUSED(local_interface_index);
    return nullptr;
  }

  /**
   * @brief 返回本类提供的 BOS capability 数量
   *        Return the number of BOS capabilities exposed by this class.
   */
  size_t GetBosCapabilityCount() override { return 0u; }

  /**
   * @brief 返回指定 BOS capability
   *        Return the BOS capability at the given index.
   */
  BosCapability* GetBosCapability(size_t index) override
  {
    UNUSED(index);
    return nullptr;
  }

 protected:
  /** @return 主机是否选择了非零 configuration / Whether the host selected a nonzero
   * configuration. */
  [[nodiscard]] bool DeviceConfigured() const noexcept
  {
    ASSERT(runtime_state_ != nullptr);
    return runtime_state_->configured;
  }

  /** @return 当前 device lifecycle generation / Current device lifecycle generation. */
  [[nodiscard]] uint32_t DeviceGeneration() const noexcept
  {
    ASSERT(runtime_state_ != nullptr);
    return runtime_state_->generation;
  }

  /** @return 当前 generation 是否已 fail-stop / Whether the current generation is
   * fail-stopped. */
  [[nodiscard]] bool DeviceGenerationFatal() const noexcept
  {
    ASSERT(runtime_state_ != nullptr);
    return runtime_state_->fatal;
  }

  /** @brief 将当前 generation 标记为 fail-stop / Fail-stop the current generation. */
  void MarkDeviceGenerationFatal() noexcept
  {
    ASSERT(runtime_state_ != nullptr);
    ASSERT(runtime_state_->configured);
    runtime_state_->fatal = true;
  }

  /**
   * @brief 发布本 class 的合并 work / Publish coalesced work for this class
   */
  void RequestPendingWork(bool in_isr) noexcept
  {
    ASSERT(execution_policy_ != nullptr);
    execution_policy_->NotifyWork(in_isr);
  }

  /**
   * @brief 由所属 device owner 推进本 class 的本地事件 / Advance this class's local
   * events under the owning device owner
   */
  virtual void ProcessPendingWork(bool in_isr) noexcept { UNUSED(in_isr); }

  /**
   * @brief ENDPOINT_HALT 成功清除后通知端点所属 class
   *        Notify the owning class after ENDPOINT_HALT is cleared successfully.
   *
   * @param in_isr  是否在 ISR 上下文 / Whether in ISR context
   * @param ep_addr 已解除 HALT 的端点地址 / Endpoint address whose halt was cleared
   */
  virtual void OnEndpointHaltCleared(bool in_isr, uint8_t ep_addr)
  {
    UNUSED(in_isr);
    UNUSED(ep_addr);
  }

  /**
   * @brief 绑定所属 device 的唯一执行策略 / Bind the owning device's sole execution
   * policy
   */
  void SetExecutionPolicy(USBExecutionPolicy& policy) noexcept
  {
    ASSERT(execution_policy_ == nullptr || execution_policy_ == &policy);
    execution_policy_ = &policy;
  }

  /**
   * @brief 返回已分配的接口字符串索引
   *        Return the assigned USB string index for a local interface.
   *
   * @param local_interface_index 类内局部接口序号 / Class-local interface index
   * @return USB 字符串索引；0 表示未分配 / USB string index; 0 means unassigned
   */
  [[nodiscard]] uint8_t GetInterfaceStringIndex(size_t local_interface_index) const;

  /**
   * @brief 控制请求（Class/Vendor）处理结果 / Control request (Class/Vendor) handling
   * result
   *
   */
  struct ControlTransferResult
  {
    RawData read_data{nullptr, 0};  ///< OUT 数据阶段接收缓冲区（Host->Device）/ OUT data
                                    ///< stage buffer (Host->Device)
    ConstRawData write_data{nullptr, 0};  ///< IN 数据阶段发送数据（Device->Host）/ IN
                                          ///< data stage payload (Device->Host)
    bool read_zlp = false;   ///< 期望 STATUS OUT（arm OUT 等待 ZLP）/ Expect STATUS OUT
                             ///< (arm OUT for ZLP)
    bool write_zlp = false;  ///< 发送 STATUS IN（发送 ZLP）/ Send STATUS IN (send ZLP)

    RawData& OutData() { return read_data; }
    const RawData& OutData() const { return read_data; }

    ConstRawData& InData() { return write_data; }
    const ConstRawData& InData() const { return write_data; }

    bool& ExpectStatusOutZLP() { return read_zlp; }
    bool ExpectStatusOutZLP() const { return read_zlp; }

    bool& SendStatusInZLP() { return write_zlp; }
    bool SendStatusInZLP() const { return write_zlp; }
  };

  /**
   * @brief 处理标准请求 GET_DESCRIPTOR（类特定描述符）
   *        Handle standard GET_DESCRIPTOR request (class-specific descriptors).
   *
   * @param in_isr   是否在 ISR / Whether in ISR context
   * @param bRequest 请求码 / Request code
   * @param wValue   wValue / wValue
   * @param wLength  wLength / wLength
   * @param out_data 输出：返回给主机的描述符数据（Device->Host）
   *                 Output: descriptor data to return (Device->Host)
   * @return 错误码 / Error code
   */
  virtual ErrorCode OnGetDescriptor(bool in_isr, uint8_t bRequest, uint16_t wValue,
                                    uint16_t wLength, ConstRawData& out_data)
  {
    UNUSED(in_isr);
    UNUSED(bRequest);
    UNUSED(wValue);
    UNUSED(wLength);
    UNUSED(out_data);
    return ErrorCode::NOT_SUPPORT;
  }

  /**
   * @brief 处理 Class-specific 请求（Setup stage）/ Handle class-specific request (Setup
   * stage)
   *
   * @param in_isr   是否在 ISR / Whether in ISR context
   * @param bRequest 请求码 / Request code
   * @param wValue   wValue / wValue
   * @param wLength  wLength / wLength
   * @param wIndex   wIndex / wIndex
   * @param result   输出：控制传输结果 / Output: control transfer result
   * @return 错误码 / Error code
   */
  virtual ErrorCode OnClassRequest(bool in_isr, uint8_t bRequest, uint16_t wValue,
                                   uint16_t wLength, uint16_t wIndex,
                                   ControlTransferResult& result)
  {
    UNUSED(in_isr);
    UNUSED(bRequest);
    UNUSED(wValue);
    UNUSED(wLength);
    UNUSED(wIndex);
    UNUSED(result);
    return ErrorCode::NOT_SUPPORT;
  }

  /**
   * @brief 处理 Class request 数据阶段 / Handle class request data stage
   *
   * @param in_isr   是否在 ISR / Whether in ISR context
   * @param bRequest 请求码 / Request code
   * @param data     数据阶段数据 / Data stage payload
   * @return 错误码 / Error code
   *
   * @note 当 OnClassRequest 返回需要 OUT/IN data stage 时，数据阶段完成后回调此函数。
   *       When OnClassRequest requires an OUT/IN data stage, this callback is invoked
   * after completion.
   */
  virtual ErrorCode OnClassData(bool in_isr, uint8_t bRequest, LibXR::ConstRawData& data)
  {
    UNUSED(in_isr);
    UNUSED(bRequest);
    UNUSED(data);
    return ErrorCode::NOT_SUPPORT;
  }

  /**
   * @brief 类请求的 IN 数据阶段在 STATUS OUT 完成后回调
   *        Called after the STATUS OUT completes for a Class IN data request.
   *
   * @param in_isr   是否在 ISR / Whether in ISR context
   * @param bRequest 请求码 / Request code
   */
  virtual void OnClassInDataStatusComplete(bool in_isr, uint8_t bRequest)
  {
    UNUSED(in_isr);
    UNUSED(bRequest);
  }

  /**
   * @brief 处理 Vendor request（Setup stage）/ Handle vendor request (Setup stage)
   *
   * @param in_isr   是否在 ISR / Whether in ISR context
   * @param bRequest 请求码 / Request code
   * @param wValue   wValue / wValue
   * @param wLength  wLength / wLength
   * @param wIndex   wIndex / wIndex
   * @param result   输出：控制传输结果 / Output: control transfer result
   * @return 错误码 / Error code
   */
  virtual ErrorCode OnVendorRequest(bool in_isr, uint8_t bRequest, uint16_t wValue,
                                    uint16_t wLength, uint16_t wIndex,
                                    ControlTransferResult& result)
  {
    UNUSED(in_isr);
    UNUSED(bRequest);
    UNUSED(wValue);
    UNUSED(wLength);
    UNUSED(wIndex);
    UNUSED(result);
    return ErrorCode::NOT_SUPPORT;
  }

 private:
  struct RuntimeState
  {
    uint32_t generation = 0U;
    bool configured = false;
    bool fatal = false;
  };

  // These helpers are driven by DeviceComposition during initialization-time string
  // registration and are not part of the public class contract.
  // 这些辅助函数只在初始化期由 DeviceComposition 调用，不属于对外类接口。
  void SetInterfaceStringBaseIndex(uint8_t string_index);

  void SetRuntimeState(RuntimeState& state) noexcept
  {
    ASSERT(runtime_state_ == nullptr || runtime_state_ == &state);
    runtime_state_ = &state;
  }

  friend class DeviceComposition;
  friend class DeviceCore;

  uint8_t interface_string_base_index_ =
      0u;  ///< 首个接口字符串索引 / First interface string index
  USBExecutionPolicy* execution_policy_ =
      nullptr;  ///< 所属 device owner / Owning device execution policy
  RuntimeState* runtime_state_ =
      nullptr;  ///< 所属 device 运行态 / Owning device runtime state
};

}  // namespace LibXR::USB
