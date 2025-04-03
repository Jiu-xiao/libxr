#pragma once

#include <algorithm>
#include <cmath>

#include "cycle_value.hpp"
#include "libxr_def.hpp"

namespace LibXR
{
using DefaultScalar = LIBXR_DEFAULT_SCALAR;

constexpr DefaultScalar PID_SIGMA = 1e-6f;

/**
 * @brief 通用 PID 控制器类。
 *        Generic PID controller.
 *
 * 支持周期角度处理、积分限幅与输出限幅，适用于基础控制应用。
 * Supports cyclic inputs, integral/output saturation. No derivative filtering.
 *
 * @tparam Scalar 控制器标量类型，默认为 DefaultScalar。
 *                Scalar type for internal calculations.
 */
template <typename Scalar = DefaultScalar>
class PID
{
 public:
  /**
   * @brief PID 参数结构体。
   *        Structure holding PID parameters.
   */
  struct Param
  {
    Scalar k = 1.0;          ///< 全局比例因子 Global gain
    Scalar p = 0.0;          ///< 比例项 Proportional gain
    Scalar i = 0.0;          ///< 积分项 Integral gain
    Scalar d = 0.0;          ///< 微分项 Derivative gain
    Scalar i_limit = 0.0;    ///< 积分限幅 Integral limit
    Scalar out_limit = 0.0;  ///< 输出限幅 Output limit
    bool cycle = false;      ///< 是否处理周期误差 Whether input is cyclic
  };

  /**
   * @brief 构造 PID 控制器。
   *        Construct a PID controller.
   *
   * @param param PID 参数结构体。
   *              PID parameter struct.
   */
  PID(const Param PARAM) : param_(std::move(PARAM)) { Reset(); }

  /**
   * @brief 使用反馈值计算 PID 输出。
   *        Compute output from feedback only.
   *        out = k(p*err + i*∫err*dt - d*(fb-last_fb)/dt)
   *
   * @param sp 期望值 Setpoint
   * @param fb 反馈值 Feedback
   * @param dt 控制周期（秒） Time step in seconds
   * @return 控制器输出 Controller output
   */
  Scalar Calculate(Scalar sp, Scalar fb, Scalar dt)
  {
    if (!std::isfinite(sp) || !std::isfinite(fb) || !std::isfinite(dt))
    {
      return last_out_;
    }

    // Compute error
    Scalar err = param_.cycle ? CycleValue<Scalar>(sp) - fb : sp - fb;
    Scalar k_err = err * param_.k;

    // Derivative from feedback change
    fb *= param_.k;
    Scalar d = (fb - last_fb_) / dt;
    if (!std::isfinite(d))
    {
      d = 0;
    }

    // Compute PD
    Scalar output = (k_err * param_.p) - (d * param_.d);

    // Integrate if within limits
    Scalar i_term = i_ + k_err * dt;
    Scalar i_out = i_term * param_.i;

    if (param_.i > PID_SIGMA && std::isfinite(i_term))
    {
      if (std::abs(output + i_out) <= param_.out_limit &&
          std::abs(i_term) <= param_.i_limit)
      {
        i_ = i_term;
      }
    }

    // Apply output limits
    output += i_out;
    output += feed_forward_;
    if (std::isfinite(output) && param_.out_limit > PID_SIGMA)
    {
      output = std::clamp(output, -param_.out_limit, param_.out_limit);
    }

    // Store states
    last_err_ = err;
    last_fb_ = fb;
    last_out_ = output;
    last_der_ = d;
    return output;
  }

