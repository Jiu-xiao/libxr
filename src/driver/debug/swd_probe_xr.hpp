#pragma once

#include <cstdint>
#include <cstring>

#include "gpio.hpp"
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "libxr_type.hpp"
#include "stm32_gpio.hpp"
#include "stm32_spi.hpp"
#include "swd.hpp"

namespace LibXR::Debug
{

/**
 * @class SwdProbeXR
 * @brief SWD 探针实现（XR 平台，SPI 桥接） / SWD probe implementation (XR platform, SPI
 * bridge)
 *
 */
class SwdProbeXR final : public Swd
{
 public:
  /**
   * @brief 构造函数 / Constructor
   * @param spi SPI 外设对象 / SPI device object
   * @param rst_n 帧门控 GPIO / Frame gate GPIO
   * @param rnw 读写方向 GPIO / Read/Write direction GPIO
   */
  SwdProbeXR(STM32SPI& spi, STM32GPIO& rst_n, STM32GPIO& rnw)
      : spi_(spi), rst_n_(rst_n), rnw_(rnw)
  {
    rst_n_.SetConfig({LibXR::GPIO::Direction::OUTPUT_PUSH_PULL, LibXR::GPIO::Pull::NONE});
    rnw_.SetConfig({LibXR::GPIO::Direction::OUTPUT_PUSH_PULL, LibXR::GPIO::Pull::NONE});

    // 默认进入可用状态（baseline 拉低）/ Default ready state (baseline low)
    rst_n_.Write(false);
    rnw_.Write(false);

    SetState(State::IDLE);
  }

  /**
   * @brief 虚析构函数 / Virtual destructor
   */
  ~SwdProbeXR() override = default;

  SwdProbeXR(const SwdProbeXR&) = delete;
  SwdProbeXR& operator=(const SwdProbeXR&) = delete;

  /**
   * @brief 设置 SWCLK 频率 / Set SWCLK frequency
   *
   * @param hz 目标频率 / Target frequency in Hz
   * @return ErrorCode 错误码 / Error code
   */
  ErrorCode SetClockHz(uint32_t hz) override
  {
    if (hz == 0u)
    {
      return ErrorCode::ARG_ERR;
    }

    auto cfg = spi_.GetConfig();

    // 选择不超过 hz 的最快分频 / Choose the fastest prescaler not exceeding hz
    const auto P = spi_.CalcPrescaler(hz, 0u, true);
    if (P == SPI::Prescaler::UNKNOWN)
    {
      return ErrorCode::FAILED;
    }

    cfg.prescaler = P;
    return spi_.SetConfig(cfg);
  }

  /**
   * @brief 关闭探针 / Close probe
   */
  void Close() override
  {
    // 置为安全态（按硬件约定可调整）/ Safe state (adjustable)
    rst_n_.Write(false);
    rnw_.Write(false);
    SetState(State::DISABLED);
  }

  /**
   * @brief 发送 line reset / Send line reset
   *
   * 标准要求：SWDIO=1，输出不少于 50 个 SWCLK。
   * Standard requires SWDIO=1 for at least 50 SWCLK cycles.
   *
   * 这里固定为 56 clocks（7 bytes 0xFF）。
   * Fixed to 56 clocks (7 bytes of 0xFF).
   *
   * @return ErrorCode 错误码 / Error code
   */
  ErrorCode LineReset() override
  {
    rst_n_.Write(false);  // RAW mode / 原始模式

    const ErrorCode EC = RawIdleBytes(7, 0xFF);
    if (EC != ErrorCode::OK)
    {
      SetState(State::ERROR);
      return EC;
    }

    rst_n_.Write(true);  // Back to normal baseline / 回到 baseline
    return ErrorCode::OK;
  }

  /**
   * @brief 进入 SWD 模式 / Enter SWD mode
   *
   * 标准流程：LineReset -> 0xE79E(LSB-first: 0x9E,0xE7) -> LineReset -> idle。
   * Standard sequence: LineReset -> 0xE79E (LSB-first: 0x9E,0xE7) -> LineReset -> idle.
   *
   * @return ErrorCode 错误码 / Error code
   */
  ErrorCode EnterSwd() override
  {
    rst_n_.Write(false);

    ErrorCode ec = RawIdleBytes(7, 0xFF);
    if (ec != ErrorCode::OK)
    {
      SetState(State::ERROR);
      return ec;
    }

    const uint8_t SEQ[2] = {0x9E, 0xE7};
    ec = RawWrite(SEQ, 2);
    if (ec != ErrorCode::OK)
    {
      SetState(State::ERROR);
      return ec;
    }

    ec = RawIdleBytes(7, 0xFF);
    if (ec != ErrorCode::OK)
    {
      SetState(State::ERROR);
      return ec;
    }

    // A little idle (keep SWDIO=1) / 少量空闲时钟
    ec = RawIdleBytes(2, 0xFF);
    if (ec != ErrorCode::OK)
    {
      SetState(State::ERROR);
      return ec;
    }

    rst_n_.Write(true);
    return ErrorCode::OK;
  }

