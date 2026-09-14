/**
 * @file test_fdcan.hpp
 * @brief CAN FD 内部回环测试 / CAN FD internal loopback test.
 *
 * 逐帧检查标准、扩展数据帧的 ID、FD 编码长度、有效数据及帧路由。
 * Check standard/extended IDs, canonical FD lengths, payload and FD routing; detect extra
 * callbacks, with one outstanding frame. Retain the test object until reset.
 */
#pragma once

#include <atomic>

#include "can.hpp"
#include "spsc_queue.hpp"
#include "test_assert.hpp"
#include "thread.hpp"

namespace LibXR::Test
{
/**
 * @brief 长期保留的 CAN FD 回环测试对象 / Caller-retained CAN FD loopback test object.
 * @pre 调用方配置 CAN FD、相位时序和内部回环，允许接收全部标准和扩展 FD 数据帧。
 *      Configure CAN FD, phase timing and internal loopback; reserve the controller and
 * accept standard and extended FD data frames.
 * @pre 注册前没有旧帧或在途回调；后端串行调用接收回调，作为队列的唯一生产者。
 *      Start without old frames or callbacks in flight; the backend must serialize
 *      receive callbacks as the queue's sole producer.
 * @pre CAN 和测试对象地址保持不变并保留到复位，当前接口不能注销订阅。
 *      Keep CAN and this object at stable addresses until reset; subscriptions cannot be
 * removed.
 */
class FDCANLoopbackTest
{
 public:
  explicit FDCANLoopbackTest(FDCAN& can) : can_(can)
  {
    auto callback = FDCAN::CallbackFD::Create(
        [](bool, FDCANLoopbackTest* self, const FDCAN::FDPack& frame)
        {
          // 帧引用只在回调期间有效，先复制，再发布完成计数。
          // Copy the ephemeral frame before publishing the completion count.
          if (self->received_.Push(frame) != ErrorCode::OK)
          {
            self->overflow_.store(1, std::memory_order_relaxed);
          }
          self->calls_.fetch_add(1, std::memory_order_release);
        },
        this);
    can_.Register(callback, CAN::Type::STANDARD);
    can_.Register(callback, CAN::Type::EXTENDED);
    auto unexpected = CAN::Callback::Create(
        [](bool, FDCANLoopbackTest* self, const CAN::ClassicPack&)
        { self->unexpected_classic_.store(1, std::memory_order_release); }, this);
    // 短 FD 帧也必须走 FD 回调；经典帧或错误事件均不是本次期望的数据。
    // Even short FD frames must use FD callbacks; classic frames/errors are unexpected.
    CAN& classic = can_;
    classic.Register(unexpected, CAN::Type::STANDARD);
    classic.Register(unexpected, CAN::Type::EXTENDED);
    classic.Register(unexpected, CAN::Type::ERROR);
  }
  FDCANLoopbackTest(const FDCANLoopbackTest&) = delete;
  FDCANLoopbackTest& operator=(const FDCANLoopbackTest&) = delete;

  /**
   * @brief 逐帧发送并核对内部回环结果 / Send and verify internal loopback frame by frame.
   * @pre 系统已初始化，从普通任务调用；每个对象只运行一次。
   *      Initialize the system, call from normal task context, and run once per object.
   * @param timeout_ms 每帧提交后等待接收的上限 / Receive deadline after each submission.
   * @param iterations 轮数，每轮 32 帧 / Rounds, each containing 32 frames.
   */
  void Run(uint32_t timeout_ms = 1000, uint32_t iterations = 1000)
  {
    TEST_ASSERT(timeout_ms > 0 && timeout_ms < UINT32_MAX / 2U);
    TEST_ASSERT(iterations > 0 && iterations <= UINT32_MAX / 32U);
    CheckEmpty(0);
    uint32_t sequence = 0;
    // 只使用可直接编码的 FD 长度，不规定其他长度的补齐策略。
    // Use canonical FD lengths without imposing a padding policy for other lengths.
    const uint8_t lengths[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};
    for (uint32_t round = 0; round < iterations; ++round)
    {
      for (unsigned extended = 0; extended < 2; ++extended)
      {
        const uint32_t id_mask = extended ? 0x1FFFFFFFU : 0x7FFU;
        for (uint8_t length : lengths)
        {
          FDCAN::FDPack sent{};
          const auto type = extended ? CAN::Type::EXTENDED : CAN::Type::STANDARD;
          const uint32_t id = (round == 0 && length < 2)
                                  ? (length == 0 ? 0U : id_mask)
                                  : (sequence * 0x1F123BB5U) & id_mask;
          sent.type = type;
          sent.id = id;
          sent.len = length;
          for (uint8_t i = 0; i < length; ++i)
          {
            sent.data[i] = static_cast<uint8_t>(sequence + i * 29U);
          }
          CheckEmpty(sequence);
          TEST_ASSERT(can_.AddMessage(sent) == ErrorCode::OK);
          const uint32_t start = Thread::GetTime();
          for (;;)
          {
            const uint32_t calls = calls_.load(std::memory_order_acquire);
            TEST_ASSERT(overflow_.load(std::memory_order_relaxed) == 0);
            TEST_ASSERT(unexpected_classic_.load(std::memory_order_acquire) == 0);
            TEST_ASSERT(calls <= sequence + 1U);
            if (calls == sequence + 1U)
            {
              break;
            }
            TEST_ASSERT(Thread::GetTime() - start < timeout_ms);
            Thread::Sleep(1);
          }
          FDCAN::FDPack received{};
          TEST_ASSERT(received_.Pop(received) == ErrorCode::OK);
          TEST_ASSERT(sent.type == type && sent.id == id && sent.len == length);
          TEST_ASSERT(received.type == type && received.id == id &&
                      received.len == length);
          // len 之外的数据没有意义，零长度帧不比较 payload。
          // Bytes beyond len are unspecified; zero-length frames have no payload to
          // compare.
          for (uint8_t i = 0; i < length; ++i)
          {
            const auto value = static_cast<uint8_t>(sequence + i * 29U);
            TEST_ASSERT(sent.data[i] == value && received.data[i] == value);
          }
          CheckEmpty(++sequence);
        }
      }
    }
    // 有限观察额外回调，不把它当作注销或等待所有未来回调的接口。
    // Observe extra callbacks briefly; this is not unsubscribe or a future-callback
    // barrier.
    Thread::Sleep(1);
    CheckEmpty(sequence);
  }

 private:
  void CheckEmpty(uint32_t expected_calls) const
  {
    TEST_ASSERT(calls_.load(std::memory_order_acquire) == expected_calls);
    TEST_ASSERT(overflow_.load(std::memory_order_relaxed) == 0);
    TEST_ASSERT(received_.Size() == 0);
    TEST_ASSERT(unexpected_classic_.load(std::memory_order_acquire) == 0);
  }

  FDCAN& can_;
  SPSCQueue<FDCAN::FDPack> received_{2};
  std::atomic<uint32_t> calls_{0};
  std::atomic<uint32_t> overflow_{0};
  std::atomic<uint32_t> unexpected_classic_{0};
};
}  // namespace LibXR::Test
