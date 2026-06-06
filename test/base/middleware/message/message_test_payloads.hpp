/**
 * @file message_test_payloads.hpp
 * @brief Shared payload types for message-bus tests.
 *
 * Provided payload shapes:
 * 1. `ByteStablePayload`: non-trivially-copyable but topic-legal payload used to verify typed topic transport without cache assumptions.
 * 2. `WideAlignedPayload`: cache-line aligned payload used to verify packet/server alignment handling.
 * 3. `PrefixIntPayload`: prefix-sized payload used to verify shorter packet-length compatibility.
 *
 * Design principle:
 * 1. Keep message test payloads in one shared header so topic-side and packet-side tests exercise the same contract shapes instead of drifting independently.
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