  /**
   * @brief 使用外部导数计算 PID 输出。
   *        Compute output using external feedback derivative.
   *        out = k(p*err + i*∫err*dt - d*fb_dot*dt)
   *
   * @param sp 期望值 Setpoint
   * @param fb 反馈值 Feedback
   * @param fb_dot 反馈导数 Feedback rate
   * @param dt 控制周期 Delta time
   * @return 控制器输出 Controller output
   */
  Scalar Calculate(Scalar sp, Scalar fb, Scalar fb_dot, Scalar dt)
  {
    if (!std::isfinite(sp) || !std::isfinite(fb) || !std::isfinite(fb_dot) ||
        !std::isfinite(dt))
    {
      return last_out_;
    }

    // Compute error
    Scalar err = param_.cycle ? CycleValue<Scalar>(sp) - fb : sp - fb;
    Scalar k_err = err * param_.k;

    // Use externally provided derivative
    Scalar d = fb_dot;
    if (!std::isfinite(d))
    {
      d = 0;
    }

    // Compute PD
    Scalar output = (k_err * param_.p) - (d * param_.d);

    // Integrate if within limits
    Scalar i_term = i_ + k_err * dt;
    Scalar i_out = i_term * param_.i;

    if (param_.i > PID_SIGMA && std::isfinite(i_term))
    {
      if (std::abs(output + i_out) <= param_.out_limit &&
          std::abs(i_term) <= param_.i_limit)
      {
        i_ = i_term;
      }
    }

    // Apply output limits
    output += i_out;
    output += feed_forward_;
    if (std::isfinite(output) && param_.out_limit > PID_SIGMA)
    {
      output = std::clamp(output, -param_.out_limit, param_.out_limit);
    }

    // Store states
    last_err_ = err;
    last_fb_ = fb;
    last_out_ = output;
    last_der_ = d;
    return output;
  }

  /// 设置全局比例系数 Set global proportional gain
  void SetK(Scalar k) { param_.k = k; }
  /// 设置 P 项系数 Set proportional gain
  void SetP(Scalar p) { param_.p = p; }
  /// 设置 I 项系数 Set integral gain
  void SetI(Scalar i) { param_.i = i; }
  /// 设置 D 项系数 Set derivative gain
  void SetD(Scalar d) { param_.d = d; }
  /// 设置积分限幅 Set integral limit
  void SetILimit(Scalar limit) { param_.i_limit = limit; }
  /// 设置输出限幅 Set output limit
  void SetOutLimit(Scalar limit) { param_.out_limit = limit; }

  /// 获取全局比例系数 Get global proportional gain
  Scalar K() const { return param_.k; }
  /// 获取 P 项系数 Get proportional gain
  Scalar P() const { return param_.p; }
  /// 获取 I 项系数 Get integral gain
  Scalar I() const { return param_.i; }
  /// 获取 D 项系数 Get derivative gain
  Scalar D() const { return param_.d; }
  /// 获取积分限幅 Get integral limit
  Scalar ILimit() const { return param_.i_limit; }
  /// 获取输出限幅 Get output limit
  Scalar OutLimit() const { return param_.out_limit; }
  /// 获取上一次误差 Get last error
  Scalar LastError() const { return last_err_; }
  /// 获取上一次反馈值 Get last feedback
  Scalar LastFeedback() const { return last_fb_; }
  /// 获取上一次输出 Get last output
  Scalar LastOutput() const { return last_out_; }
  /// 获取上一次导数 Get last derivative
  Scalar LastDerivative() const { return last_der_; }

  /**
   * @brief 重置控制器状态。
   *        Reset all internal states.
   */
  void Reset()
  {
    i_ = 0;
    last_err_ = 0;
    last_fb_ = 0;
    last_out_ = 0;
  }

  /**
   * @brief 设置累计误差 Set integral error
   *
   * @param err 累计误差 Integral error
   */
  void SetIntegralError(Scalar err) { i_ = err; }

  /**
   * @brief 获取累计误差 Get integral error
   *
   * @return 累计误差 Integral error
   */
  Scalar GetIntegralError() const { return i_; }

  /**
   * @brief 设置前馈项 Set feedforward
   *
   * @param feed_forward 前馈项 Feedforward
   */
  void SetFeedForward(Scalar feed_forward) { feed_forward_ = feed_forward; }

  /**
   * @brief 获取前馈项 Get feedforward
   *
   * @return 前馈项 Feedforward
   *
   */
  Scalar GetFeedForward() const { return feed_forward_; }

 private:
  Param param_;              ///< PID 参数 PID parameter set
  Scalar i_ = 0;             ///< 积分状态 Integral state
  Scalar last_err_ = 0;      ///< 上次误差 Last error
  Scalar last_fb_ = 0;       ///< 上次反馈 Last feedback
  Scalar last_der_ = 0;      ///< 上次导数 Last derivative
  Scalar last_out_ = 0;      ///< 上次输出 Last output
  Scalar feed_forward_ = 0;  ///< 前馈项 Feedforward term
};

}  // namespace LibXR
