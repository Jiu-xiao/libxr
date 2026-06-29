/**
 * @file hpm_classic_can.hpp
 * @brief HPM classic CAN_Type IP 閫傞厤澶存枃浠?/ Adapter header for HPM classic CAN_Type
 * IP.
 *
 * @details
 * 鏈枃浠舵寜搴曞眰澶栬 IP 褰掓。锛屽彧瀵瑰簲 classic `CAN_Type`
 * IP銆傜被鍚嶄粛鎸?LibXR 鎶借薄鍛藉悕锛? * 鍥犳杩欓噷鐨?`HPMClassicCAN`
 * 琛ㄧず鈥渃lassic CAN IP + LibXR::CAN鈥濄€傝嫢鐩爣 SoC
 * 鏆撮湶鐨勬槸 `MCAN_Type`锛屽悓鍚?`HPMClassicCAN` 瀹炵幇浣嶄簬 `hpm_mcan.*`銆? * This
 * file is grouped by hardware IP and only targets the classic `CAN_Type` IP.
 * `HPMClassicCAN` is named separately so it can coexist with PR #208's `HPMCAN`
 * MCAN classic adapter. When the target exposes only `MCAN_Type`, use `HPMCAN`
 * from `hpm_can.*` or `HPMCANFD` from `hpm_mcan.*` instead.
 *
 * 澶氱郴鍒楅€夋嫨浣跨敤 HPM SDK header 涓殑
 * `MCAN_SOC_MAX_COUNT`銆乣CAN_SOC_MAX_COUNT`銆? * `HPMSOC_HAS_HPMSDK_CAN`
 * 鍜?`__has_include("hpm_can_drv.h")` 鑳藉姏瀹忥紱渚嬪 HPM6360 璧?classic
 * CAN锛孒PM6E80 璧?`hpm_mcan.*`锛屼笉鎸夌郴鍒楀悕澶嶅埗鏂囦欢銆傜己灏?classic CAN SDK
 * driver 澶存垨妯″潡瀹忔椂淇濈暀绫诲舰鐘跺苟杩斿洖 `NOT_SUPPORT`銆? * Multi-series
 * selection uses the `MCAN_SOC_MAX_COUNT`, `CAN_SOC_MAX_COUNT`, `HPMSOC_HAS_HPMSDK_CAN`,
 * and `__has_include("hpm_can_drv.h")` capability macros from HPM SDK headers. For
 * example, HPM6360 uses classic CAN, while HPM6E80 uses `hpm_mcan.*`; no series-specific
 * driver copy is used. When the classic CAN SDK driver header or module macro is missing,
 * the class shape remains available and APIs return `NOT_SUPPORT`.
 */
#pragma once

#include <atomic>

#include "can.hpp"
#include "hpm_clock_drv.h"
#include "hpm_common.h"
#include "hpm_soc.h"
#include "lockfree_pool.hpp"

#if defined(MCAN_SOC_MAX_COUNT) && (MCAN_SOC_MAX_COUNT > 0)
#include "hpm_mcan.hpp"
#else

#if defined(HPMSOC_HAS_HPMSDK_CAN) && __has_include("hpm_can_drv.h") &&           \
                                                    defined(CAN_SOC_MAX_COUNT) && \
                                                    (CAN_SOC_MAX_COUNT > 0)
#include "hpm_can_drv.h"
#define LIBXR_HPM_CLASSIC_CAN_SUPPORTED 1
using LibXRHpmClassicCanType = CAN_Type;
#else
#define LIBXR_HPM_CLASSIC_CAN_SUPPORTED 0
using LibXRHpmClassicCanType = void;
#endif

