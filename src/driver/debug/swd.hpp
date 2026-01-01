#pragma once

#include <cstdint>

#include "libxr_def.hpp"
#include "libxr_type.hpp"

namespace LibXR::Debug
{

/**
 * @class Swd
 * @brief SWD 链路基类 / SWD link base class
 *
 * 用于描述与操作 SWD（Serial Wire Debug）链路，提供标准 SWD 进入流程、line reset、
 * 单次传输（transfer）以及 DP 寄存器便捷访问接口。
 * Used for describing and manipulating an SWD (Serial Wire Debug) link, providing
 * standard SWD entry sequence, line reset, single transfer primitive, and DP
 * convenience APIs.
 */
class Swd
{
 public:
  /**
   * @brief SWD 访问端口选择 / SWD access port select
   */
  enum class Port : uint8_t
  {
    DP = 0,  ///< Debug Port / 调试端口
    AP = 1   ///< Access Port / 访问端口
  };

  /**
   * @brief SWD ACK 值 / SWD ACK value
   *
   * ADIv5 SWD ACK:
   * - OK    = 0b001
   * - WAIT  = 0b010
   * - FAULT = 0b100
   * Other values are usually treated as protocol/line errors.
   */
  enum class Ack : uint8_t
  {
    NO_ACK = 0x0,    ///< No acknowledge (e.g. line error) / 无应答（例如链路错误）
    OK = 0x1,        ///< OK / 正常
    WAIT = 0x2,      ///< WAIT / 等待
    FAULT = 0x4,     ///< FAULT / 故障
    PROTOCOL = 0x7,  ///< Protocol/invalid / 协议异常或非法应答
  };

  /**
   * @brief 链路状态 / Link state
   */
  enum class State : uint8_t
  {
    DISABLED,  ///< 禁用 / Disabled
    IDLE,      ///< 空闲 / Idle
    BUSY,      ///< 忙 / Busy
    ERROR,     ///< 错误 / Error
  };

  /**
   * @brief 单次传输请求 / Single transfer request
   *
   * addr2b: A[3:2] two-bit address encoding (0..3).
   */
  struct Request
  {
    Port port = Port::DP;  ///< DP/AP 选择 / DP/AP selection
    bool rnw = true;       ///< Read=1 / Write=0
    uint8_t addr2b = 0;    ///< A[3:2] encoded as 0..3
    uint32_t wdata = 0;  ///< 写数据（rnw=false 有效） / Write data (valid when rnw=false)
  };

  /**
   * @brief 单次传输响应 / Single transfer response
   */
  struct Response
  {
    Ack ack = Ack::PROTOCOL;  ///< 协议 ACK / SWD ACK
    uint32_t rdata = 0;  ///< 读数据（rnw=true 且 ack==OK 有效） / Read data (valid when
                         ///< rnw=true && ack==OK)
    bool parity_ok = true;  ///< 读数据奇偶校验是否通过 / Read data parity check result
  };

  /**
   * @brief DP 读寄存器编码（A[3:2]）/ DP read register encoding (A[3:2])
   *
   * 注意：addr2b=0 读为 IDCODE/DPIDR。
   * Note: addr2b=0 reads IDCODE/DPIDR.
   */
  enum class DpReadReg : uint8_t
  {
    IDCODE = 0,     ///< IDCODE/DPIDR / 读 IDCODE（或 DPIDR）
    CTRL_STAT = 1,  ///< CTRL/STAT / 控制与状态
    SELECT = 2,     ///< SELECT / 选择寄存器（多数实现支持读回）
    RDBUFF = 3,     ///< RDBUFF / 读缓冲
  };

  /**
   * @brief DP 写寄存器编码（A[3:2]）/ DP write register encoding (A[3:2])
   *
   * 注意：addr2b=0 写为 ABORT。
   * Note: addr2b=0 writes ABORT.
   */
  enum class DpWriteReg : uint8_t
  {
    ABORT = 0,      ///< ABORT / 清除 sticky 错误等
    CTRL_STAT = 1,  ///< CTRL/STAT / 控制与状态
    SELECT = 2,     ///< SELECT / 选择寄存器
  };

