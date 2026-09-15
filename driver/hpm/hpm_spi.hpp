#pragma once

#include "hpm_clock_drv.h"
#include "hpm_soc.h"
#include "hpm_spi_drv.h"
#include "spi.hpp"

namespace LibXR
{

/**
 * @class HPMSPI
 * @brief HPM SDK SPI 主机驱动，适配 LibXR SPI 接口 /
 * HPM SDK based SPI master driver for the LibXR SPI interface.
 *
 * 该类持有一个 HPM SPI 外设实例，提供普通流式传输和简单寄存器式 SPI 访问。
 * This class owns one HPM SPI peripheral instance and provides stream transfers
 * and simple register-style SPI access.
 *
 * 当前实现调用 HPM SDK 阻塞式传输 API；任一公开传输接口返回时，硬件传输已经
 * 结束，不会留下后台 DMA 或中断事务。
 * The current implementation calls HPM SDK blocking transfer APIs; when any public
 * transfer method returns, the hardware transfer has finished and no background DMA
 * or interrupt transaction remains pending.
 *
 * 支持 LibXR 操作模式参数，但底层传输均为同步完成 /
 * LibXR operation mode parameters are accepted, while the low-level transfer is
 * always completed synchronously:
 * - BLOCK：直接返回最终 ErrorCode，不触发回调或状态更新 /
 *   BLOCK: returns the final ErrorCode directly without callback/status update.
 * - POLLING：返回前把轮询状态更新为 DONE 或 ERROR /
 *   POLLING: updates polling status to DONE or ERROR before return.
 * - CALLBACK：返回前以最终 ErrorCode 执行回调，并透传 in_isr 标志 /
 *   CALLBACK: invokes the callback before return with the final ErrorCode and in_isr.
 * - NONE：只返回最终 ErrorCode，不产生额外副作用 /
 *   NONE: returns the final ErrorCode without additional side effects.
 */
class HPMSPI final : public SPI
{
 public:
  using ChipSelectControl = void (*)(bool selected);

  /**
   * @brief 构造 HPM SPI 主机对象 / Construct an HPM SPI master object.
   * @param spi HPM SPI 外设基地址，不能为空 /
   * HPM SPI peripheral base address. Must not be nullptr.
   * @param clock HPM 时钟树名称；当板级 helper 未提供时用于开启并查询外设源时钟 /
   * HPM clock name used to enable and query the peripheral source clock when the
   * optional board helper does not provide one.
   * @param rx_buffer 内部 RX 暂存缓冲区，供 ReadAndWrite()、Transfer()、MemRead()
   * 使用；驱动生命周期内必须保持非空且有效 /
   * Internal RX staging buffer used by ReadAndWrite(), Transfer(), and MemRead().
   * It must remain non-null and valid for the driver lifetime.
   * @param tx_buffer 内部 TX 暂存缓冲区，供所有传输接口使用；驱动生命周期内必须
   * 保持非空且有效 / Internal TX staging buffer used by all transfer methods.
   * It must remain non-null and valid for the driver lifetime.
   * @param auto_board_init 若为 true 且存在 board.h，则自动调用板级 SPI 时钟和引脚
   * 初始化 helper / If true and board.h is available, call board SPI clock and pin
   * helper functions automatically.
   * @param config 初始 SPI 配置；该驱动按 8-bit、MSB-first、single-I/O、master
   * 模式配置硬件；HPM 后端当前支持 DIV_1 和不超过 DIV_256 的偶数分频 /
   * Initial SPI configuration. This driver configures hardware as 8-bit,
   * MSB-first, single-I/O master. The current HPM backend supports DIV_1 and
   * even prescalers up to DIV_256.
   * @param chip_select Optional GPIO chip-select callback. It is called with true
   * immediately before a transfer and false immediately after it.
   *
   * @note 构造函数会断言外设指针为空、暂存缓冲区为空、源时钟无法解析或初始配置无效 /
   * The constructor asserts on null peripheral pointer, null/empty staging buffers,
   * unresolved source clock, or invalid initial configuration.
   */
  HPMSPI(SPI_Type* spi, clock_name_t clock, RawData rx_buffer, RawData tx_buffer,
         bool auto_board_init = true,
         SPI::Configuration config = {SPI::ClockPolarity::LOW, SPI::ClockPhase::EDGE_1,
                                      SPI::Prescaler::DIV_4, false},
         ChipSelectControl chip_select = nullptr);

