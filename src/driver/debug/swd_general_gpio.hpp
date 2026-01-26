#pragma once

#include <cstdint>

#include "gpio.hpp"
#include "libxr_def.hpp"
#include "swd.hpp"
#include "timebase.hpp"

namespace LibXR::Debug
{
/**
 * @brief 基于 GpioType 轮询 bit-bang 的 SWD 探针。
 *        SWD probe based on polling bit-bang using GpioType.
 * 
 * @tparam GpioType GPIO 类型。GPIO type.
 *
 * @note 推荐外围电路：SWCLK/SWDIO 均串联 33Ω 限流电阻，SWDIO 端接 10k 上拉电阻。
 *       Recommended circuit: 33Ω series resistors on SWCLK/SWDIO, 10k pull-up on SWDIO.
 */
template <typename GpioType>
class SwdGeneralGPIO final : public Swd
{
 public:
  /**
   * @brief 构造函数。Constructor.
   * @param swclk 用作 SWCLK 的 GPIO。GPIO used as SWCLK.
   * @param swdio 用作 SWDIO 的 GPIO。GPIO used as SWDIO.
   * @param default_hz 默认 SWCLK 频率（Hz）。Default SWCLK frequency (Hz).
   */
  explicit SwdGeneralGPIO(GpioType& swclk, GpioType& swdio,
                          uint32_t default_hz = DEFAULT_CLOCK_HZ)
      : swclk_(swclk), swdio_(swdio)
  {
    // SWCLK 基线配置：推挽输出，空闲高电平（保留历史行为）。SWCLK baseline: push-pull
    // output, idle high (legacy behavior kept).
    swclk_.SetConfig({GpioType::Direction::OUTPUT_PUSH_PULL, GpioType::Pull::NONE});
    swclk_.Write(true);

    // SWDIO 基线配置：主机初始驱动为高电平（推挽输出）。SWDIO baseline: host drives high
    // initially (push-pull output).
    (void)SetSwdioDriveMode();
    swdio_.Write(true);

    (void)SetClockHz(default_hz);
  }

  ~SwdGeneralGPIO() override = default;

  SwdGeneralGPIO(const SwdGeneralGPIO&) = delete;
  SwdGeneralGPIO& operator=(const SwdGeneralGPIO&) = delete;

  ErrorCode SetClockHz(uint32_t hz) override
  {
    clock_hz_ = hz;

    // hz == 0 或过高：尽力而为，不插入延时。hz == 0 or too high: best-effort, no delay.
    if (hz == 0u || hz > 1'000'000u)
    {
      half_period_us_ = 0u;
      return ErrorCode::OK;
    }

    // half_period_us = ceil(1e6 / (2*hz))。half_period_us = ceil(1e6 / (2*hz)).
    const uint64_t DENOM = 2ull * static_cast<uint64_t>(hz);
    const uint64_t HP = (1'000'000ull + DENOM - 1ull) / DENOM;
    half_period_us_ = static_cast<uint32_t>(HP);
    return ErrorCode::OK;
  }

  void Close() override
  {
    InvalidateSelectCache();

    // 安全状态：Safe state:
    // - SWCLK 高电平（历史行为）。SWCLK high (legacy).
    // - SWDIO 上拉输入（不驱动）。SWDIO input with pull-up (no drive).
    swclk_.Write(true);
    (void)SetSwdioSampleMode();
  }

  ErrorCode LineReset() override
  {
    InvalidateSelectCache();
    // SWD 线复位：SWDIO = 1 持续 >= 50 个周期；此处使用 64 个周期。SWD line reset: SWDIO
    // = 1 for >= 50 cycles; here use 64 cycles.
    (void)SetSwdioDriveMode();
    swdio_.Write(true);
    for (uint32_t i = 0; i < LINE_RESET_CYCLES; ++i)
    {
      GenOneClk();
    }
    return ErrorCode::OK;
  }

  ErrorCode EnterSwd() override
  {
    ErrorCode ec = LineReset();
    if (ec != ErrorCode::OK)
    {
      return ec;
    }

    WriteByteLSB(JTAG_TO_SWD_SEQ0);
    WriteByteLSB(JTAG_TO_SWD_SEQ1);

    ec = LineReset();
    if (ec != ErrorCode::OK)
    {
      return ec;
    }

    swdio_.Write(false);
    WriteByteLSB(0x00);

    return ErrorCode::OK;
  }