  /**
   * @brief 常用 DP ABORT 位定义 / Common DP ABORT bit definitions
   *
   */
  static constexpr uint32_t DP_ABORT_DAPABORT = (1u << 0);  ///< DAPABORT / 中止传输
  static constexpr uint32_t DP_ABORT_STKCMPCLR =
      (1u << 1);  ///< STKCMPCLR / 清 sticky compare
  static constexpr uint32_t DP_ABORT_STKERRCLR =
      (1u << 2);  ///< STKERRCLR / 清 sticky error
  static constexpr uint32_t DP_ABORT_WDERRCLR =
      (1u << 3);  ///< WDERRCLR / 清 write data error
  static constexpr uint32_t DP_ABORT_ORUNERRCLR =
      (1u << 4);  ///< ORUNERRCLR / 清 overrun error

  /**
   * @brief 常用 DP CTRL/STAT 位定义 / Common DP CTRL/STAT bit definitions
   */
  static constexpr uint32_t DP_CTRLSTAT_CDBGPWRUPREQ =
      (1u << 28);  ///< CDBGPWRUPREQ / Debug power up request
  static constexpr uint32_t DP_CTRLSTAT_CDBGPWRUPACK =
      (1u << 29);  ///< CDBGPWRUPACK / Debug power up acknowledge
  static constexpr uint32_t DP_CTRLSTAT_CSYSPWRUPREQ =
      (1u << 30);  ///< CSYSPWRUPREQ / System power up request
  static constexpr uint32_t DP_CTRLSTAT_CSYSPWRUPACK =
      (1u << 31);  ///< CSYSPWRUPACK / System power up acknowledge

  /**
   * @brief 生成 DP SELECT 寄存器值 / Build DP SELECT register value
   *
   * SELECT[31:24] = APSEL
   * SELECT[7:4]   = APBANKSEL
   * SELECT[3:0]   = DPBANKSEL
   *
   * @param apsel AP 选择 / AP select
   * @param apbanksel AP bank 选择 / AP bank select
   * @param dpbanksel DP bank 选择（通常为 0） / DP bank select (typically 0)
   * @return uint32_t SELECT 值 / SELECT value
   */
  static constexpr uint32_t MakeSelect(uint8_t apsel, uint8_t apbanksel,
                                       uint8_t dpbanksel = 0)
  {
    return (static_cast<uint32_t>(apsel) << 24) |
           ((static_cast<uint32_t>(apbanksel) & 0x0Fu) << 4) |
           (static_cast<uint32_t>(dpbanksel) & 0x0Fu);
  }

  /**
   * @brief 构造 DP 读请求 / Build DP read request
   * @param reg DP 读寄存器 / DP read register
   * @return Request 请求结构 / Request struct
   */
  static constexpr Request MakeDpReadReq(DpReadReg reg)
  {
    return Request{Port::DP, true, static_cast<uint8_t>(reg), 0u};
  }

  /**
   * @brief 构造 DP 写请求 / Build DP write request
   * @param reg DP 写寄存器 / DP write register
   * @param wdata 写数据 / Write data
   * @return Request 请求结构 / Request struct
   */
  static constexpr Request MakeDpWriteReq(DpWriteReg reg, uint32_t wdata)
  {
    return Request{Port::DP, false, static_cast<uint8_t>(reg), wdata};
  }

  /**
   * @brief 构造 AP 读请求 / Build AP read request
   * @param addr2b A[3:2] 编码（0..3） / A[3:2] encoding (0..3)
   * @return Request 请求结构 / Request struct
   */
  static constexpr Request MakeApReadReq(uint8_t addr2b)
  {
    return Request{Port::AP, true, static_cast<uint8_t>(addr2b & 0x03u), 0u};
  }

