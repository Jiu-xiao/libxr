/**
 * @file message_packet_test_common.hpp
 * @brief message packet 测试共用 helper。 Shared helpers for message packet tests.
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "crc.hpp"
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "message_test_payloads.hpp"
#include "test.hpp"

inline uint64_t TimestampUs(const LibXR::MicrosecondTimestamp& timestamp)
{
  return static_cast<uint64_t>(timestamp);
}

template <typename Packet>
inline void RewritePacketPayloadLengthForTest(Packet& packet, size_t payload_len)
{
  auto* packet_bytes = reinterpret_cast<uint8_t*>(&packet);
  const auto crc_offset = sizeof(LibXR::Topic::PackedDataHeader) + payload_len;

  packet.raw.header_.SetDataLen(static_cast<uint32_t>(payload_len));
  packet.raw.header_.pack_header_crc8 =
      LibXR::CRC8::Calculate(&packet.raw,
                             sizeof(LibXR::Topic::PackedDataHeader) - sizeof(uint8_t));
  packet_bytes[crc_offset] = LibXR::CRC8::Calculate(packet_bytes, crc_offset);
}

void RunMessagePacketParseTests();
void RunMessagePacketValidationTests();
void RunMessagePacketAlignmentTests();
