#pragma once

#include "flag.hpp"

namespace LibXR::USB
{

/**
 * @brief CDC UART TX 路径的轻量状态集合。
 * @brief Lightweight state holder for the CDC UART TX path.
 *
 * 该类型只集中保存 TX 路径自己的控制标志，不持有 endpoint 或队列对象。
 * This type only stores TX-path control flags; it does not own endpoints or queues.
 */
class CDCUartTxState final
{
 public:
  /**
   * @brief 构造 TX completion 作用域标志。
   * @brief Create the scoped flag used by TX completion handling.
   */
  Flag::ScopedRestore<Flag::Plain> EnterCompletion()
  {
    return Flag::ScopedRestore<Flag::Plain>(in_completion_);
  }

  /**
   * @brief 判断当前是否正在 TX completion 回调中。
   * @brief Check whether TX completion handling is currently active.
   */
  bool InCompletion() const { return in_completion_.IsSet(); }

  /**
   * @brief 清除待发送 ZLP 标志。
   * @brief Clear the pending-ZLP flag.
   */
  void ClearZlp() { need_zlp_ = false; }

  /**
   * @brief 记录需要在后续 completion 中发送 ZLP。
   * @brief Record that a ZLP must be sent by a later completion path.
   */
  void RequestZlp() { need_zlp_ = true; }

  /**
   * @brief 判断是否存在待发送 ZLP。
   * @brief Check whether a ZLP is pending.
   */
  bool NeedZlp() const { return need_zlp_; }

 private:
  Flag::Plain in_completion_;  ///< TX completion 回调保护标志。 TX completion guard flag.
  bool need_zlp_ = false;      ///< ZLP 需求标志。 ZLP required flag.
};

}  // namespace LibXR::USB