  /**
   * @brief 构造 AP 写请求 / Build AP write request
   * @param addr2b A[3:2] 编码（0..3） / A[3:2] encoding (0..3)
   * @param wdata 写数据 / Write data
   * @return Request 请求结构 / Request struct
   */
  static constexpr Request MakeApWriteReq(uint8_t addr2b, uint32_t wdata)
  {
    return Request{Port::AP, false, static_cast<uint8_t>(addr2b & 0x03u), wdata};
  }

  /**
   * @brief 事务层传输配置 / Transfer policy configuration
   *
   * 用于控制 WAIT 重试策略、idle clocks 插入以及故障恢复策略。
   * Used to control WAIT retry strategy, idle clocks insertion, and fault recovery
   * policy.
   */
  struct TransferPolicy
  {
    uint8_t idle_cycles = 0;  ///< WAIT 重试间插入的 idle clocks 数 / Idle clocks inserted
                              ///< between WAIT retries
    uint16_t wait_retry = 100;  ///< WAIT 最大重试次数 / Maximum WAIT retries
    bool clear_sticky_on_fault =
        true;  ///< FAULT 时清 sticky 错误 / Clear sticky errors on FAULT
  };

 public:
  /**
   * @brief 虚析构函数 / Virtual destructor
   */
  virtual ~Swd() = default;

  Swd(const Swd&) = delete;
  Swd& operator=(const Swd&) = delete;

  /**
   * @brief 获取链路状态 / Get link state
   * @return State 当前状态 / Current state
   */
  State GetState() const { return state_; }

  /**
   * @brief 设置事务层传输策略 / Set transfer policy
   * @param policy 传输策略 / Transfer policy
   */
  void SetTransferPolicy(const TransferPolicy& policy) { policy_ = policy; }

  /**
   * @brief 获取事务层传输策略 / Get transfer policy
   * @return TransferPolicy 当前策略 / Current policy
   */
  const TransferPolicy& GetTransferPolicy() const { return policy_; }

  /**
   * @brief 可选：设置 SWCLK 频率 / Optional: set SWCLK frequency
   *
   * @param hz 目标频率 / Target frequency
   * @return ErrorCode 错误码 / Error code
   */
  virtual ErrorCode SetClockHz(uint32_t hz)
  {
    (void)hz;
    return ErrorCode::NOT_SUPPORT;
  }

  /**
   * @brief 关闭链路 / Close the link
   */
  virtual void Close() = 0;

  /**
   * @brief 发送 line reset / Send line reset
   *
   * 标准要求：SWDIO=1，输出不少于 50 个 SWCLK。
   * Standard requires SWDIO=1 and at least 50 SWCLK cycles.
   *
   * @return ErrorCode 错误码 / Error code
   */
  virtual ErrorCode LineReset() = 0;

  /**
   * @brief 进入 SWD 模式 / Enter SWD mode
   *
   * 标准流程：LineReset -> 0xE79E -> LineReset -> optional idle。
   * Standard sequence: LineReset -> 0xE79E -> LineReset -> optional idle.
   *
   * @return ErrorCode 错误码 / Error code
   */
  virtual ErrorCode EnterSwd() = 0;

  /**
   * @brief 单次 SWD transfer（不做 WAIT 重试策略） / Single SWD transfer (no WAIT retry
   * policy)
   *
   * 返回值表示底层传输是否成功（SPI/USB/DMA 等）。
   * The return value indicates whether the underlying transport succeeded.
   *
   * @param req 请求 / Request
   * @param resp 响应 / Response
   * @return ErrorCode 错误码 / Error code
   */
  virtual ErrorCode Transfer(const Request& req, Response& resp) = 0;