  ErrorCode Transfer(const SwdProtocol::Request& req,
                     SwdProtocol::Response& resp) override
  {
    resp.ack = SwdProtocol::Ack::PROTOCOL;
    resp.rdata = 0u;
    resp.parity_ok = true;

    const bool APNDP = (req.port == SwdProtocol::Port::AP);
    const uint8_t REQUEST_BYTE = MakeReq(APNDP, req.rnw, req.addr2b);

    // 请求阶段确保 SWDIO 处于驱动模式。Ensure SWDIO is driven for request phase.
    (void)SetSwdioDriveMode();

    // 请求阶段（8 bit，LSB-first）。Request (8 bits, LSB-first).
    WriteByteLSB(REQUEST_BYTE);

    // 方向切换 Host -> Target：将 SWDIO 切为输入，然后产生 1 个时钟。Turnaround Host ->
    // Target: switch SWDIO to input, then one clock.
    (void)SetSwdioSampleMode();
    GenOneClk();

    // ACK：3 bit，LSB-first。ACK: 3 bits, LSB-first.
    uint8_t ack_raw = 0u;
    for (uint32_t i = 0; i < ACK_BITS; ++i)
    {
      if (swdio_.Read())
      {
        ack_raw |= static_cast<uint8_t>(1u << i);
      }
      GenOneClk();
    }
    resp.ack = DecodeAck(static_cast<uint8_t>(ack_raw & 0x7u));

    if (resp.ack != SwdProtocol::Ack::OK)
    {
      // 方向切换 Target -> Host（跳过数据阶段）：产生 1 个时钟。Turnaround Target -> Host
      // (data phase skipped): one clock.
      GenOneClk();

      // 线路驻留（为下一次传输准备/与 SWJ shadow 使用保持一致）：Park lines for next
      // transfer / keep consistent with SWJ shadow usage:
      // - SWDIO 高电平（驻留）。SWDIO high (park).
      // - SWCLK 低电平。SWCLK low.
      (void)SetSwdioDriveMode();
      swdio_.Write(true);
      swclk_.Write(false);
      return ErrorCode::OK;
    }

    if (req.rnw)
    {
      // READ：保持输入，读取 32-bit 数据（按字节 LSB-first）+ 奇偶校验位。READ: keep
      // input, read 32-bit data (LSB-first per byte) + parity bit.
      uint32_t data = 0u;
      for (uint32_t byte = 0; byte < 4u; ++byte)
      {
        const uint32_t B = ReadByteLSB();
        data |= (B << (8u * byte));
      }

      const bool PARITY_BIT = ReadBitAndClock();
      resp.rdata = data;
      resp.parity_ok = (static_cast<uint8_t>(PARITY_BIT) == Parity32(data));

      // 方向切换 Target -> Host：切为驱动并将 SWDIO 驻留为高电平，然后产生 1
      // 个时钟。Turnaround Target -> Host: switch to drive, park SWDIO high, one clock.
      (void)SetSwdioDriveMode();
      swdio_.Write(true);
      GenOneClk();

      // SWCLK 驻留为低电平（与其他路径对齐）。Park SWCLK low (align with other paths).
      swclk_.Write(false);
    }
    else
    {
      // WRITE：方向切换（1 个时钟）后写入 32-bit 数据 + 奇偶校验位。WRITE: turnaround
      // (one clock) then 32-bit data + parity.
      (void)SetSwdioDriveMode();
      GenOneClk();

      const uint32_t DATA = req.wdata;
      for (uint32_t byte = 0; byte < 4u; ++byte)
      {
        const uint8_t B = static_cast<uint8_t>((DATA >> (8u * byte)) & 0xFFu);
        WriteByteLSB(B);
      }

      const bool PARITY_BIT = (Parity32(DATA) & 0x1u) != 0u;
      WriteBit(PARITY_BIT);

      // 线路驻留。Park lines.
      swdio_.Write(true);
      swclk_.Write(false);
    }

    return ErrorCode::OK;
  }

