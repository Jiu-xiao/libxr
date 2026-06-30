/**
 * @file hpm_mcan.hpp
 * @brief HPM MCAN IP 閫傞厤澶存枃浠?/ Adapter header for HPM MCAN IP.
 *
 * @details
 * 鏈枃浠舵寜搴曞眰澶栬 IP
 * 褰掓。锛屽搴?`MCAN_Type`銆傛枃浠朵腑鍚屾椂鎻愪緵 `HPMCAN` 涓? *
 * `HPMCANFD`锛氬墠鑰呭鍑?`LibXR::CAN`锛屽悗鑰呭鍑?`LibXR::FDCAN`銆備袱鑰呭叡鐢ㄥ悓涓€濂?
 * * MCAN 澶栬妯″瀷锛屼絾鍚屼竴涓?MCAN instance 涓嶅厑璁稿叡瀛樸€? * This file
 * is grouped by the low-level peripheral IP and targets `MCAN_Type`. This follow-up
 * intentionally does not redefine `HPMCAN`; PR #208 keeps the MCAN classic `LibXR::CAN`
 * adapter in `hpm_can.*`. This file adds only `HPMCANFD`, the `LibXR::FDCAN` adapter for
 * the same Bosch MCAN IP.
 *
 * 澶氱郴鍒楅€傞厤閫氳繃 `HPMSOC_HAS_HPMSDK_MCAN`銆乣__has_include("hpm_mcan_drv.h")`銆? *
 * `MCAN_SOC_MAX_COUNT`銆乣HPM_MCANn`銆乣IRQn_MCANn` 鍜?HPM SDK `hpm_mcan_drv.h`
 * 鏆撮湶鐨勭姸鎬?feature 瀹忓畬鎴愶紱娌℃湁 MCAN 鐨?SoC 鎴栬鍓?SDK 缂哄皯 MCAN
 * driver 澶存椂璧? * `NOT_SUPPORT` 鎴栫紪璇戞湡 gate銆? * Multi-series adaptation is
 * driven by `HPMSOC_HAS_HPMSDK_MCAN`,
 * `__has_include("hpm_mcan_drv.h")`, `MCAN_SOC_MAX_COUNT`, `HPM_MCANn`,
 * `IRQn_MCANn`, and status/feature macros exposed by the HPM SDK `hpm_mcan_drv.h`.
 * SoCs without MCAN, or trimmed SDKs without the MCAN driver header, use the
 * `NOT_SUPPORT` path or compile-time gate.
 */
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "can.hpp"
#include "hpm_clock_drv.h"
#include "hpm_common.h"
#include "hpm_soc.h"
#include "lockfree_pool.hpp"

#if defined(HPMSOC_HAS_HPMSDK_MCAN) && __has_include("hpm_mcan_drv.h") &&           \
                                                     defined(MCAN_SOC_MAX_COUNT) && \
                                                     (MCAN_SOC_MAX_COUNT > 0)
#include "hpm_mcan_drv.h"
#define LIBXR_HPM_MCAN_CORE_SUPPORTED 1
#define LIBXR_HPM_MCAN_SUPPORTED 1
using LibXRHpmCanType = MCAN_Type;
using LibXRHpmCanFdType = MCAN_Type;
#else
#define LIBXR_HPM_MCAN_CORE_SUPPORTED 0
#define LIBXR_HPM_MCAN_SUPPORTED 0
using mcan_last_err_code_t = int;
using mcan_node_mode_t = int;
typedef struct
{
  uint16_t prescaler;
  uint16_t num_seg1;
  uint16_t num_seg2;
  uint8_t num_sjw;
  bool enable_tdc;
} mcan_bit_timing_param_t;
typedef struct
{
  uint32_t ext_id;
  uint32_t std_id;
  uint32_t rtr;
  uint32_t use_ext_id;
  uint32_t error_state_indicator;
  uint32_t dlc;
  uint32_t bitrate_switch;
  uint32_t canfd_frame;
  uint8_t data_8[64];
} mcan_tx_frame_t;
typedef struct
{
  uint32_t ext_id;
  uint32_t std_id;
  uint32_t rtr;
  uint32_t use_ext_id;
  uint32_t dlc;
  uint32_t bitrate_switch;
  uint32_t canfd_frame;
  uint8_t data_8[64];
} mcan_rx_message_t;
using LibXRHpmCanType = void;
using LibXRHpmCanFdType = void;
#endif

