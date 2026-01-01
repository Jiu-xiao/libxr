#pragma once

#include <cstdint>

#include "gpio.hpp"
#include "libxr_def.hpp"
#include "libxr_time.hpp"
#include "libxr_type.hpp"
#include "swd.hpp"

namespace LibXR::Debug
{

/**
 * @class SwdGeneralGPIO
 * @brief 基于 GPIO 轮询的 SWD 探针 / SWD probe implemented via GPIO bit-bang
 */
class SwdGeneralGPIO final : public Swd
{
 public:
  /**
   * @brief 构造函数 / Constructor
   * @param swclk 用作 SWCLK 的 GPIO / GPIO used as SWCLK
   * @param swdio 用作 SWDIO 的 GPIO / GPIO used as SWDIO
   * @param default_hz 默认 SWCLK 频率（Hz）/ Default SWCLK frequency (Hz)
   */
  SwdGeneralGPIO(GPIO& swclk, GPIO& swdio, uint32_t default_hz = DEFAULT_CLOCK_HZ)
      : swclk_(swclk), swdio_(swdio)
  {
    swclk_.SetConfig({GPIO::Direction::OUTPUT_PUSH_PULL, GPIO::Pull::NONE});
    swclk_.Write(false);

    SetSwdioDriveMode();
    swdio_.Write(true);

    (void)SetClockHz(default_hz);
    SetState(State::IDLE);
  }

  /**
   * @brief 虚析构函数 / Virtual destructor
   */
  ~SwdGeneralGPIO() override = default;

  SwdGeneralGPIO(const SwdGeneralGPIO&) = delete;
  SwdGeneralGPIO& operator=(const SwdGeneralGPIO&) = delete;

  /**
   * @brief 设置 SWCLK 频率 / Set SWCLK frequency
   * @param hz 目标频率（Hz），为 0 表示不插延时 / Target frequency (Hz), 0 = no delay
   * @return ErrorCode
   */
  ErrorCode SetClockHz(uint32_t hz) override
  {
    clock_hz_ = hz;

    if (hz == 0u || hz > 1000000u)
    {
      half_period_us_ = 0u;
      return ErrorCode::OK;
    }

    const uint64_t DENOM = 2ull * static_cast<uint64_t>(hz);
    uint64_t hp = (1000000ull + DENOM - 1ull) / DENOM;  // ceil(1e6 / (2*hz))
    half_period_us_ = static_cast<uint32_t>(hp);

    return ErrorCode::OK;
  }

  /**
   * @brief 关闭探针 / Close probe
   */
  void Close() override
  {
    InvalidateSelectCache();

    swclk_.Write(false);

    SetSwdioDriveMode();
    swdio_.Write(true);
    SetSwdioSampleMode();

    SetState(State::DISABLED);
  }

  /**
   * @brief 发送 line reset（SWDIO=1，64 周期） / Send line reset
   * @return ErrorCode
   */
  ErrorCode LineReset() override
  {
    if (GetState() == State::DISABLED)
    {
      return ErrorCode::FAILED;
    }

    InvalidateSelectCache();

    SetState(State::BUSY);

    SetSwdioDriveMode();
    swdio_.Write(true);
    swclk_.Write(false);

    for (uint32_t i = 0; i < LINE_RESET_CYCLES; ++i)
    {
      GenOneClk();
    }

    swclk_.Write(false);

    SetState(State::IDLE);

    return ErrorCode::OK;
  }

  /**
   * @brief 进入 SWD 模式 / Enter SWD mode
   * @return ErrorCode
   */
  ErrorCode EnterSwd() override
  {
    if (GetState() == State::DISABLED)
    {
      return ErrorCode::FAILED;
    }

    SetState(State::BUSY);

    ErrorCode ec = LineReset();
    if (ec != ErrorCode::OK)
    {
      SetState(State::ERROR);
      return ec;
    }

    SetSwdioDriveMode();
    WriteByteLSB(JTAG_TO_SWD_SEQ0);
    WriteByteLSB(JTAG_TO_SWD_SEQ1);

    ec = LineReset();
    if (ec != ErrorCode::OK)
    {
      SetState(State::ERROR);
      return ec;
    }

    // 发送 8 个 idle 时钟（等价 SWD_SendByte(0x00)）
    swdio_.Write(false);
    WriteByteLSB(0x00);

    SetState(State::IDLE);

    return ErrorCode::OK;
  }