  /**
   * @brief 传输 SPI 字节，并可同时采集接收数据 /
   * Transfer SPI bytes while optionally collecting received bytes.
   *
   * 当读写缓冲区都非空时，实际传输长度取两者较大值，较短的写载荷由内部 TX
   * 暂存缓冲区补 0。只有 read_data 非空时执行只读传输；只有 write_data 非空时
   * 执行只写传输。
   * When both buffers are non-empty, the transfer length is the larger size and the
   * shorter write payload is padded with zero bytes in the internal TX staging buffer.
   * If only read_data is non-empty, a read-only transfer is used; if only write_data
   * is non-empty, a write-only transfer is used.
   *
   * @param read_data 目标缓冲区；size_ 为 0 表示不拷贝接收数据，非零长度时 addr_
   * 必须非空 / Destination buffer. A zero-size buffer disables receive copy; for
   * non-zero size, addr_ must be non-null.
   * @param write_data 源缓冲区；size_ 为 0 表示不发送用户载荷，非零长度时 addr_
   * 必须非空 / Source buffer. A zero-size buffer disables user transmit payload; for
   * non-zero size, addr_ must be non-null.
   * @param op LibXR 读写操作描述符，见类注释中的操作模式说明 /
   * LibXR read/write operation descriptor. See class-level operation mode notes.
   * @param in_isr 仅用于 CALLBACK/POLLING 完成分发的中断上下文标志 /
   * ISR-context flag forwarded only to CALLBACK/POLLING completion handling.
   * @return 成功返回 OK，包括读写长度都为 0 的情况；空用户缓冲区返回 PTR_NULL；
   * 传输长度超过 SPI_SOC_TRANSFER_COUNT_MAX 或内部暂存缓冲区容量返回 SIZE_ERR；
   * 其余返回 HPM SDK 状态转换后的 TIMEOUT、BUSY 或 FAILED /
   * Returns OK on success, including when both sizes are zero; PTR_NULL for null
   * non-empty user buffer; SIZE_ERR when the effective length exceeds
   * SPI_SOC_TRANSFER_COUNT_MAX or staging buffer capacity; otherwise converted HPM
   * SDK status such as TIMEOUT, BUSY, or FAILED.
   */
  ErrorCode ReadAndWrite(RawData read_data, ConstRawData write_data, OperationRW& op,
                         bool in_isr = false) override;

  /**
   * @brief 应用 SPI 格式和时序配置 / Apply SPI format and timing configuration.
   * @param config 目标时钟极性、相位、分频和双缓冲标志 /
   * Desired clock polarity, phase, prescaler, and double-buffer flag.
   * @return 成功返回 OK；外设指针为空返回 PTR_NULL；源时钟无法解析返回 INIT_ERR；
   * 分频无效或计算出的 SCLK 为 0 返回 ARG_ERR；超过 DIV_256 返回 NOT_SUPPORT；
   * 其余返回 HPM SDK 时序配置状态 /
   * Returns OK on success, PTR_NULL for null peripheral pointer, INIT_ERR when source
   * clock cannot be resolved, ARG_ERR for invalid prescaler or zero SCLK, NOT_SUPPORT
   * beyond DIV_256, or converted HPM SDK timing status otherwise.
   */
  ErrorCode SetConfig(SPI::Configuration config) override;

  /**
   * @brief 使用 8-bit 命令字节读取 SPI 寄存器 /
   * Read bytes from an SPI register using an 8-bit command byte.
   *
   * 发送的命令字节为 static_cast<uint8_t>(reg | 0x80)：bit7 表示读操作，
   * bit0..6 为寄存器地址，reg 高于 bit7 的位会被截断。
   * The transmitted command byte is static_cast<uint8_t>(reg | 0x80): bit 7 marks
   * a read operation, bits 0..6 carry the register address, and bits above bit 7 are
   * discarded by the cast.
   *
   * @param reg 寄存器地址；置读位前的有效范围为 0x00..0x7F /
   * Register address. Effective range before applying the read bit is 0x00..0x7F.
   * @param read_data 目标载荷缓冲区；size_ 为 0 时不访问总线并返回 OK；非零长度时
   * addr_ 必须非空 / Destination payload buffer. A zero-size buffer completes with OK
   * without bus access; for non-zero size, addr_ must be non-null.
   * @param op LibXR 读写操作描述符，见类注释中的操作模式说明 /
   * LibXR read/write operation descriptor. See class-level operation mode notes.
   * @param in_isr 仅用于 CALLBACK/POLLING 完成分发的中断上下文标志 /
   * ISR-context flag forwarded only to CALLBACK/POLLING completion handling.
   * @return 成功返回 OK；空目标缓冲区返回 PTR_NULL；命令字节加载荷超过硬件限制或
   * 暂存缓冲区容量返回 SIZE_ERR；其余返回 HPM SDK 状态转换后的错误码 /
   * Returns OK on success, PTR_NULL for null non-empty destination, SIZE_ERR when
   * command byte plus payload exceeds hardware limit or staging capacity, or converted
   * HPM SDK status otherwise.
   */
  ErrorCode MemRead(uint16_t reg, RawData read_data, OperationRW& op,
                    bool in_isr = false) override;

