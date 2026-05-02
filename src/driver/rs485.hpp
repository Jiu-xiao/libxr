#pragma once

#include <cstddef>
#include <cstdint>

#include "libxr.hpp"
#include "libxr_rw.hpp"
#include "uart.hpp"

namespace LibXR
{

/**
 * @class RS485
 * @brief Single-controller framed RS485-like bus.
 *        单主机帧式 RS485 类总线。
 *
 * No multi-master arbitration is provided. Frame size and RX buffering are
 * derived-driver resources.
 */
class RS485
{
 public:
  /**
   * @struct Configuration
   * @brief Runtime bus parameters. Topology and DE backend are driver-defined.
   *        运行时总线参数；拓扑和 DE 后端由驱动决定。
   */
  struct Configuration
  {
    uint32_t baudrate = 115200;
    UART::Parity parity = UART::Parity::NO_PARITY;
    /// Byte-frame mode supports 5..8 data bits. 9-bit address/word modes need a
    /// derived-driver API because `Write(ConstRawData)` is byte-oriented.
    uint8_t data_bits = 8;
    uint8_t stop_bits = 1;
    bool tx_active_level = true;
    uint32_t assert_time_us = 0;
    uint32_t deassert_time_us = 0;
  };

  /// Raw-frame callback type. / 原始帧回调类型。
  using Callback = LibXR::Callback<ConstRawData>;

  /**
   * @struct Filter
   * @brief Match a byte pattern at a fixed raw-frame offset.
   *        在原始帧固定偏移处匹配一段字节。
   *
   * Registered subscriptions are permanent. Register() copies pattern/mask data
   * into permanent subscription storage and does not provide unregister.
   */
  struct Filter
  {
    size_t offset = 0;
    ConstRawData data;
    ConstRawData mask;

    [[nodiscard]] bool Match(ConstRawData frame) const;
  };

  RS485() = default;
  virtual ~RS485() = default;

  /**
   * @brief Configure runtime bus parameters. / 配置运行时总线参数。
   */
  virtual ErrorCode SetConfig(const Configuration& config) = 0;

  /**
   * @brief Write one complete bus frame.
   *        发送一帧完整总线数据。
   *
   * Completion means the last stop bit has left the line and direction has been
   * released when applicable. Blocking timeout is carried by `op`.
   */
  virtual ErrorCode Write(ConstRawData frame, WriteOperation& op,
                          bool in_isr = false) = 0;

  /**
   * @brief Abort/reset current TX, RX and parser state.
   *        打断并重置当前发送、接收和解析状态。
   */
  virtual void Reset() = 0;

  /**
   * @brief Register a raw-frame callback. / 注册原始帧回调。
   */
  ErrorCode Register(Callback cb);

  /**
   * @brief Register a raw-frame callback with a filter.
   *        按过滤器注册原始帧回调。
   */
  ErrorCode Register(Callback cb, const Filter& filter);

 protected:
  /**
   * @brief Dispatch one raw frame to matching subscribers.
   *        将一帧原始数据分发给匹配订阅者。
   *
   * `frame` is only valid during callback execution unless the derived driver
   * documents a longer lifetime.
   */
  void OnFrame(ConstRawData frame, bool in_isr);

 private:
  struct Subscription
  {
    Filter filter;
    Callback cb;
  };

  LockFreeList subscriber_list_;
};

}  // namespace LibXR
