/**
 * @file message_test_payloads.hpp
 * @brief 消息总线测试共用 payload 类型。 / Shared payload types for message-bus tests.
 *
 * 提供的测试载荷 / Provided payload shapes:
 * 1. `ByteStablePayload`：用于验证非平凡但 topic 合法 payload 的 typed 传输。 *    `ByteStablePayload`: used to verify typed topic transport for a non-trivially-copyable but topic-legal payload.
 * 2. `WideAlignedPayload`：用于验证 packet/server 的高对齐处理。 *    `WideAlignedPayload`: used to verify packet/server alignment handling.
 * 3. `PrefixIntPayload`：用于验证更短 packet payload 长度兼容路径。 *    `PrefixIntPayload`: used to verify shorter packet-length compatibility.
 *
 * 设计原理 / Design principle:
 * 1. 把 message 测试里复用的 payload 形状集中到一个头里，避免 topic 侧和 packet 侧各自漂移。 *    Keep shared message-test payload shapes in one header so topic-side and packet-side tests do not drift independently.
 */
#pragma once

#include <cstdint>
#include <type_traits>

#include "libxr.hpp"

struct ByteStablePayload
{
  float data[4];

  ByteStablePayload() : data{0.0f, 0.0f, 0.0f, 0.0f} {}

  ByteStablePayload(float a, float b, float c, float d) : data{a, b, c, d} {}

  ByteStablePayload(const ByteStablePayload& other)
      : data{other.data[0], other.data[1], other.data[2], other.data[3]}
  {
  }

  ByteStablePayload& operator=(const ByteStablePayload& other)
  {
    data[0] = other.data[0];
    data[1] = other.data[1];
    data[2] = other.data[2];
    data[3] = other.data[3];
    return *this;
  }
};

static_assert(!std::is_trivially_copyable_v<ByteStablePayload>);
static_assert(std::is_trivially_destructible_v<ByteStablePayload>);
static_assert(LibXR::TopicPayload<ByteStablePayload>);

struct alignas(LibXR::CACHE_LINE_SIZE) WideAlignedPayload
{
  uint64_t left;
  uint64_t right;
};

static_assert(LibXR::TopicPayload<WideAlignedPayload>);
static_assert(alignof(WideAlignedPayload) == LibXR::CACHE_LINE_SIZE);

struct PrefixIntPayload
{
  int32_t value;
  int32_t reserved;
};

static_assert(LibXR::TopicPayload<PrefixIntPayload>);