namespace LibXR
{

namespace detail
{

enum class HpmMcanOwnerKind : uint8_t
{
  NONE = 0,
  CAN,
  CANFD
};

struct HpmMcanSharedOwner
{
  HpmMcanOwnerKind kind = HpmMcanOwnerKind::NONE;
  void* owner = nullptr;
};

template <typename Owner, uint8_t InstanceCount>
class HpmMcanInstanceRegistry
{
 public:
  static void Register(uint8_t index, Owner* owner)
  {
    ASSERT(index < InstanceCount);
    ASSERT(map_[index] == nullptr);
    map_[index] = owner;
  }

  static void Unregister(uint8_t index, Owner* owner)
  {
    if (index < InstanceCount && map_[index] == owner)
    {
      map_[index] = nullptr;
    }
  }

  static Owner* Get(uint8_t index)
  {
    if (index >= InstanceCount)
    {
      return nullptr;
    }
    return map_[index];
  }

 private:
  inline static Owner* map_[InstanceCount] = {};
};

#if LIBXR_HPM_MCAN_CORE_SUPPORTED
inline constexpr uint32_t kMcanRxInterruptMask =
    MCAN_INT_RXFIFO0_NEW_MSG | MCAN_INT_RXFIFO1_NEW_MSG;
inline constexpr uint32_t kMcanTxInterruptMask =
    MCAN_EVENT_TRANSMIT | MCAN_INT_TXFIFO_EMPTY;
inline constexpr uint32_t kMcanErrorInterruptMask = MCAN_EVENT_ERROR;
inline constexpr uint32_t kMcanInterruptMask =
    kMcanRxInterruptMask | kMcanTxInterruptMask | kMcanErrorInterruptMask;
inline constexpr uint32_t kMcanRxFaultMask =
    MCAN_INT_RXFIFO0_FULL | MCAN_INT_RXFIFO1_FULL | MCAN_INT_RXFIFO0_MSG_LOST |
    MCAN_INT_RXFIFO1_MSG_LOST | MCAN_INT_MSG_RAM_ACCESS_FAILURE;
inline constexpr uint32_t kMcanRxFifo0ActivityMask =
    MCAN_INT_RXFIFO0_NEW_MSG | MCAN_INT_RXFIFO0_FULL | MCAN_INT_RXFIFO0_MSG_LOST;
inline constexpr uint32_t kMcanRxFifo1ActivityMask =
    MCAN_INT_RXFIFO1_NEW_MSG | MCAN_INT_RXFIFO1_FULL | MCAN_INT_RXFIFO1_MSG_LOST;

ErrorCode ConvertMcanStatus(hpm_stat_t status);
bool HasLowLevelTiming(const CAN::BitTiming& timing);
bool HasLowLevelTiming(const FDCAN::DataBitTiming& timing);
uint16_t SamplePointToPermille(float sample_point);
mcan_node_mode_t ConvertMcanMode(const CAN::Mode& mode);
void ApplyLowLevelTiming(const CAN::BitTiming& src, mcan_bit_timing_param_t& dst);
void ApplyLowLevelTiming(const FDCAN::DataBitTiming& src, mcan_bit_timing_param_t& dst);
CAN::ErrorID ConvertMcanProtocolError(mcan_last_err_code_t code);
uint32_t AcquireMcanClock(clock_name_t clock);
void PrepareMcanCommonConfig(MCAN_Type* can, mcan_config_t& config, bool enable_canfd);
void PrepareMcanAcceptAllFilters(mcan_config_t& config);
void ShutdownMcan(MCAN_Type* can, uint32_t irq, bool auto_enable_irq,
                  uint32_t interrupt_mask);
void EnableMcanInterrupts(MCAN_Type* can, uint32_t irq, bool auto_enable_irq,
                          uint32_t interrupt_mask);
ErrorCode ReadMcanErrorState(MCAN_Type* can, CAN::ErrorState& state);
size_t HardwareTxQueueEmptySize(MCAN_Type* can);
bool AcquireSharedMcanOwnership(uint8_t index, void* owner, HpmMcanOwnerKind kind);
void ReleaseSharedMcanOwnership(uint8_t index, void* owner, HpmMcanOwnerKind kind);

template <typename FrameConsumer, typename ErrorConsumer>
void DrainMcanRxFifo(MCAN_Type* can, uint32_t fifo_index, FrameConsumer&& on_frame,
                     ErrorConsumer&& on_error)
{
  while (true)
  {
    mcan_rx_message_t frame{};
    const hpm_stat_t status = mcan_read_rxfifo(can, fifo_index, &frame);
    if (status == status_mcan_rxfifo_empty)
    {
      break;
    }
    if (status != status_success)
    {
      on_error();
      break;
    }
    on_frame(frame);
  }
}

template <typename RxHandler, typename ErrorEmitter, typename TxHandler,
          typename ErrorHandler>
void ProcessMcanInterrupt(MCAN_Type* can, bool configured, bool in_isr,
                          RxHandler&& on_rx_fifo, ErrorEmitter&& emit_other_error,
                          TxHandler&& on_tx, ErrorHandler&& on_error)
{
  if (!configured || can == nullptr)
  {
    return;
  }

  const uint32_t flags = mcan_get_interrupt_flags(can);
  if (flags == 0U)
  {
    return;
  }

  mcan_clear_interrupt_flags(can, flags);

  if ((flags & kMcanRxFifo0ActivityMask) != 0U)
  {
    on_rx_fifo(0U, flags, in_isr);
  }
  if ((flags & kMcanRxFifo1ActivityMask) != 0U)
  {
    on_rx_fifo(1U, flags, in_isr);
  }
  if ((flags & kMcanRxFaultMask) != 0U)
  {
    emit_other_error(in_isr);
  }
  if ((flags & kMcanTxInterruptMask) != 0U)
  {
    on_tx();
  }
  if ((flags & kMcanErrorInterruptMask) != 0U)
  {
    on_error(flags & kMcanErrorInterruptMask, in_isr);
    on_tx();
  }
}
#endif

}  // namespace detail

/**
 * @class HPMCANFD
 * @brief HPM MCAN FDCAN 椹卞姩閫傞厤鍣紝閫傞厤 LibXR FDCAN 鎺ュ彛 /
 * HPM MCAN FDCAN adapter for the LibXR FDCAN interface.
 *
 * @details
 * 璇ョ被鍦?`MCAN_Type` 涓婂疄鐜?`LibXR::FDCAN`锛屽湪 classic frame
 * 涔嬪杩樻敮鎸?FD frame銆? * BRS銆丒SI銆乀DC 绛?FDCAN
 * 璇箟銆傚畠涓庡悓鏂囦欢涓殑 `HPMCAN` 鍏变韩鍚屼竴濂?MCAN
 * 澶栬妯″瀷锛? * 浣嗗悓涓€涓?MCAN instance
 * 涓婁笉鍏佽鍏卞瓨銆?
 * * This class implements `LibXR::FDCAN` on top of `MCAN_Type` and supports FD frame,
 * BRS, ESI, TDC, and related FDCAN semantics in addition to classic frames. It shares
 * the same MCAN peripheral model as the `HPMCAN` adapter from `hpm_can.*`; callers
 * should not instantiate both wrappers for the same MCAN instance.
 */
class HPMCANFD : public FDCAN
{
 public:
  static constexpr uint32_t kInvalidIrq = 0xFFFFFFFFu;
  static constexpr uint32_t kDefaultTxPoolSize = 8;

#if LIBXR_HPM_MCAN_SUPPORTED
  static constexpr uint8_t kMaxInstances = MCAN_SOC_MAX_COUNT;
  using McanRegistry = detail::HpmMcanInstanceRegistry<HPMCANFD, kMaxInstances>;
#else
  static constexpr uint8_t kMaxInstances = 1;
#endif