  /**
   * @brief 使用 8-bit 命令字节写入 SPI 寄存器 /
   * Write bytes to an SPI register using an 8-bit command byte.
   *
   * 发送的命令字节为 static_cast<uint8_t>(reg & 0x7F)：bit7 被清零表示写操作，
   * bit0..6 为寄存器地址，reg 高于 bit7 的位会被截断。零长度载荷合法，只发送命令字节。
   * The transmitted command byte is static_cast<uint8_t>(reg & 0x7F): bit 7 is
   * cleared for write operation, bits 0..6 carry the register address, and bits above
   * bit 7 are discarded. A zero-size payload is valid and sends only the command byte.
   *
   * @param reg 寄存器地址；有效范围为 0x00..0x7F /
   * Register address. Effective range is 0x00..0x7F.
   * @param write_data 源载荷缓冲区；非零长度时 addr_ 必须非空 /
   * Source payload buffer. For non-zero size, addr_ must be non-null.
   * @param op LibXR 读写操作描述符，见类注释中的操作模式说明 /
   * LibXR read/write operation descriptor. See class-level operation mode notes.
   * @param in_isr 仅用于 CALLBACK/POLLING 完成分发的中断上下文标志 /
   * ISR-context flag forwarded only to CALLBACK/POLLING completion handling.
   * @return 成功返回 OK；空载荷指针返回 PTR_NULL；命令字节加载荷超过硬件限制或 TX
   * 暂存缓冲区容量返回 SIZE_ERR；其余返回 HPM SDK 状态转换后的错误码 /
   * Returns OK on success, PTR_NULL for null non-empty payload, SIZE_ERR when command
   * byte plus payload exceeds hardware limit or TX staging capacity, or converted HPM
   * SDK status otherwise.
   */
  ErrorCode MemWrite(uint16_t reg, ConstRawData write_data, OperationRW& op,
                     bool in_isr = false) override;

  /**
   * @brief 获取 SPI 源时钟频率，即理论最高总线速度 /
   * Get SPI source clock frequency, the theoretical fastest bus speed.
   * @return 源时钟频率，单位 Hz；成功解析时钟前为 0 /
   * Source clock frequency in Hz, or 0 before successful clock discovery.
   */
  uint32_t GetMaxBusSpeed() const override;

  /**
   * @brief 获取该 HPM SPI 后端支持的最慢分频 /
   * Get the slowest prescaler supported by this HPM SPI backend.
   * @return Prescaler::DIV_256 / Prescaler::DIV_256.
   */
  Prescaler GetMaxPrescaler() const override;

  /**
   * @brief 从当前活动 TX 暂存缓冲区传输到 RX 暂存缓冲区 /
   * Transfer from the active TX staging buffer into the RX staging buffer.
   *
   * 这是给已通过 GetTxBuffer() 准备数据的调用者使用的零拷贝路径。当前活动 RX/TX
   * 暂存缓冲区都必须至少有 size 字节。传输结束后调用 SwitchBuffer()，使双缓冲用户
   * 切换到另一组缓冲区；当前实现仅在传输成功后执行 SwitchBuffer()。
   * This is the zero-copy path for callers that prepared data through GetTxBuffer().
   * The active RX/TX staging buffers must both contain at least size bytes. After the
   * transfer, SwitchBuffer() is called so double-buffer users advance to the other
   * buffer pair; the current implementation switches only after successful transfer.
   *
   * @param size 传输字节数；0 合法并直接返回 OK；非零时不能超过
   * SPI_SOC_TRANSFER_COUNT_MAX / Number of bytes to transfer. Zero is valid and
   * completes with OK; non-zero size must not exceed SPI_SOC_TRANSFER_COUNT_MAX.
   * @param op LibXR 读写操作描述符，见类注释中的操作模式说明 /
   * LibXR read/write operation descriptor. See class-level operation mode notes.
   * @param in_isr 仅用于 CALLBACK/POLLING 完成分发的中断上下文标志 /
   * ISR-context flag forwarded only to CALLBACK/POLLING completion handling.
   * @return 成功返回 OK；长度超过硬件限制或活动暂存缓冲区容量返回 SIZE_ERR；活动
   * 暂存指针为空返回 PTR_NULL；其余返回 HPM SDK 状态转换后的错误码 /
   * Returns OK on success, SIZE_ERR when size exceeds hardware limit or active staging
   * capacity, PTR_NULL for null active staging pointer, or converted HPM SDK status
   * otherwise.
   */
  ErrorCode Transfer(size_t size, OperationRW& op, bool in_isr = false) override;

