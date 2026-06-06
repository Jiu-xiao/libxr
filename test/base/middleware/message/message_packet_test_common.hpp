/**
 * @file message_packet_test_common.hpp
 * @brief message packet 测试共用 helper。 Shared helpers for message packet tests.
 */
#pragma once

#include <cstdint>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "message_test_payloads.hpp"
#include "test.hpp"

inline uint64_t TimestampUs(const LibXR::MicrosecondTimestamp& timestamp)
{
  return static_cast<uint64_t>(timestamp);
}

void RunMessagePacketParseTests();
void RunMessagePacketValidationTests();
void RunMessagePacketAlignmentTests();