  /**
   * @brief 带 WAIT 重试的 SWD transfer（事务层）/ SWD transfer with WAIT retry
   * (transaction layer)
   *
   * @param req 请求 / Request
   * @param resp 响应 / Response
   * @return ErrorCode::OK 仅在底层传输成功且最终 ACK == OK 时返回 OK
   *         Returns OK only if underlying transport succeeds and final ACK == OK.
   */
  ErrorCode TransferWithRetry(const Request& req, Response& resp)
  {
    ErrorCode ec = ErrorCode::OK;

    // 以 policy_.wait_retry 为上限重试 WAIT
    for (uint32_t attempt = 0; attempt < policy_.wait_retry; ++attempt)
    {
      ec = Transfer(req, resp);
      if (ec != ErrorCode::OK)
      {
        // 底层传输失败：链路/DP 状态不可信
        resp.ack = Ack::PROTOCOL;
        InvalidateSelectCache();
        return ec;
      }

      if (resp.ack == Ack::WAIT)
      {
        // 插入 idle clocks，再重试
        if (policy_.idle_cycles != 0u)
        {
          IdleClocks(policy_.idle_cycles);
        }
        continue;
      }

      // OK / FAULT / NO_ACK / PROTOCOL：结束重试
      break;
    }

    // 最终仍为 WAIT：视为失败
    if (resp.ack == Ack::WAIT)
    {
      InvalidateSelectCache();
      return ErrorCode::FAILED;
    }

    // FAULT 时清 sticky
    if (resp.ack == Ack::FAULT && policy_.clear_sticky_on_fault)
    {
      (void)ClearStickyErrors();
    }

    if (resp.ack == Ack::OK)
    {
      return ErrorCode::OK;
    }

    // 任意最终非 OK：不再信任 SELECT cache
    InvalidateSelectCache();
    return ErrorCode::FAILED;
  }

  /**
   * @brief DP 读寄存器（一次 transfer，不做重试策略）
   *        Read DP register (single transfer, no retry policy)
   *
   * @param reg DP 读寄存器 / DP read register
   * @param val 读出值（输出） / Read value (output)
   * @param ack ACK（输出） / ACK (output)
   * @return ErrorCode::OK 仅在 ack==OK 且 parity_ok==true 时返回 OK
   *         Returns OK only if ack==OK and parity_ok==true.
   */
  ErrorCode DpRead(DpReadReg reg, uint32_t& val, Ack& ack)
  {
    Response resp;
    const ErrorCode EC = Transfer(MakeDpReadReq(reg), resp);
    if (EC != ErrorCode::OK)
    {
      ack = Ack::PROTOCOL;
      return EC;
    }

    ack = resp.ack;

    if (resp.ack != Ack::OK)
    {
      return ErrorCode::FAILED;
    }

    if (!resp.parity_ok)
    {
      return ErrorCode::FAILED;
    }

    val = resp.rdata;
    return ErrorCode::OK;
  }

  /**
   * @brief DP 写寄存器（一次 transfer，不做重试策略）
   *        Write DP register (single transfer, no retry policy)
   *
   * @param reg DP 写寄存器 / DP write register
   * @param val 写入值 / Value to write
   * @param ack ACK（输出） / ACK (output)
   * @return ErrorCode::OK 仅在 ack==OK 时返回 OK
   *         Returns OK only if ack==OK.
   */
  ErrorCode DpWrite(DpWriteReg reg, uint32_t val, Ack& ack)
  {
    Response resp;
    const ErrorCode EC = Transfer(MakeDpWriteReq(reg, val), resp);
    if (EC != ErrorCode::OK)
    {
      ack = Ack::PROTOCOL;
      return EC;
    }

    ack = resp.ack;

    return (resp.ack == Ack::OK) ? ErrorCode::OK : ErrorCode::FAILED;
  }