  /**
   * @brief 鏋勯€?MCAN FDCAN wrapper / Construct the MCAN FDCAN wrapper.
   * @param can HPM `MCAN_Type` 瀹炰緥锛泆nsupported SoC
   * 涓婁负鍗犱綅鎸囬拡骞惰繑鍥?`NOT_SUPPORT` / HPM `MCAN_Type` instance; on unsupported
   * SoCs this is a placeholder pointer and APIs return `NOT_SUPPORT`.
   * @param clock HPM SDK `clock_name_t`锛岀敤浜?clock group銆乣clock_get_frequency()` 鍜?
   * * `mcan_init()` / HPM SDK `clock_name_t` used for the clock group,
   * `clock_get_frequency()`, and `mcan_init()`.
   * @param index MCAN instance 绱㈠紩锛屽繀椤诲皬浜?`MCAN_SOC_MAX_COUNT` / MCAN instance
   * index, which must be lower than `MCAN_SOC_MAX_COUNT`.
   * @param irq 澶栬 IRQ 鍙凤紱涓?`kInvalidIrq` 鏃朵笉鑷姩鎵撳紑 NVIC /
   * Peripheral IRQ number; `kInvalidIrq` disables automatic NVIC enable.
   * @param auto_enable_irq 鏄惁鐢遍€傞厤鍣ㄦ竻鏍囧織骞舵墦寮€ IRQ / Whether
   * the adapter clears flags and enables IRQs.
   * @param queue_size classic 鍜?FD 杞欢鍙戦€侀槦鍒楁繁搴︼紝蹇呴』澶т簬 0 /
   * Classic and FD software TX queue depth, which must be greater than 0.
   *
   * @note HPM5301 `MCAN_SOC_MAX_COUNT ==
   * 0`锛屽洜姝ゆ湰妯℃澘鍙潤鎬佺紪璇?unsupported wrapper锛?   * FD
   * 琛屼负闇€鍦ㄥ甫 MCAN IP 鐨?SoC 涓婇獙璇併€?/ HPM5301 has `MCAN_SOC_MAX_COUNT == 0`,
   * so this template only statically builds the unsupported wrapper; FD behavior needs
   * validation on a SoC with MCAN IP.
   */
  HPMCANFD(LibXRHpmCanFdType* can, clock_name_t clock, uint8_t index = 0,
           uint32_t irq = kInvalidIrq, bool auto_enable_irq = true,
           uint32_t queue_size = kDefaultTxPoolSize, void* msg_buf = nullptr,
           uint32_t msg_buf_size = 0);

