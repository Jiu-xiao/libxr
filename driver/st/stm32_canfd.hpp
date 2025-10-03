#pragma once

#include <cstdint>
#include <type_traits>
#include <utility>

#include "libxr_def.hpp"
#include "main.h"

#ifdef HAL_FDCAN_MODULE_ENABLED

#ifdef FDCAN
#undef FDCAN
#endif

#include "can.hpp"
#include "libxr.hpp"

#ifndef FDCAN_MESSAGE_RAM_WORDS_MAX
#define FDCAN_MESSAGE_RAM_WORDS_MAX 2560u
#endif

typedef enum : uint8_t
{
#ifdef FDCAN1
  STM32_FDCAN1,
#endif
#ifdef FDCAN2
  STM32_FDCAN2,
#endif
#ifdef FDCAN3
  STM32_FDCAN3,
#endif
  STM32_FDCAN_NUMBER,
  STM32_FDCAN_ID_ERROR
} stm32_fdcan_id_t;

stm32_fdcan_id_t STM32_FDCAN_GetID(FDCAN_GlobalTypeDef* addr);  // NOLINT

template <typename, typename = void>
struct HasTxFifoQueueElmtsNbr : std::false_type
{
};

template <typename T>
struct HasTxFifoQueueElmtsNbr<
    T, std::void_t<decltype(std::declval<T>()->Init.TxFifoQueueElmtsNbr)>>
    : std::true_type
{
};

// 当没有TxFifoQueueElmtsNbr成员时的处理函数
template <typename T>
typename std::enable_if<!HasTxFifoQueueElmtsNbr<T>::value, uint32_t>::type
GetTxFifoTotalElements(T& hcan)  // NOLINT
{
  UNUSED(hcan);
  return 3;
}

// 当有TxFifoQueueElmtsNbr成员时的处理函数
template <typename T>
typename std::enable_if<HasTxFifoQueueElmtsNbr<T>::value, uint32_t>::type
GetTxFifoTotalElements(T& hcan)  // NOLINT
{
  return hcan->Init.TxFifoQueueElmtsNbr;
}

namespace LibXR
{
/**
 * @brief STM32CANFD 类，用于处理 STM32 系统的 CANFD 通道。 Provides handling for STM32
 * CANFD
 *
 */
class STM32CANFD : public FDCAN
{
 public:
  template <typename, typename = void>
  struct HasMessageRAMOffset : std::false_type
  {
  };

  template <typename T>
  struct HasMessageRAMOffset<
      T, std::void_t<decltype(std::declval<T>()->Init.MessageRAMOffset)>> : std::true_type
  {
  };

  template <typename T>
  typename std::enable_if<!HasMessageRAMOffset<T>::value, int>::type
  CheckMessageRAMOffset(T&, uint32_t = FDCAN_MESSAGE_RAM_WORDS_MAX)
  {
    return 0;
  }

