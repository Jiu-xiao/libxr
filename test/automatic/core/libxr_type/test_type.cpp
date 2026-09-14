/**
 * @file test_type.cpp
 * @brief 检查 RawData 和 ConstRawData 的地址及长度。 /
 * Tests RawData and ConstRawData addresses and lengths.
 *
 * 使用字符串、非零结尾数组和含零字节的 string_view。
 * Uses strings, non-terminated arrays and string_view with embedded zero bytes.
 */

#include <string_view>

#include "libxr_def.hpp"
#include "libxr_type.hpp"
#include "test.hpp"
#include "test_assert.hpp"

void test_type()
{
  char mutable_text[] = "abc";
  LibXR::RawData mutable_text_view(mutable_text);
  TEST_ASSERT(mutable_text_view.addr_ == mutable_text);
  TEST_ASSERT(mutable_text_view.size_ == 3);

  char mutable_payload[3] = {'i', 'm', 'u'};
  LibXR::RawData mutable_payload_view(mutable_payload);
  TEST_ASSERT(mutable_payload_view.addr_ == mutable_payload);
  TEST_ASSERT(mutable_payload_view.size_ == 3);

  const char literal_text[] = "abc";
  LibXR::ConstRawData literal_text_view(literal_text);
  TEST_ASSERT(literal_text_view.addr_ == literal_text);
  TEST_ASSERT(literal_text_view.size_ == 3);

  const char embedded_text[] = "ab\0cd";
  LibXR::ConstRawData embedded_text_view(embedded_text);
  TEST_ASSERT(embedded_text_view.size_ == 5);

  const char bounded_payload[3] = {'g', 'p', 'u'};
  LibXR::ConstRawData bounded_payload_view(bounded_payload);
  TEST_ASSERT(bounded_payload_view.addr_ == bounded_payload);
  TEST_ASSERT(bounded_payload_view.size_ == 3);

  const std::string_view explicit_view("ab\0cd", 5);
  LibXR::ConstRawData explicit_view_data(explicit_view);
  TEST_ASSERT(explicit_view_data.addr_ == explicit_view.data());
  TEST_ASSERT(explicit_view_data.size_ == explicit_view.size());
}