  /**
   * @brief 鏋愭瀯骞堕噴鏀?MCAN/FDCAN 鐘舵€?/ Destruct and release MCAN/FDCAN state.
   *
   * @note 鏀寔 MCAN IP 鏃朵細鍏抽棴涓柇銆佹竻鐞?registry
   * 骞堕噴鏀惧叡浜?ownership銆?/ When MCAN IP is supported, interrupts are disabled, the
   * registry is cleared, and shared ownership is released.
   */
  ~HPMCANFD() override;

  /**
   * @brief 杞婚噺鍒濆鍖栨鏌?/ Perform the lightweight initialization check.
   * @return 鏀寔 MCAN IP 涓旂‖浠舵寚閽堟湁鏁堟椂杩斿洖
   * `OK`锛涚┖鎸囬拡杩斿洖 `PTR_NULL`锛泆nsupported SoC 杩斿洖 `NOT_SUPPORT`銆?/ Returns
   * `OK` when MCAN IP is supported and the hardware pointer is valid, `PTR_NULL` for null
   * hardware, and `NOT_SUPPORT` on unsupported SoCs.
   */
  ErrorCode Init(void);

  /**
   * @brief Set MCAN message RAM before configuration.
   *
   * The buffer should be placed in `.ahb_sram` on HPM SoCs whose MCAN message
   * RAM is backed by AHB RAM.
   */
  ErrorCode SetMessageBuffer(void* msg_buf, uint32_t msg_buf_size);