  /**
   * @brief 单次 SWD 传输（无重试）/ Single SWD transfer (no retry)
   * @param req  传输请求 / Transfer request
   * @param resp 传输响应 / Transfer response
   * @return ErrorCode（总线级错误）/ Error code (bus-level)
   */
  ErrorCode Transfer(const Request& req, Response& resp) override
  {
    if (GetState() == State::DISABLED)
    {
      return ErrorCode::FAILED;
    }

    SetState(State::BUSY);

    const bool APNDP = (req.port == Port::AP);
    const uint8_t REQUEST_BYTE = MakeReq(APNDP, req.rnw, req.addr2b);

    resp.ack = Ack::PROTOCOL;
    resp.rdata = 0;
    resp.parity_ok = true;

    // pre-sync
    swclk_.Write(false);
    SetSwdioDriveMode();
    swdio_.Write(false);
    GenOneClk();

    // request
    WriteByteLSB(REQUEST_BYTE);

    // turnaround Host -> Target
    SetSwdioSampleMode();
    GenOneClk();

    // ACK: 3 bits, LSB-first
    uint8_t ack_raw = 0;
    for (uint32_t i = 0; i < ACK_BITS; ++i)
    {
      if (swdio_.Read())
      {
        ack_raw |= static_cast<uint8_t>(1u << i);
      }
      GenOneClk();
    }
    resp.ack = DecodeAck(ack_raw & 0x7u);

    if (resp.ack != Ack::OK)
    {
      TailOneClock();
      swclk_.Write(false);
      SetState(State::IDLE);
      return ErrorCode::OK;
    }

    if (req.rnw)
    {
      // READ path: 4 bytes LSB-first + parity
      uint32_t data = 0;
      for (uint32_t byte = 0; byte < 4; ++byte)
      {
        const uint32_t B = ReadByteLSB();
        data |= (B << (8u * byte));
      }

      const bool PARITY_BIT = ReadBitAndClock();
      resp.rdata = data;
      resp.parity_ok = (static_cast<uint8_t>(PARITY_BIT) == Parity32(data));

      // turnaround Target -> Host
      SetSwdioDriveMode();
      GenOneClk();

      // READ tail
      TailOneClock();
    }
    else
    {
      // WRITE path: turnaround + 4 bytes + parity
      SetSwdioDriveMode();
      GenOneClk();

      const uint32_t DATA = req.wdata;
      for (uint32_t byte = 0; byte < 4; ++byte)
      {
        const uint8_t B = static_cast<uint8_t>((DATA >> (8u * byte)) & 0xFFu);
        WriteByteLSB(B);
      }

      const bool PARITY_BIT = (Parity32(DATA) & 0x1u) != 0u;
      swdio_.Write(PARITY_BIT);
      GenOneClk();

      // WRITE tail（单个清尾时钟）/ single tail clock, no extra idle
      TailOneClock();
    }

    swclk_.Write(false);
    SetState(State::IDLE);
    return ErrorCode::OK;
  }

  /**
   * @brief 发送 idle clocks（用于 WAIT 重试间插入空闲时钟）/ Send idle clocks (for WAIT
   * retry insertion)
   *
   * @param cycles 时钟个数 / Number of clock cycles
   */
  void IdleClocks(uint32_t cycles) override
  {
    // 与 Transfer() 中 pre-sync 的做法保持一致：Host 驱动 SWDIO=0
    SetSwdioDriveMode();
    swdio_.Write(false);
    swclk_.Write(false);

    for (uint32_t i = 0; i < cycles; ++i)
    {
      GenOneClk();
    }

    swclk_.Write(false);
  }

 private:
  /**
   * @brief 构造 SWD request 字节 / Build SWD request byte
   */
  static inline uint8_t MakeReq(bool apndp, bool rnw, uint8_t addr2b)
  {
    const uint8_t A2 = addr2b & 0x1u;
    const uint8_t A3 = (addr2b >> 1) & 0x1u;
    const uint8_t PAR = static_cast<uint8_t>((apndp ^ rnw ^ A2 ^ A3) & 0x1u);

    return static_cast<uint8_t>((1u << 0) | (static_cast<uint8_t>(apndp) << 1) |
                                (static_cast<uint8_t>(rnw) << 2) |
                                (static_cast<uint8_t>(A2) << 3) |
                                (static_cast<uint8_t>(A3) << 4) |
                                (static_cast<uint8_t>(PAR) << 5) | (0u << 6) | (1u << 7));
  }

  /**
   * @brief 计算 32-bit 奇偶校验 / Parity of 32-bit value
   */
  static inline uint8_t Parity32(uint32_t x)
  {
    x ^= x >> 16;
    x ^= x >> 8;
    x ^= x >> 4;
    x &= 0xFu;
    static const uint8_t LUT[16] = {0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0};
    return LUT[x];
  }