 private:
  /**
   * @brief 将 HPM SDK SPI 状态码转换为 LibXR 错误码 /
   * Convert HPM SDK SPI status to LibXR ErrorCode.
   */
  static ErrorCode ConvertStatus(hpm_stat_t status);

  /**
   * @brief 转换 LibXR SPI 时钟极性到 HPM SDK 枚举 /
   * Convert LibXR SPI clock polarity to HPM SDK enum.
   */
  static spi_sclk_idle_state_t ConvertPolarity(ClockPolarity polarity);

  /**
   * @brief 转换 LibXR SPI 时钟相位到 HPM SDK 枚举 /
   * Convert LibXR SPI clock phase to HPM SDK enum.
   */
  static spi_sclk_sampling_clk_edges_t ConvertPhase(ClockPhase phase);

  /**
   * @brief 完成 LibXR 操作状态更新或回调 /
   * Complete LibXR operation status update or callback dispatch.
   */
  template <typename Op>
  static ErrorCode FinishOperation(Op& op, bool in_isr, ErrorCode ans)
  {
    if (op.type != Op::OperationType::BLOCK)
    {
      op.UpdateStatus(in_isr, ans);
    }
    return ans;
  }

  /**
   * @brief 根据 LibXR 分频参数配置 HPM SPI 时序 /
   * Configure HPM SPI timing from LibXR prescaler.
   */
  ErrorCode ApplyTiming(Prescaler prescaler);

  /**
   * @brief 校验 HPM SPI 配置参数 / Validate HPM SPI configuration.
   */
  ErrorCode ValidateConfiguration(const Configuration& config) const;

  /**
   * @brief 确保源时钟频率已可用 / Ensure the source clock frequency is available.
   */
  ErrorCode EnsureClockReady();

  /**
   * @brief 按当前 LibXR 配置写入 HPM SPI 格式寄存器 /
   * Apply HPM SPI format registers from the current LibXR configuration.
   */
  void ApplyFormat(const Configuration& config);

  /**
   * @brief 构造一次普通主机传输的控制参数 /
   * Build control configuration for a regular master transfer.
   */
  spi_control_config_t MakeControlConfig(spi_trans_mode_t mode) const;

  /**
   * @brief 判断失败后是否需要复位控制器 / Decide whether a failed transfer should
   * recover the controller.
   */
  static bool ShouldRecover(hpm_stat_t status);

  /**
   * @brief 复位 HPM SPI 控制器并恢复上次成功配置 /
   * Reset the HPM SPI controller and restore the last successful configuration.
   */
  void RecoverController();

  /**
   * @brief 执行全双工写读同时传输 / Execute full-duplex write-read transfer.
   */
  ErrorCode DoTransfer(uint8_t* rx, const uint8_t* tx, uint32_t size);

  /**
   * @brief 执行只写传输 / Execute write-only transfer.
   */
  ErrorCode DoWriteOnly(const uint8_t* tx, uint32_t size);

  /**
   * @brief 执行只读传输 / Execute read-only transfer.
   */
  ErrorCode DoReadOnly(uint8_t* rx, uint32_t size);

  void SetChipSelect(bool selected) const;

  SPI_Type* spi_;                  ///< SPI 外设实例 / SPI peripheral instance.
  clock_name_t clock_;             ///< SPI 源时钟名称 / SPI source clock name.
  uint32_t source_clock_hz_ = 0;   ///< 缓存的源时钟频率 / Cached source clock frequency.
  size_t rx_buffer_capacity_ = 0;  ///< 原始 RX 缓冲区容量 / Raw RX buffer capacity.
  size_t tx_buffer_capacity_ = 0;  ///< 原始 TX 缓冲区容量 / Raw TX buffer capacity.
  bool configured_ = false;        ///< 是否已有成功配置 / Whether a config was applied.
  bool auto_board_init_;  ///< 是否自动调用板级初始化 / Whether board init is automatic.
  ChipSelectControl chip_select_;  ///< Optional transaction-level GPIO chip-select.
};

}  // namespace LibXR