  /**
   * @brief 鎸?classic CAN 閰嶇疆 MCAN/FDCAN wrapper / Configure the MCAN/FDCAN wrapper
   * with classic CAN settings.
   * @param cfg LibXR classic CAN 閰嶇疆锛屼細鎻愬崌涓?`FDCAN::Configuration`
   * 涓斿叧闂?FD銆?/ LibXR classic CAN configuration, promoted to `FDCAN::Configuration`
   * with FD disabled.
   * @return 閫忎紶 `SetConfig(const FDCAN::Configuration&)` 鐨勭粨鏋?/ Returns the result
   * of `SetConfig(const FDCAN::Configuration&)`.
   */
  ErrorCode SetConfig(const CAN::Configuration& cfg) override;

  /**
   * @brief 閰嶇疆 MCAN classic/FD 宸ヤ綔鍙傛暟 / Configure MCAN classic/FD operating
   * parameters.
   * @param cfg LibXR FDCAN 閰嶇疆锛沶ominal/data bitrate銆乻ample point銆丅RS銆丒SI
   * 鍜?TDC 鏄犲皠鍒?`mcan_config_t`銆?/ LibXR FDCAN configuration; nominal/data bitrate,
   * sample point, BRS, ESI, and TDC map to `mcan_config_t`.
   * @return 鎴愬姛杩斿洖 `OK`锛涚┖鎸囬拡杩斿洖 `PTR_NULL`锛屾棤鏁?mode/timing 杩斿洖
   * `ARG_ERR` 鎴?   * `NOT_SUPPORT`锛孲DK 鐘舵€佹寜 `status_mcan_*` 杞崲銆?/
   * Returns `OK` on success; returns `PTR_NULL` for null hardware, `ARG_ERR` or
   * `NOT_SUPPORT` for invalid mode/timing, and maps SDK `status_mcan_*` values.
   *
   * @note 璋冪敤 `mcan_get_default_config()` 鍜?`mcan_init()`锛汻X FIFO銆乀X
   * FIFO銆丒CR/PSR銆?   * DLC/message RAM 琛屼负浠嶉渶涓婃澘楠岃瘉銆?/ Calls
   * `mcan_get_default_config()` and `mcan_init()`; RX FIFO, TX FIFO, ECR/PSR, DLC, and
   * message RAM behavior still need hardware validation.
   */
  ErrorCode SetConfig(const FDCAN::Configuration& cfg) override;

  /**
   * @brief 杩斿洖 MCAN 澶栬杈撳叆鏃堕挓 / Return the MCAN peripheral input
   * clock.
   * @return 鏀寔 MCAN IP 鏃惰繑鍥?`clock_get_frequency(clock_)`锛寀nsupported
   * SoC 杩斿洖 0銆?/ Returns `clock_get_frequency(clock_)` when MCAN IP is supported, and
   * 0 on unsupported SoCs.
   */
  uint32_t GetClockFreq() const override;

  /**
   * @brief 灏?classic CAN 甯у姞鍏ュ彂閫侀槦鍒?/ Queue a classic CAN frame.
   * @param pack LibXR classic CAN 甯э紱DLC
   * 蹇呴』涓嶅ぇ浜?8锛岄敊璇抚涓嶈兘鍙戦€併€?/ LibXR classic CAN
   * frame; DLC must be no greater than 8, and error frames cannot be transmitted.
   * @return 鎴愬姛鍏ラ槦杩斿洖 `OK`锛涙湭閰嶇疆杩斿洖 `INIT_ERR`锛岄潪娉曞抚杩斿洖
   * `ARG_ERR`锛岄槦鍒楁弧杩斿洖 `FULL`锛寀nsupported SoC 杩斿洖 `NOT_SUPPORT`銆?/ Returns
   * `OK` when queued; returns `INIT_ERR` if not configured, `ARG_ERR` for invalid frames,
   * `FULL` when the queue is full, and `NOT_SUPPORT` on unsupported SoCs.
   */
  ErrorCode AddMessage(const ClassicPack& pack) override;