  /**
   * @brief DP 读寄存器（事务层：WAIT 重试 + parity check）
   *        Read DP register (transaction layer: WAIT retry + parity check)
   *
   * @param reg DP 读寄存器 / DP read register
   * @param val 读出值（输出） / Read value (output)
   * @param ack ACK（输出） / ACK (output)
   * @return ErrorCode::OK 仅在 ack==OK 且 parity_ok==true 时返回 OK
   *         Returns OK only if ack==OK and parity_ok==true.
   */
  ErrorCode DpReadTxn(DpReadReg reg, uint32_t& val, Ack& ack)
  {
    Response resp;
    const ErrorCode EC = TransferWithRetry(MakeDpReadReq(reg), resp);
    if (EC != ErrorCode::OK)
    {
      ack = Ack::PROTOCOL;
      return EC;
    }

    ack = resp.ack;

    if (resp.ack != Ack::OK)
    {
      return ErrorCode::FAILED;
    }

    if (!resp.parity_ok)
    {
      return ErrorCode::FAILED;
    }

    val = resp.rdata;
    return ErrorCode::OK;
  }

  /**
   * @brief DP 写寄存器（事务层：WAIT 重试）
   *        Write DP register (transaction layer: WAIT retry)
   *
   * @param reg DP 写寄存器 / DP write register
   * @param val 写入值 / Value to write
   * @param ack ACK（输出） / ACK (output)
   * @return ErrorCode::OK 仅在 ack==OK 时返回 OK
   *         Returns OK only if ack==OK.
   */
  ErrorCode DpWriteTxn(DpWriteReg reg, uint32_t val, Ack& ack)
  {
    Response resp;
    const ErrorCode EC = TransferWithRetry(MakeDpWriteReq(reg, val), resp);
    if (EC != ErrorCode::OK)
    {
      ack = Ack::PROTOCOL;
      return EC;
    }

    ack = resp.ack;

    return (resp.ack == Ack::OK) ? ErrorCode::OK : ErrorCode::FAILED;
  }

  /**
   * @brief 读取 IDCODE/DPIDR / Read IDCODE/DPIDR
   * @param idcode 读出值（输出） / Read value (output)
   * @param ack ACK（输出） / ACK (output)
   * @return ErrorCode 错误码 / Error code
   */
  ErrorCode ReadIdCode(uint32_t& idcode, Ack& ack)
  {
    return DpRead(DpReadReg::IDCODE, idcode, ack);
  }

  /**
   * @brief 写 ABORT / Write ABORT
   * @param flags ABORT 标志 / ABORT flags
   * @param ack ACK（输出） / ACK (output)
   * @return ErrorCode 错误码 / Error code
   */
  ErrorCode WriteAbort(uint32_t flags, Ack& ack)
  {
    return DpWrite(DpWriteReg::ABORT, flags, ack);
  }

  /**
   * @brief 写 ABORT（事务层：WAIT 重试） / Write ABORT (transaction layer: WAIT retry)
   * @param flags ABORT 标志 / ABORT flags
   * @param ack ACK（输出） / ACK (output)
   * @return ErrorCode 错误码 / Error code
   */
  ErrorCode WriteAbortTxn(uint32_t flags, Ack& ack)
  {
    return DpWriteTxn(DpWriteReg::ABORT, flags, ack);
  }

  /**
   * @brief 事务层 AP 读（自动处理 posted read）/ Transaction-layer AP read (handles
   * posted read)
   *
   * @param addr2b A[3:2] 编码（0..3）/ A[3:2] encoding (0..3)
   * @param val 读出值（输出）/ Read value (output)
   * @param ack ACK（输出）/ ACK (output)
   * @return ErrorCode::OK 仅在最终 ack==OK 且 parity_ok==true 时返回 OK
   */
  ErrorCode ApReadTxn(uint8_t addr2b, uint32_t& val, Ack& ack)
  {
    // 1) 发起 AP read（posted）
    Response resp;
    ErrorCode ec = TransferWithRetry(MakeApReadReq(addr2b), resp);
    if (ec != ErrorCode::OK)
    {
      ack = Ack::PROTOCOL;
      return ec;
    }

    ack = resp.ack;
    if (resp.ack != Ack::OK)
    {
      return ErrorCode::FAILED;
    }

    // 2) 通过 DP RDBUFF 取回数据
    return DpReadTxn(DpReadReg::RDBUFF, val, ack);
  }