  /**
   * @brief 单次 SWD transfer / Single SWD transfer
   *
   * 本实现按当前硬件约定使用固定 6 字节事务帧：
   * - READ：frame = (request_byte << 2)
   *   ack   @ bit11..13
   *   data  @ bit14..45
   *   parity@ bit46
   * - WRITE：frame = (request_byte << 2) | (wdata << 15) | (parity << 47)
   *
   * @param req 请求 / Request
   * @param resp 响应 / Response
   * @return ErrorCode 错误码 / Error code
   */
  ErrorCode Transfer(const Request& req, Response& resp) override
  {
    if (GetState() == State::DISABLED)
    {
      return ErrorCode::FAILED;
    }

    SetState(State::BUSY);

    // Default response / 默认响应初始化
    resp.ack = Ack::PROTOCOL;
    resp.rdata = 0;
    resp.parity_ok = true;

    // External direction control / 外部方向控制
    rnw_.Write(req.rnw);

    const bool APNDP = (req.port == Port::AP);
    const uint8_t REQUEST_BYTE = MakeReq(APNDP, req.rnw, req.addr2b);

    uint8_t tx[6] = {0};
    uint8_t rx[6] = {0};

    if (req.rnw)
    {
      // READ frame
      const uint64_t FRAME = (static_cast<uint64_t>(REQUEST_BYTE) << 2);
      U48ToBytes(FRAME, tx);

      FrameBegin();
      const ErrorCode EC = Xfer6(rx, tx);
      FrameEnd();

      if (EC != ErrorCode::OK)
      {
        SetState(State::ERROR);
        return EC;
      }

      const uint64_t R = U48FromBytes(rx);
      const uint8_t ACK_BITS = static_cast<uint8_t>((R >> 11) & 0x7u);
      resp.ack = DecodeAck(ACK_BITS);

      if (resp.ack == Ack::OK)
      {
        const uint32_t DATA = static_cast<uint32_t>((R >> 14) & 0xFFFFFFFFu);
        const uint8_t P = static_cast<uint8_t>((R >> 46) & 0x1u);

        resp.rdata = DATA;
        resp.parity_ok = (P == Parity32(DATA));
      }
    }
    else
    {
      // WRITE frame
      uint64_t frame = (static_cast<uint64_t>(REQUEST_BYTE) << 2);
      frame |= (static_cast<uint64_t>(req.wdata) << 15);
      frame |= (static_cast<uint64_t>(Parity32(req.wdata) & 0x1u) << 47);
      U48ToBytes(frame, tx);

      FrameBegin();
      const ErrorCode EC = Xfer6(rx, tx);
      FrameEnd();

      if (EC != ErrorCode::OK)
      {
        SetState(State::ERROR);
        return EC;
      }

      const uint64_t R = U48FromBytes(rx);
      const uint8_t ACK_BITS = static_cast<uint8_t>((R >> 11) & 0x7u);
      resp.ack = DecodeAck(ACK_BITS);

      // Write direction parity is generated, not validated / 写方向 parity 仅生成不校验
      resp.rdata = 0;
      resp.parity_ok = true;
    }

    SetState(State::IDLE);
    return ErrorCode::OK;
  }

 private:
  /**
   * @brief 帧开始（门控拉高） / Begin a framed transaction (gate high)
   */
  void FrameBegin() { rst_n_.Write(true); }

  /**
   * @brief 帧结束（门控拉低） / End a framed transaction (gate low)
   */
  void FrameEnd() { rst_n_.Write(false); }

  /**
   * @brief 固定 6-byte full duplex 传输 / Fixed 6-byte full-duplex transfer
   * @param rx 接收缓冲 / RX buffer
   * @param tx 发送缓冲 / TX buffer
   * @return ErrorCode 错误码 / Error code
   */
  ErrorCode Xfer6(uint8_t* rx, const uint8_t* tx)
  {
    return spi_.ReadAndWrite({rx, 6}, {tx, 6}, op_);
  }

  /**
   * @brief RAW 写入（不关心回读） / RAW write (ignore readback)
   * @param tx 待写数据 / Data to write
   * @param len 长度 / Length
   * @return ErrorCode 错误码 / Error code
   */
  ErrorCode RawWrite(const uint8_t* tx, uint32_t len)
  {
    uint8_t dummy_rx[RAW_BUF_SIZE] = {0};

    for (uint32_t off = 0; off < len;)
    {
      uint32_t chunk = len - off;
      if (chunk > RAW_BUF_SIZE)
      {
        chunk = RAW_BUF_SIZE;
      }

      LibXR::Memory::FastCopy(raw_tx_, tx + off, chunk);

      const ErrorCode EC = spi_.ReadAndWrite({dummy_rx, chunk}, {raw_tx_, chunk}, op_);
      if (EC != ErrorCode::OK)
      {
        return EC;
      }

      off += chunk;
    }

    return ErrorCode::OK;
  }