  /**
   * @brief 灏?FD 甯у姞鍏ュ彂閫侀槦鍒?/ Queue an FD frame.
   * @param pack LibXR FD 甯э紱浠呮敮鎸佹爣鍑?鎵╁睍鏁版嵁甯э紝闀垮害蹇呴』涓嶅ぇ浜?64銆?/
   * LibXR FD frame; only standard/extended data frames are supported, and length must be
   * no greater than 64.
   * @return 鎴愬姛鍏ラ槦杩斿洖 `OK`锛涙湭閰嶇疆杩斿洖 `INIT_ERR`锛孎D
   * 鏈惎鐢ㄨ繑鍥?`NOT_SUPPORT`锛岄潪娉曞抚 杩斿洖
   * `ARG_ERR`锛岄槦鍒楁弧杩斿洖 `FULL`銆?/ Returns `OK` when queued; returns `INIT_ERR`
   * if not configured, `NOT_SUPPORT` if FD is disabled, `ARG_ERR` for invalid frames,
   * and `FULL` when the queue is full.
   */
  ErrorCode AddMessage(const FDPack& pack) override;

  /**
   * @brief 璇诲彇 MCAN 閿欒鐘舵€?/ Read MCAN error state.
   * @param state 杈撳嚭 LibXR 閿欒璁℃暟鍜?bus-off/passive/warning 鐘舵€?/ Output
   * LibXR error counters and bus-off/passive/warning state.
   * @return 鎴愬姛杩斿洖 `OK`锛涚┖鎸囬拡杩斿洖 `PTR_NULL`锛泆nsupported SoC 杩斿洖
   * `NOT_SUPPORT`銆?/ Returns `OK` on success, `PTR_NULL` for null hardware, and
   * `NOT_SUPPORT` on unsupported SoCs.
   */
  ErrorCode GetErrorState(CAN::ErrorState& state) const override;

  /**
   * @brief 鏌ヨ纭欢 TX FIFO 绌洪棽妲芥暟 / Query free slots in the hardware
   * TX FIFO.
   * @return 鏀寔 MCAN IP 鏃惰繑鍥?`TXFQS`/SDK helper
   * 璁＄畻鍑虹殑绌洪棽妲芥暟锛寀nsupported SoC 杩斿洖 0銆?/ Returns the free-slot count
   * calculated from `TXFQS`/SDK helper when MCAN IP is supported, and 0 on unsupported
   * SoCs.
   */
  size_t HardwareTxQueueEmptySize() const;

  /**
   * @brief 澶勭悊鎸囧畾 RX FIFO 鐨勬帴鏀朵腑鏂?/ Process receive interrupts for one RX
   * FIFO.
   * @param fifo RX FIFO 绱㈠紩锛岄€氬父涓?0 鎴?1 / RX FIFO index, usually 0 or 1.
   *
   * @note 浣跨敤 `mcan_read_rxfifo()` drain FIFO锛屽苟鎸?classic/FD frame 绫诲瀷娲惧彂
   * LibXR 鍥炶皟銆?   * / Uses `mcan_read_rxfifo()` to drain the FIFO and dispatch LibXR
   * callbacks by classic/FD frame type.
   */
  void ProcessRxInterrupt(uint32_t fifo);

  /**
   * @brief 澶勭悊 MCAN error/status 涓柇 / Process MCAN error/status interrupts.
   * @param error_status_its 鏈涓柇鍛戒腑鐨?MCAN error/status flags / MCAN
   * error/status flags hit by this interrupt.
   *
   * @note 璇诲彇 `mcan_get_protocol_status()`锛屽皢 bus-off/passive/warning/protocol
   * error 杞崲涓?LibXR error frame銆?/ Reads `mcan_get_protocol_status()` and
   * converts bus-off/passive/warning/protocol errors to LibXR error frames.
   */
  void ProcessErrorStatusInterrupt(uint32_t error_status_its);

  /**
   * @brief 澶勭悊 MCAN 缁煎悎涓柇 / Process combined MCAN interrupts.
   * @param in_isr 鏍囪褰撳墠璋冪敤鏄惁鏉ヨ嚜 ISR / Marks whether the call is
   * from ISR context.
   */
  void ProcessInterrupt(bool in_isr = true);