  /**
   * @brief 事务层 AP 写 / Transaction-layer AP write
   * @param addr2b A[3:2] 编码（0..3）/ A[3:2] encoding (0..3)
   * @param val 写入值 / Value to write
   * @param ack ACK（输出）/ ACK (output)
   * @return ErrorCode::OK 仅在最终 ack==OK 时返回 OK
   */
  ErrorCode ApWriteTxn(uint8_t addr2b, uint32_t val, Ack& ack)
  {
    Response resp;
    const ErrorCode EC = TransferWithRetry(MakeApWriteReq(addr2b, val), resp);
    if (EC != ErrorCode::OK)
    {
      ack = Ack::PROTOCOL;
      return EC;
    }

    ack = resp.ack;
    return (resp.ack == Ack::OK) ? ErrorCode::OK : ErrorCode::FAILED;
  }

  /**
   * @brief 事务层写 DP SELECT（带缓存）/ Transaction-layer DP SELECT write (cached)
   *
   * 仅当写入成功时更新缓存；失败时不更新。
   * Cache is updated only on successful write; not updated on failure.
   *
   * @param select SELECT 值 / SELECT value
   * @param ack ACK（输出）/ ACK (output)
   * @return ErrorCode 错误码 / Error code
   */
  ErrorCode SetSelectCached(uint32_t select, Ack& ack)
  {
    if (select_valid_ && (select_cache_ == select))
    {
      ack = Ack::OK;
      return ErrorCode::OK;
    }

    const ErrorCode EC = DpWriteTxn(DpWriteReg::SELECT, select, ack);
    if (EC == ErrorCode::OK && ack == Ack::OK)
    {
      select_cache_ = select;
      select_valid_ = true;
    }

    return EC;
  }

  /**
   * @brief 使 SELECT 缓存失效 / Invalidate SELECT cache
   */
  void InvalidateSelectCache()
  {
    select_valid_ = false;
    select_cache_ = 0;
  }

 protected:
  /**
   * @brief 构造函数 / Constructor
   */
  Swd() = default;

  /**
   * @brief 设置链路状态 / Set link state
   * @param s 状态 / State
   */
  void SetState(State s)
  {
    state_ = s;
    if (s == State::DISABLED || s == State::ERROR)
    {
      InvalidateSelectCache();
    }
  }

  /**
   * @brief 发送 idle clocks（由派生类实现）/ Send idle clocks (implemented by derived
   * class)
   *
   * 用于 WAIT 重试间插入空闲时钟，通常 SWDIO 保持为 0。
   * Used to insert idle clocks between WAIT retries; typically holds SWDIO low.
   *
   * @param cycles 时钟个数 / Number of clock cycles
   */
  virtual void IdleClocks(uint32_t cycles) = 0;

  /**
   * @brief 清除 sticky 错误（事务层）/ Clear sticky errors (transaction layer)
   *
   * @return ErrorCode 错误码 / Error code
   */
  ErrorCode ClearStickyErrors()
  {
    Ack ack = Ack::NO_ACK;
    const uint32_t FLAGS =
        DP_ABORT_STKCMPCLR | DP_ABORT_STKERRCLR | DP_ABORT_WDERRCLR | DP_ABORT_ORUNERRCLR;

    return DpWrite(DpWriteReg::ABORT, FLAGS, ack);
  }

 private:
  State state_ = State::DISABLED;  ///< 当前状态 / Current state

  // 事务层策略与缓存 / Transaction-layer policy and cache
  TransferPolicy policy_{};    ///< 传输策略 / Transfer policy
  uint32_t select_cache_ = 0;  ///< SELECT 缓存 / SELECT cache
  bool select_valid_ = false;  ///< SELECT 缓存是否有效 / SELECT cache valid flag
};

}  // namespace LibXR::Debug