namespace LibXR
{

/**
 * @class HPMClassicCAN
 * @brief HPM classic CAN 閫傞厤鍣?/ HPM classic CAN adapter.
 *
 * @details
 * 璇ョ被鍦?classic `CAN_Type` IP 涓婂疄鐜?`LibXR::CAN`銆傚綋鍓嶆枃浠跺彧鎻愪緵
 * `HPMClassicCAN`锛? * 鑻ュ悗缁琛?classic CAN IP 涓婄殑 `LibXR::FDCAN`
 * 閫傞厤鍣紝浠嶅簲缁х画鏀惧湪 `hpm_can.*`锛? *
 * 鍥犱负鏂囦欢褰掑睘鎸?IP锛岃€屼笉鏄寜鏄惁鍚敤 FD銆? * This class implements
 * `LibXR::CAN` on top of the classic `CAN_Type` IP. This file currently provides only
 * `HPMClassicCAN` for non-MCAN `CAN_Type` peripherals, avoiding class-name overlap with
 * PR #208's MCAN `HPMCAN`.
 */
class HPMClassicCAN : public CAN
{
 public:
  static constexpr uint32_t kInvalidIrq = 0xFFFFFFFFu;
  static constexpr uint32_t kDefaultTxPoolSize = 8;

#if LIBXR_HPM_CLASSIC_CAN_SUPPORTED
  static constexpr uint8_t kMaxInstances = CAN_SOC_MAX_COUNT;
  static constexpr uint8_t kRxInterruptMask =
      CAN_EVENT_RECEIVE | CAN_EVENT_RX_BUF_OVERRUN | CAN_EVENT_RX_BUF_FULL;
  static constexpr uint8_t kTxInterruptMask =
      CAN_EVENT_TX_SECONDARY_BUF | CAN_EVENT_TX_PRIMARY_BUF;
  static constexpr uint8_t kErrorInterruptMask = CAN_EVENT_ERROR | CAN_EVENT_ABORT;
  static constexpr uint8_t kInterruptMask =
      kRxInterruptMask | kTxInterruptMask | kErrorInterruptMask;
  static constexpr uint8_t kCanErrorInterruptMask =
      CAN_ERROR_PASSIVE_INT_ENABLE | CAN_ERROR_ARBITRATION_LOST_INT_ENABLE |
      CAN_ERROR_BUS_ERROR_INT_ENABLE;
#else
  static constexpr uint8_t kMaxInstances = 1;
#endif

  /**
   * @brief 鏋勯€?classic CAN 閫傞厤鍣?/ Construct the classic CAN adapter.
   * @param can HPM classic `CAN_Type` 瀹炰緥锛涘湪涓嶆敮鎸?classic CAN IP 鐨?SoC
   * 涓婁负鍗犱綅 鎸囬拡涓斿悗缁?API 杩斿洖 `NOT_SUPPORT` / HPM classic `CAN_Type`
   * instance; on SoCs without classic CAN IP this is a placeholder pointer and later APIs
   * return `NOT_SUPPORT`.
   * @param clock HPM SDK `clock_name_t`锛岀敤浜?`clock_get_frequency()` 鍜?`can_init()` /
   * HPM SDK `clock_name_t` used by `clock_get_frequency()` and `can_init()`.
   * @param index 澶栬绱㈠紩锛屽繀椤诲皬浜?`CAN_SOC_MAX_COUNT` / Peripheral index,
   * which must be lower than `CAN_SOC_MAX_COUNT`.
   * @param irq 澶栬 IRQ 鍙凤紱涓?`kInvalidIrq` 鏃朵笉鑷姩鎵撳紑 NVIC / Peripheral
   * IRQ number; `kInvalidIrq` disables automatic NVIC enable.
   * @param auto_enable_irq 鏄惁鐢遍€傞厤鍣ㄦ竻鏍囧織骞舵墦寮€ IRQ / Whether the
   * adapter clears flags and enables IRQs.
   * @param tx_pool_size LibXR 鍙戦€侀槦鍒楁繁搴︼紝蹇呴』澶т簬 0 / LibXR TX queue depth,
   * which must be greater than 0.
   *
   * @note HPM5301 鐨?`CAN_SOC_MAX_COUNT` 璺熼殢 `MCAN_SOC_MAX_COUNT ==
   * 0`锛屽洜姝ゆ湰妯℃澘鍙?   * 闈欐€佺紪璇?unsupported 璺緞锛沜lassic CAN
   * 琛屼负闇€鍦ㄥ甫 `CAN_Type` 鐨?SoC 涓婇獙璇併€?/ On HPM5301, `CAN_SOC_MAX_COUNT`
   * follows `MCAN_SOC_MAX_COUNT == 0`, so this template only statically builds the
   * unsupported path; classic CAN behavior needs hardware validation on a SoC with
   * `CAN_Type`.
   */
  HPMClassicCAN(LibXRHpmClassicCanType* can, clock_name_t clock, uint8_t index = 0,
                uint32_t irq = kInvalidIrq, bool auto_enable_irq = true,
                uint32_t tx_pool_size = kDefaultTxPoolSize);