  /**
   * @brief C ISR trampoline 鐨勬寜绱㈠紩鍏ュ彛 / Indexed entry used by the C ISR
   * trampoline.
   * @param index MCAN instance 绱㈠紩 / MCAN instance index.
   */
  static void OnInterrupt(uint8_t index);

  /**
   * @brief 灏?LibXR classic frame 杞负 HPM `mcan_tx_frame_t` / Convert a LibXR
   * classic frame to HPM `mcan_tx_frame_t`.
   * @param pack 杈撳叆 classic frame / Input classic frame.
   * @param frame 杈撳嚭 HPM MCAN TX frame / Output HPM MCAN TX frame.
   */
  static inline void BuildTxFrame(const ClassicPack& pack, mcan_tx_frame_t& frame);

  /**
   * @brief 灏?LibXR FD frame 杞负 HPM `mcan_tx_frame_t` / Convert a LibXR FD frame
   * to HPM `mcan_tx_frame_t`.
   * @param pack 杈撳叆 FD frame / Input FD frame.
   * @param frame 杈撳嚭 HPM MCAN TX frame / Output HPM MCAN TX frame.
   *
   * @note DLC 浣跨敤 HPM SDK 鐨?8/12/16/20/24/32/48/64 瀛楄妭缂栫爜锛涘疄闄?message RAM
   * 灏哄闇€
   * 涓婃澘纭銆?/ DLC uses the HPM SDK 8/12/16/20/24/32/48/64-byte encoding;
   * actual message RAM sizing needs hardware validation.
   */
  static inline void BuildTxFrame(const FDPack& pack, mcan_tx_frame_t& frame);

  /**
   * @brief 鏈嶅姟杞欢鍙戦€侀槦鍒楀埌 MCAN TX FIFO / Service software TX queues
   * into the MCAN TX FIFO.
   *
   * @note FD 闃熷垪浼樺厛浜?classic
   * 闃熷垪锛屾渶缁堣皟鐢?`mcan_transmit_via_txfifo_nonblocking()`銆?   * / The FD queue
   * is served before the classic queue, and the final call is
   * `mcan_transmit_via_txfifo_nonblocking()`.
   */
  void TxService();

 private:
  static ErrorCode ConvertStatus(hpm_stat_t status);
  static bool HasLowLevelTiming(const CAN::BitTiming& timing);
  static bool HasLowLevelTiming(const FDCAN::DataBitTiming& timing);
  static uint16_t SamplePointToPermilleX10(float sample_point);
  static uint8_t DlcToBytes(uint8_t dlc);
  static uint8_t BytesToDlc(uint8_t bytes);
  ErrorCode ApplyMessageBuffer();
  void Shutdown();
  static bool BuildRxPack(const mcan_rx_message_t& frame, ClassicPack& pack);
  static bool BuildRxPack(const mcan_rx_message_t& frame, FDPack& pack);
  static CAN::ErrorID ConvertProtocolError(mcan_last_err_code_t code);
  void EmitErrorFrame(CAN::ErrorID error_id, bool in_isr);

  LibXRHpmCanFdType* can_;
  clock_name_t clock_;
  uint8_t index_;
  uint32_t irq_;
  bool auto_enable_irq_;
  void* msg_buf_{nullptr};
  uint32_t msg_buf_size_{0};
  bool ownership_acquired_ = false;
  bool configured_ = false;
  bool fd_enabled_ = false;
  bool brs_enabled_ = false;
  bool esi_enabled_ = false;

  std::atomic<uint32_t> tx_lock_{0};
  std::atomic<uint32_t> tx_pend_{0};

  LockFreePool<ClassicPack> tx_pool_;
  LockFreePool<FDPack> tx_pool_fd_;

  struct
  {
    mcan_rx_message_t frame;
    ClassicPack pack;
    FDPack pack_fd;
  } rx_buff_;

  struct
  {
    mcan_tx_frame_t frame;
  } tx_buff_;
};

}  // namespace LibXR

extern "C" void libxr_hpm_mcan_process_interrupt(uint8_t index);