  /**
   * @brief RAW 空闲时钟（重复发送 idle_byte） / RAW idle clocks (repeat idle_byte)
   * @param bytes 发送字节数 / Number of bytes
   * @param idle_byte 空闲字节（默认 0xFF，SWDIO=1） / Idle byte (default 0xFF, SWDIO=1)
   * @return ErrorCode 错误码 / Error code
   */
  ErrorCode RawIdleBytes(uint32_t bytes, uint8_t idle_byte)
  {
    uint8_t dummy_rx[RAW_BUF_SIZE] = {0};

    LibXR::Memory::FastSet(raw_tx_, idle_byte, sizeof(raw_tx_));

    for (uint32_t off = 0; off < bytes;)
    {
      uint32_t chunk = bytes - off;
      if (chunk > RAW_BUF_SIZE)
      {
        chunk = RAW_BUF_SIZE;
      }

      const ErrorCode EC = spi_.ReadAndWrite({dummy_rx, chunk}, {raw_tx_, chunk}, op_);
      if (EC != ErrorCode::OK)
      {
        return EC;
      }

      off += chunk;
    }

    return ErrorCode::OK;
  }

  /**
   * @brief 生成 SWD request 字节 / Build SWD request byte
   * @param apndp APnDP 位 / APnDP bit
   * @param rnw RnW 位 / RnW bit
   * @param addr2b A[3:2] 编码 / A[3:2] encoding
   * @return uint8_t request 字节 / request byte
   */
  static inline uint8_t MakeReq(bool apndp, bool rnw, uint8_t addr2b)
  {
    const uint8_t A2 = addr2b & 0x1u;
    const uint8_t A3 = (addr2b >> 1) & 0x1u;
    const uint8_t PAR = static_cast<uint8_t>((apndp ^ rnw ^ A2 ^ A3) & 0x1u);

    // b0=start(1), b1=APnDP, b2=RnW, b3=A2, b4=A3, b5=parity, b6=stop(0), b7=park(1)
    return static_cast<uint8_t>((1u << 0) | (static_cast<uint8_t>(apndp) << 1) |
                                (static_cast<uint8_t>(rnw) << 2) |
                                (static_cast<uint8_t>(A2) << 3) |
                                (static_cast<uint8_t>(A3) << 4) |
                                (static_cast<uint8_t>(PAR) << 5) | (0u << 6) | (1u << 7));
  }

  /**
   * @brief 计算 32-bit 数据奇偶校验 / Calculate parity of a 32-bit value
   * @param x 32-bit 数据 / 32-bit value
   * @return uint8_t parity（0=偶校验，1=奇校验） / parity (0=even, 1=odd)
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
   * @brief 6 字节小端转 uint64（低 48 bit 有效） / Convert 6 bytes LE to uint64 (low 48
   * bits valid)
   * @param b 输入字节数组 / Input byte array
   * @return uint64_t 值 / Value
   */
  static inline uint64_t U48FromBytes(const uint8_t b[6])
  {
    uint64_t v = 0;
    for (int i = 0; i < 6; ++i)
    {
      v |= (static_cast<uint64_t>(b[i]) << (8 * i));
    }
    return v;
  }

  /**
   * @brief uint64 的低 48 bit 转 6 字节小端 / Convert low 48 bits of uint64 to 6 bytes LE
   * @param v 输入值 / Input value
   * @param b 输出字节数组 / Output byte array
   */
  static inline void U48ToBytes(uint64_t v, uint8_t b[6])
  {
    for (int i = 0; i < 6; ++i)
    {
      b[i] = static_cast<uint8_t>((v >> (8 * i)) & 0xFFu);
    }
  }

  /**
   * @brief ACK 位解析 / Decode ACK bits
   * @param ack_bits 3-bit ACK / 3-bit ACK
   * @return Ack ACK 枚举 / Ack enum
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

 private:
  static constexpr uint32_t RAW_BUF_SIZE = 32;  ///< RAW 缓冲大小 / RAW buffer size

  STM32SPI& spi_;  ///< SPI 外设 / SPI device
  Semaphore sem_;  ///< 操作信号量（用于 OperationRW） / Semaphore for OperationRW
  SPI::OperationRW op_ =
      SPI::OperationRW(sem_);  ///< 读写操作对象 / Read-write operation object
  STM32GPIO& rst_n_;           ///< 帧门控 GPIO / Frame gate GPIO
  STM32GPIO& rnw_;             ///< 方向 GPIO / Direction GPIO

  uint8_t raw_tx_[RAW_BUF_SIZE] = {0};  ///< RAW 发送缓冲 / RAW TX buffer
};

}  // namespace LibXR::Debug