  /**
   * @brief 鏋愭瀯骞跺叧闂?IRQ/鐘舵€?/ Destruct and shut down IRQ/state.
   *
   * @note 鏀寔 classic CAN IP 鏃朵細鍏抽棴 TX/RX/error IRQ 骞舵竻鐞?instance
   * 鏄犲皠锛泆nsupported
   * 璺緞鏃犵‖浠跺壇浣滅敤銆?/ When classic CAN IP is supported, TX/RX/error IRQs are
   * disabled and the instance map is cleared; the unsupported path has no hardware side
   * effect.
   */
  ~HPMClassicCAN() override;

  /**
   * @brief 閰嶇疆 classic CAN 浣嶆椂搴忓拰宸ヤ綔妯″紡 / Configure classic CAN bit timing
   * and mode.
   * @param cfg LibXR CAN 閰嶇疆锛沗bitrate`/`sample_point` 鎴?low-level `bit_timing`
   * 鏄犲皠鍒?   * HPM SDK `can_config_t`锛宍mode`
   * 鏄犲皠鍒?normal/listen-only/loopback/one-shot 琛屼负銆?/ LibXR CAN configuration;
   * `bitrate`/`sample_point` or low-level `bit_timing` maps to HPM SDK `can_config_t`,
   * and `mode` maps to normal/listen-only/loopback/one-shot behavior.
   * @return 鎴愬姛杩斿洖 `OK`锛涚┖鎸囬拡杩斿洖 `PTR_NULL`锛岄潪娉?timing/妯″紡杩斿洖
   * `ARG_ERR` 鎴?   * `NOT_SUPPORT`锛孲DK 鍒濆鍖栧け璐ユ寜 `status_can_*` 杞负
   * LibXR 閿欒鐮併€?/ Returns `OK` on success; returns `PTR_NULL` for null
   * hardware, `ARG_ERR` or `NOT_SUPPORT` for invalid timing/mode, and maps SDK
   * initialization `status_can_*` values to LibXR error codes.
   *
   * @note 璋冪敤閾句负 `can_get_default_config()` -> accept-all filter 閰嶇疆 ->
   * `can_init()`锛?   * 涓柇璺緞浣跨敤 `CAN_EVENT_*` 涓?`CAN_ERROR_*` 鏍囧織銆?/
   * The call sequence is `can_get_default_config()` -> accept-all filter setup ->
   * `can_init()`; the interrupt path uses `CAN_EVENT_*` and `CAN_ERROR_*` flags.
   */
  ErrorCode SetConfig(const CAN::Configuration& cfg) override;

  /**
   * @brief 杩斿洖 CAN 澶栬杈撳叆鏃堕挓 / Return the CAN peripheral input
   * clock.
   * @return 鏀寔 classic CAN IP
   * 鏃惰繑鍥?`clock_get_frequency(clock_)`锛屽惁鍒欒繑鍥?0銆?/ Returns
   * `clock_get_frequency(clock_)` when classic CAN IP is supported, otherwise 0.
   */
  uint32_t GetClockFreq() const override;

  /**
   * @brief 灏?classic CAN 甯у姞鍏ュ彂閫侀槦鍒?/ Queue a classic CAN frame for
   * transmission.
   * @param pack LibXR classic CAN 甯э紱鏀寔鏍囧噯/鎵╁睍/杩滅▼甯э紝DLC
   * 蹇呴』涓嶅ぇ浜?8銆?/ LibXR classic CAN frame; standard, extended, and remote frames
   * are supported, and DLC must be no greater than 8.
   * @return 鎴愬姛鍏ラ槦杩斿洖 `OK`锛涙湭閰嶇疆杩斿洖 `INIT_ERR`锛岄潪娉曞抚杩斿洖
   * `ARG_ERR`锛岄槦鍒楁弧杩斿洖 `FULL`锛寀nsupported SoC 杩斿洖 `NOT_SUPPORT`銆?/ Returns
   * `OK` when queued; returns `INIT_ERR` if not configured, `ARG_ERR` for invalid frames,
   * `FULL` when the queue is full, and `NOT_SUPPORT` on unsupported SoCs.
   *
   * @note 鍙戦€佹湇鍔℃渶缁堣皟鐢?`can_send_message_nonblocking()`锛岀‖浠?TX
   * FIFO/鎬荤嚎浠茶缁撴灉浠嶉渶 涓婃澘瑙傚療銆?/ TX service eventually
   * calls `can_send_message_nonblocking()`; hardware TX FIFO and bus arbitration behavior
   * still need hardware observation.
   */
  ErrorCode AddMessage(const ClassicPack& pack) override;