  template <typename T>
  typename std::enable_if<HasMessageRAMOffset<T>::value, int>::type CheckMessageRAMOffset(
      T& fdcan_handle, uint32_t max_words = FDCAN_MESSAGE_RAM_WORDS_MAX)
  {
    // NOLINTNEXTLINE
    static const auto FDCAN_ElmtWords = [](uint32_t sz) -> uint32_t
    {
      switch (sz)
      {
        case FDCAN_DATA_BYTES_8:
          return 4;  // 2 words header + 2 words data(0..8B)
        case FDCAN_DATA_BYTES_12:
          return 5;
        case FDCAN_DATA_BYTES_16:
          return 6;
        case FDCAN_DATA_BYTES_20:
          return 7;
        case FDCAN_DATA_BYTES_24:
          return 8;
        case FDCAN_DATA_BYTES_32:
          return 10;
        case FDCAN_DATA_BYTES_48:
          return 14;
        case FDCAN_DATA_BYTES_64:
          return 18;
        default:
          return 4;
      }
    };

    const uint32_t TX_FIFO_ELEMS =
        HasTxFifoQueueElmtsNbr<T>::value ? fdcan_handle->Init.TxFifoQueueElmtsNbr : 0u;

    // NOLINTNEXTLINE
    const auto FDCAN_MessageRAMWords = [&](const FDCAN_InitTypeDef& c) -> uint32_t
    {
      return c.StdFiltersNbr * 1u + c.ExtFiltersNbr * 2u +
             c.RxFifo0ElmtsNbr * FDCAN_ElmtWords(c.RxFifo0ElmtSize) +
             c.RxFifo1ElmtsNbr * FDCAN_ElmtWords(c.RxFifo1ElmtSize) +
             c.RxBuffersNbr * FDCAN_ElmtWords(c.RxBufferSize) + c.TxEventsNbr * 2u +
             (c.TxBuffersNbr + TX_FIFO_ELEMS) * FDCAN_ElmtWords(c.TxElmtSize);
    };

    static struct
    {
      bool inited;
      uint32_t offset;
      uint32_t size;
    } offset_map[STM32_FDCAN_NUMBER] = {};  // NOLINT

    auto id = STM32_FDCAN_GetID(fdcan_handle->Instance);
    ASSERT(id >= 0 && id < STM32_FDCAN_NUMBER);

    offset_map[id].offset = fdcan_handle->Init.MessageRAMOffset;
    offset_map[id].size = FDCAN_MessageRAMWords(fdcan_handle->Init);

    const uint32_t START = offset_map[id].offset;
    const uint32_t END = START + offset_map[id].size;

    /* 内存越界 */
    ASSERT(START <= max_words);
    ASSERT(END <= max_words);

    for (auto& it : offset_map)
    {
      if (!it.inited)
      {
        continue;
      }

      const uint32_t A0 = it.offset, A1 = it.offset + it.size;
      const uint32_t B0 = START, B1 = END;
      if (A0 < B1 && B0 < A1)
      {
        /* 内存区域重叠 */
        ASSERT(false);
      }
    }

    offset_map[id].inited = true;
    return 0;
  }

  /**
   * @brief STM32CANFD 类，用于处理 STM32 系统的 CANFD 通道。 Provides handling for STM32
   * CANFD
   *
   * @param hcan STM32CANFD对象 CAN object
   * @param queue_size 发送队列大小 Send queue size
   */
  STM32CANFD(FDCAN_HandleTypeDef* hcan, uint32_t queue_size);

  /**
   * @brief 初始化
   *
   * @return ErrorCode
   */
  ErrorCode Init(void);

  ErrorCode AddMessage(const ClassicPack& pack) override;

  ErrorCode AddMessage(const FDPack& pack) override;

  /**
   * @brief 处理接收中断
   *
   * @param fifo 接收缓冲区号 Receive buffer number
   */
  void ProcessRxInterrupt(uint32_t fifo);

  /**
   * @brief 处理发送中断
   *
   */
  void ProcessTxInterrupt();

  /**
   * @brief 获取硬件发送队列空闲大小
   *
   * @return size_t 硬件发送队列空闲大小
   */
  size_t HardwareTxQueueEmptySize()
  {
    auto free = HAL_FDCAN_GetTxFifoFreeLevel(hcan_);
    return free;
  }

  FDCAN_HandleTypeDef* hcan_;

  stm32_fdcan_id_t id_;
  LockFreePool<ClassicPack> tx_pool_;
  LockFreePool<FDPack> tx_pool_fd_;

  static STM32CANFD* map[STM32_FDCAN_NUMBER];  // NOLINT

  struct
  {
    FDCAN_RxHeaderTypeDef header;
    ClassicPack pack;
    FDPack pack_fd;
  } rx_buff_;

  struct
  {
    FDCAN_TxHeaderTypeDef header;
    ClassicPack pack;
    FDPack pack_fd;
  } tx_buff_;

  uint32_t txMailbox;
};
}  // namespace LibXR

#endif