  void IdleClocks(uint32_t cycles) override
  {
    // CMSIS-DAP 空闲周期插入。CMSIS-DAP idle cycles insertion.
    // 保留历史序列：周期内驱动 SWDIO 高电平，结束后拉低。Keep the legacy sequence: drive
    // SWDIO high during cycles, then pull low.
    (void)SetSwdioDriveMode();
    swdio_.Write(true);

    for (uint32_t i = 0; i < cycles; ++i)
    {
      GenOneClk();
    }

    swdio_.Write(false);
  }

  ErrorCode SeqWriteBits(uint32_t cycles, const uint8_t* data_lsb_first) override
  {
    if (cycles == 0u)
    {
      // 保持一致：结束时 SWCLK 低电平
      swclk_.Write(false);
      return ErrorCode::OK;
    }
    if (data_lsb_first == nullptr)
    {
      return ErrorCode::ARG_ERR;
    }

    // 输出序列：SWDIO 输出，SWCLK 以 low->high->low 脉冲，结束保持 low
    (void)SetSwdioDriveMode();

    // 统一让序列从 SWCLK=0 开始，并最终回到 0
    swclk_.Write(false);

    for (uint32_t i = 0; i < cycles; ++i)
    {
      const bool BIT =
          (((data_lsb_first[i / 8u] >> (i & 7u)) & 0x01u) != 0u);  // LSB-first
      swdio_.Write(BIT);

      // clock pulse: low -> high -> low (leave low)
      DelayHalf();
      swclk_.Write(true);
      DelayHalf();
      swclk_.Write(false);
      DelayHalf();
    }

    return ErrorCode::OK;
  }

  ErrorCode SeqReadBits(uint32_t cycles, uint8_t* out_lsb_first) override
  {
    if (cycles == 0u)
    {
      swclk_.Write(false);
      return ErrorCode::OK;
    }
    if (out_lsb_first == nullptr)
    {
      return ErrorCode::ARG_ERR;
    }

    const uint32_t BYTES = (cycles + 7u) / 8u;
    Memory::FastSet(out_lsb_first, 0, BYTES);

    // 输入序列：SWDIO 输入采样，SWCLK low->high->low，采样在 high 相位（best-effort）
    (void)SetSwdioSampleMode();

    swclk_.Write(false);

    for (uint32_t i = 0; i < cycles; ++i)
    {
      // low phase
      DelayHalf();

      // high phase
      swclk_.Write(true);
      DelayHalf();

      const bool BIT = swdio_.Read();
      if (BIT)
      {
        out_lsb_first[i / 8u] =
            static_cast<uint8_t>(out_lsb_first[i / 8u] | (1u << (i & 7u)));
      }

      // back to low (leave low)
      swclk_.Write(false);
      DelayHalf();
    }

    return ErrorCode::OK;
  }

 private:
  static inline uint8_t MakeReq(bool apndp, bool rnw, uint8_t addr2b)
  {
    const uint8_t A2 = addr2b & 0x1u;
    const uint8_t A3 = (addr2b >> 1) & 0x1u;
    const uint8_t PAR = static_cast<uint8_t>((apndp ^ rnw ^ A2 ^ A3) & 0x1u);

    // 请求字节位域：start(1), APnDP, RnW, A2, A3, PAR, stop(0), park(1)。Request bit
    // fields: start(1), APnDP, RnW, A2, A3, PAR, stop(0), park(1).
    return static_cast<uint8_t>((1u << 0) | (static_cast<uint8_t>(apndp) << 1) |
                                (static_cast<uint8_t>(rnw) << 2) |
                                (static_cast<uint8_t>(A2) << 3) |
                                (static_cast<uint8_t>(A3) << 4) |
                                (static_cast<uint8_t>(PAR) << 5) | (0u << 6) | (1u << 7));
  }

  static inline uint8_t Parity32(uint32_t x)
  {
    x ^= x >> 16;
    x ^= x >> 8;
    x ^= x >> 4;
    x &= 0xFu;
    static constexpr uint8_t LUT[16] = {
        0, 1, 1, 0, 1, 0, 0, 1,
        1, 0, 0, 1, 0, 1, 1, 0};  ///< 奇偶校验查找表（4-bit 折叠）。Parity lookup table
                                  ///< (4-bit fold).
    return LUT[x];
  }