  /**
   * @brief 璇诲彇 classic CAN 閿欒鐘舵€?/ Read classic CAN error state.
   * @param state 杈撳嚭 LibXR 閿欒璁℃暟鍜?bus-off/passive/warning 鐘舵€?/ Output LibXR
   * error counters and bus-off/passive/warning state.
   * @return 鎴愬姛杩斿洖 `OK`锛涚┖鎸囬拡杩斿洖 `PTR_NULL`锛泆nsupported SoC 杩斿洖
   * `NOT_SUPPORT`銆?/ Returns `OK` on success, `PTR_NULL` for null hardware, and
   * `NOT_SUPPORT` on unsupported SoCs.
   *
   * @note 璇佹嵁鏉ヨ嚜
   * `can_get_transmit_error_count()`銆乣can_get_receive_error_count()`銆?   *
   * `can_is_in_bus_off_mode()` 鍜?`can_get_error_interrupt_flags()`銆?/ Evidence comes
   * from `can_get_transmit_error_count()`, `can_get_receive_error_count()`,
   * `can_is_in_bus_off_mode()`, and `can_get_error_interrupt_flags()`.
   */
  ErrorCode GetErrorState(CAN::ErrorState& state) const override;

  /**
   * @brief 涓诲姩杞 RX buffer 骞舵淳鍙?LibXR 鍥炶皟 / Poll the RX buffer and
   * dispatch LibXR callbacks.
   * @param in_isr 鏍囪鍥炶皟鏄惁鍦?ISR 璇瑙﹀彂 / Marks whether callbacks are
   * emitted from ISR context.
   */
  void ProcessRx(bool in_isr = false);

  /**
   * @brief 澶勭悊 classic CAN 涓柇鏍囧織 / Process classic CAN interrupt
   * flags.
   * @param in_isr 鏍囪褰撳墠璋冪敤鏄惁鏉ヨ嚜 ISR / Marks whether the call is from
   * ISR context.
   *
   * @note 浼氬厛璇诲彇骞舵竻闄?TX/RX flags锛屽啀鏍规嵁 RX銆乀X銆乪rror 鏍囧織璋冪敤 RX
   * drain銆乀X service
   * 鍜岄敊璇抚娲惧彂銆?/ Reads and clears TX/RX flags first, then calls RX drain, TX
   * service, and error-frame dispatch according to RX, TX, and error flags.
   */
  void ProcessInterrupt(bool in_isr = true);

  /**
   * @brief C ISR trampoline 鐨勬寜绱㈠紩鍏ュ彛 / Indexed entry used by the C ISR
   * trampoline.
   * @param index HPM CAN instance 绱㈠紩 / HPM CAN instance index.
   */
  static void OnInterrupt(uint8_t index);

 private:
  static ErrorCode ConvertStatus(hpm_stat_t status);
  static ErrorCode ValidateConfig(const CAN::Configuration& cfg);
  void EmitErrorFrame(CAN::ErrorID error_id, bool in_isr);
  void Shutdown();
  static bool HasLowLevelTiming(const CAN::BitTiming& timing);
  static uint16_t SamplePointToPermille(float sample_point);
  void TxService();

#if LIBXR_HPM_CLASSIC_CAN_SUPPORTED
  static can_node_mode_t ConvertMode(const CAN::Mode& mode);
  static void ApplyLowLevelTiming(const CAN::BitTiming& src, can_bit_timing_param_t& dst);
  static void BuildTxFrame(const ClassicPack& pack, can_transmit_buf_t& frame);
  static bool BuildRxPack(const can_receive_buf_t& frame, ClassicPack& pack);
  static CAN::ErrorID ConvertProtocolError(uint8_t error_kind);
  void ProcessRxBuffer(bool in_isr);
  void ProcessError(bool in_isr);
#endif

  LibXRHpmClassicCanType* can_;
  clock_name_t clock_;
  uint8_t index_;
  uint32_t irq_;
  bool auto_enable_irq_;
  bool configured_ = false;

  LockFreePool<ClassicPack> tx_pool_;
  std::atomic<uint32_t> tx_lock_{0};
  std::atomic<uint32_t> tx_pend_{0};

#if LIBXR_HPM_CLASSIC_CAN_SUPPORTED
  static HPMClassicCAN* instance_map_[kMaxInstances];
#endif
};

}  // namespace LibXR

extern "C" void libxr_hpm_classic_can_process_interrupt(uint8_t index);

#endif
