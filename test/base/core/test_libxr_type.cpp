/**
 * @file test_libxr_type.cpp
 * @brief Raw-data view construction tests for `libxr_type.hpp`.
 *
 * Test items:
 * 1. Mutable array views: verify `RawData` captures the original address and byte count for writable bounded arrays.
 * 2. Const array and string views: verify `ConstRawData` preserves bounded-array length, embedded-NUL payloads and explicit `std::string_view` sizes.
 *
 * Test principle:
 * 1. Use bounded arrays and embedded-NUL payloads, because this API must distinguish true byte spans from ordinary C-string termination.
 * 2. Check both address identity and size, since downstream code relies on the pair rather than on textual interpretation.
 */
#include <string_view>

#include "libxr_def.hpp"
#include "libxr_type.hpp"
#include "test.hpp"

void test_type()
{
  char mutable_text[] = "abc";
  LibXR::RawData mutable_text_view(mutable_text);
  ASSERT(mutable_text_view.addr_ == mutable_text);
  ASSERT(mutable_text_view.size_ == 3);

  char mutable_payload[3] = {'i', 'm', 'u'};
  LibXR::RawData mutable_payload_view(mutable_payload);
  ASSERT(mutable_payload_view.addr_ == mutable_payload);
  ASSERT(mutable_payload_view.size_ == 3);

  const char literal_text[] = "abc";
  LibXR::ConstRawData literal_text_view(literal_text);
  ASSERT(literal_text_view.addr_ == literal_text);
  ASSERT(literal_text_view.size_ == 3);

  const char embedded_text[] = "ab\0cd";
  LibXR::ConstRawData embedded_text_view(embedded_text);
  ASSERT(embedded_text_view.size_ == 5);

  const char bounded_payload[3] = {'g', 'p', 'u'};
  LibXR::ConstRawData bounded_payload_view(bounded_payload);
  ASSERT(bounded_payload_view.addr_ == bounded_payload);
  ASSERT(bounded_payload_view.size_ == 3);

  const std::string_view explicit_view("ab\0cd", 5);
  LibXR::ConstRawData explicit_view_data(explicit_view);
  ASSERT(explicit_view_data.addr_ == explicit_view.data());
  ASSERT(explicit_view_data.size_ == explicit_view.size());
}