  static inline SwdProtocol::Ack DecodeAck(uint8_t ack_bits)
  {
    switch (ack_bits)
    {
      case 0x1:
        return SwdProtocol::Ack::OK;
      case 0x2:
        return SwdProtocol::Ack::WAIT;
      case 0x4:
        return SwdProtocol::Ack::FAULT;
      case 0x0:
        return SwdProtocol::Ack::NO_ACK;
      default:
        return SwdProtocol::Ack::PROTOCOL;
    }
  }

 private:
  /**
   * @enum SwdioMode
   * @brief SWDIO 管脚当前模式。Current SWDIO pin mode.
   */
  enum class SwdioMode : uint8_t
  {
    UNKNOWN = 0,  ///< 未知/未初始化。Unknown / uninitialized.
    DRIVE_PP,     ///< 推挽输出。Push-pull output.
    SAMPLE_IN,    ///< 输入采样。Input sampling.
  };

  ErrorCode SetSwdioDriveMode()
  {
    if (swdio_mode_ == SwdioMode::DRIVE_PP)
    {
      return ErrorCode::OK;
    }

    const ErrorCode EC =
        swdio_.SetConfig({GpioType::Direction::OUTPUT_PUSH_PULL, GpioType::Pull::NONE});
    if (EC == ErrorCode::OK)
    {
      swdio_mode_ = SwdioMode::DRIVE_PP;
    }
    return EC;
  }

  ErrorCode SetSwdioSampleMode()
  {
    if (swdio_mode_ == SwdioMode::SAMPLE_IN)
    {
      return ErrorCode::OK;
    }

    const ErrorCode EC =
        swdio_.SetConfig({GpioType::Direction::INPUT, GpioType::Pull::UP});
    if (EC == ErrorCode::OK)
    {
      swdio_mode_ = SwdioMode::SAMPLE_IN;
    }
    return EC;
  }

  inline void DelayHalf()
  {
    if (half_period_us_ != 0u)
    {
      Timebase::DelayMicroseconds(half_period_us_);
    }
  }

  inline void GenOneClk()
  {
    swclk_.Write(false);
    DelayHalf();
    swclk_.Write(true);
    DelayHalf();
  }

  inline void WriteBit(bool bit)
  {
    swdio_.Write(bit);
    GenOneClk();
  }

  inline void WriteByteLSB(uint8_t b)
  {
    for (uint32_t i = 0; i < BYTE_BITS; ++i)
    {
      WriteBit(((b >> i) & 0x1u) != 0u);
    }
  }

  inline bool ReadBitAndClock()
  {
    const bool B = swdio_.Read();
    GenOneClk();
    return B;
  }

  inline uint8_t ReadByteLSB()
  {
    uint8_t v = 0u;
    for (uint32_t i = 0; i < BYTE_BITS; ++i)
    {
      if (ReadBitAndClock())
      {
        v = static_cast<uint8_t>(v | (1u << i));
      }
    }
    return v;
  }

 private:
  static constexpr uint32_t DEFAULT_CLOCK_HZ =
      500'000u;  ///< 默认 SWCLK 频率（Hz）。Default SWCLK frequency (Hz).
  static constexpr uint32_t LINE_RESET_CYCLES =
      64u;  ///< 线复位时钟周期数。Line reset clock cycles.
  static constexpr uint32_t BYTE_BITS = 8u;  ///< 每字节比特数。Bits per byte.
  static constexpr uint32_t ACK_BITS = 3u;   ///< ACK 比特数。ACK bits.

  static constexpr uint8_t JTAG_TO_SWD_SEQ0 =
      0x9Eu;  ///< JTAG->SWD 序列字节 0。JTAG-to-SWD sequence byte 0.
  static constexpr uint8_t JTAG_TO_SWD_SEQ1 =
      0xE7u;  ///< JTAG->SWD 序列字节 1。JTAG-to-SWD sequence byte 1.

  GpioType& swclk_;  ///< SWCLK GPIO。GPIO for SWCLK.
  GpioType& swdio_;  ///< SWDIO GPIO。GPIO for SWDIO.

  uint32_t clock_hz_ = 0u;  ///< 当前 SWCLK 频率（Hz）。Current SWCLK frequency (Hz).
  uint32_t half_period_us_ = 0u;  ///< 半周期延时（us）。Half-period delay (us).

  SwdioMode swdio_mode_ =
      SwdioMode::UNKNOWN;  ///< SWDIO 当前模式缓存。Cached current SWDIO mode.
};

}  // namespace LibXR::Debug