  /**
   * @brief 解析 3-bit ACK / Decode 3-bit ACK
   */
  static inline Ack DecodeAck(uint8_t ack_bits)
  {
    switch (ack_bits)
    {
      case 0x1:
        return Ack::OK;
      case 0x2:
        return Ack::WAIT;
      case 0x4:
        return Ack::FAULT;
      case 0x0:
        return Ack::NO_ACK;
      default:
        return Ack::PROTOCOL;
    }
  }

  /**
   * @brief 配置 SWDIO 为输出 / Configure SWDIO as output
   */
  ErrorCode SetSwdioDriveMode()
  {
    return swdio_.SetConfig({GPIO::Direction::OUTPUT_PUSH_PULL, GPIO::Pull::NONE});
  }

  /**
   * @brief 配置 SWDIO 为输入上拉 / Configure SWDIO as input with pull-up
   */
  ErrorCode SetSwdioSampleMode()
  {
    swdio_.Write(true);
    return swdio_.SetConfig({GPIO::Direction::INPUT, GPIO::Pull::UP});
  }

  /**
   * @brief us 级延时 / Delay in microseconds
   */
  static inline void DelayUs(uint32_t us)
  {
    if (us == 0u)
    {
      return;
    }

    const uint64_t START = static_cast<uint64_t>(Timebase::GetMicroseconds());
    while ((static_cast<uint64_t>(Timebase::GetMicroseconds()) - START) < us)
    {
      // busy-wait
    }
  }

  /**
   * @brief 半周期延时 / Half-period delay
   */
  inline void DelayHalf() { DelayUs(half_period_us_); }

  /**
   * @brief 生成一个 SWCLK 周期 / Generate one SWCLK cycle
   */
  inline void GenOneClk()
  {
    swclk_.Write(true);
    DelayHalf();
    swclk_.Write(false);
    DelayHalf();
  }

  /**
   * @brief 写 1 bit（LSB-first）/ Write 1 bit (LSB-first)
   */
  inline void WriteBit(bool bit)
  {
    swdio_.Write(bit);
    GenOneClk();
  }

  /**
   * @brief 写 n 个 bit（LSB-first）/ Write n bits (LSB-first)
   */
  void WriteBitsLSB(uint32_t value, uint32_t nbits)
  {
    for (uint32_t i = 0; i < nbits; ++i)
    {
      const bool BIT = ((value >> i) & 0x1u) != 0u;
      WriteBit(BIT);
    }
  }

  /**
   * @brief 写 1 字节（LSB-first）/ Write 1 byte (LSB-first)
   */
  void WriteByteLSB(uint8_t b) { WriteBitsLSB(static_cast<uint32_t>(b), BYTE_BITS); }

  /**
   * @brief 读 1 bit 并打一拍时钟 / Read 1 bit and clock once
   */
  inline bool ReadBitAndClock()
  {
    const bool B = swdio_.Read();
    GenOneClk();
    return B;
  }

  /**
   * @brief 读 n 个 bit（LSB-first）/ Read n bits (LSB-first)
   */
  uint32_t ReadBitsLSB(uint32_t nbits)
  {
    uint32_t value = 0;
    for (uint32_t i = 0; i < nbits; ++i)
    {
      if (ReadBitAndClock())
      {
        value |= (1u << i);
      }
    }
    return value;
  }

  /**
   * @brief 读 1 字节（LSB-first）/ Read 1 byte (LSB-first)
   */
  uint8_t ReadByteLSB() { return static_cast<uint8_t>(ReadBitsLSB(BYTE_BITS) & 0xFFu); }

  /**
   * @brief SWD 包尾部“清尾”时序 / Tail timing of SWD packet
   */
  inline void TailOneClock()
  {
    swdio_.Write(false);
    SetSwdioDriveMode();
    GenOneClk();
  }

 private:
  static constexpr uint32_t DEFAULT_CLOCK_HZ = 500000u;
  static constexpr uint32_t LINE_RESET_CYCLES = 64u;
  static constexpr uint32_t BYTE_BITS = 8u;
  static constexpr uint32_t ACK_BITS = 3u;
  static constexpr uint32_t DATA_BITS = 32u;

  static constexpr uint8_t JTAG_TO_SWD_SEQ0 = 0x9Eu;
  static constexpr uint8_t JTAG_TO_SWD_SEQ1 = 0xE7u;

  GPIO& swclk_;
  GPIO& swdio_;

  uint32_t clock_hz_ = 0;
  uint32_t half_period_us_ = 0;
};

}  // namespace LibXR::Debug
