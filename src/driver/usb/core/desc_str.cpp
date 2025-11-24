#include "desc_str.hpp"

using namespace LibXR::USB;

DescriptorStrings::DescriptorStrings(
    const std::initializer_list<const LanguagePack*>& lang_list, const uint8_t* uid,
    size_t uid_len)
    : LANG_NUM(lang_list.size()),
      header_(new uint16_t[LANG_NUM + 1]),
      land_id_(header_ + 1),
      string_list_(new const LanguagePack*[LANG_NUM])
{
  ASSERT(LANG_NUM > 0);
  ASSERT(uid != nullptr || uid_len == 0);

  auto lang = lang_list.begin();
  size_t max_str_len = 0;

  // 每个字节 -> 2个HEX字符 -> 每字符2字节UTF-16LE => 每字节4字节UTF-16LE
  const size_t EXTRA_SERIAL_UTF16 = uid ? uid_len * 4 : 0;

  for (size_t i = 0; i < LANG_NUM; i++)
  {
    land_id_[i] = static_cast<uint16_t>((*lang)->lang_id);
    string_list_[i] = *lang;

    size_t lang_max = (*lang)->max_string_length;

    if (EXTRA_SERIAL_UTF16 != 0)
    {
      const size_t IDX_SERIAL = static_cast<size_t>(Index::SERIAL_NUMBER_STRING) - 1;
      size_t serial_prefix_len = (*lang)->string_lens[IDX_SERIAL];
      size_t serial_total_len = serial_prefix_len + EXTRA_SERIAL_UTF16;
      if (serial_total_len > lang_max)
      {
        lang_max = serial_total_len;
      }
    }

    if (max_str_len < lang_max)
    {
      max_str_len = lang_max;
    }

    ++lang;
  }

  *header_ = (static_cast<uint16_t>(LANG_NUM * 2 + 2)) | (0x03 << 8);
  buffer_.addr_ = new uint8_t[max_str_len + 2];
  buffer_.size_ = max_str_len + 2;

  ASSERT(max_str_len + 2 <= 255);

  serial_uid_ = uid;
  serial_uid_len_ = uid_len;
}

ErrorCode DescriptorStrings::GenerateString(Index index, uint16_t lang)
{
  ASSERT(buffer_.addr_ != nullptr);

  if (index == Index::LANGUAGE_ID)
  {
    return ErrorCode::NOT_SUPPORT;
  }

  int ans = -1;
  for (size_t i = 0; i < LANG_NUM; ++i)
  {
    if (land_id_[i] == lang)
    {
      ans = static_cast<int>(i);
      break;
    }
  }
  if (ans == -1)
  {
    return ErrorCode::NOT_FOUND;
  }

  uint8_t* buffer = reinterpret_cast<uint8_t*>(buffer_.addr_);

  // 有 UID 且请求的是 Serial：前缀 + UID 的十六进制字符串
  if (index == Index::SERIAL_NUMBER_STRING && serial_uid_ != nullptr)
  {
    const LanguagePack* pack = string_list_[ans];
    constexpr size_t IDX = static_cast<size_t>(Index::SERIAL_NUMBER_STRING) - 1;

    const char* const SERIAL_PREFIX = pack->strings[IDX];
    const size_t PREFIX_UTF16_LEN = pack->string_lens[IDX];

    const uint8_t DATA_LEN =
        static_cast<uint8_t>(PREFIX_UTF16_LEN + serial_uid_len_ * 4 + 2);

    ASSERT(PREFIX_UTF16_LEN + serial_uid_len_ * 4 + 2 <= 255);

    buffer[1] = 0x03;
    buffer[0] = DATA_LEN;

    uint8_t* out = buffer + 2;

    // 先写前缀（UTF-8 -> UTF-16LE）
    ToUTF16LE(SERIAL_PREFIX, out);
    out += PREFIX_UTF16_LEN;  // 前缀的 UTF-16LE 字节数

    // 再写 UID 的十六进制（ASCII 0-9 A-F）
    static const char HEX[16] = {'0', '1', '2', '3', '4', '5', '6', '7',
                                 '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

    for (size_t i = 0; i < serial_uid_len_; ++i)
    {
      uint8_t b = serial_uid_[i];
      char hi = HEX[(b >> 4) & 0x0F];
      char lo = HEX[b & 0x0F];

      // hi
      *out++ = static_cast<uint8_t>(hi);
      *out++ = 0x00;

      // lo
      *out++ = static_cast<uint8_t>(lo);
      *out++ = 0x00;
    }

    return ErrorCode::OK;
  }

  // 其它字符串：走原来的逻辑
  auto data_len = string_list_[ans]->string_lens[static_cast<size_t>(index) - 1] + 2;

  buffer[1] = 0x03;
  buffer[0] = static_cast<uint8_t>(data_len);
  const char* str = string_list_[ans]->strings[static_cast<size_t>(index) - 1];
  ToUTF16LE(str, buffer + 2);
  return ErrorCode::OK;
}

LibXR::RawData DescriptorStrings::GetData()
{
  ASSERT(buffer_.addr_ != nullptr);
  uint8_t* buffer = reinterpret_cast<uint8_t*>(buffer_.addr_);
  return RawData{buffer_.addr_, buffer[0]};
}

LibXR::RawData DescriptorStrings::GetLangIDData()
{
  return RawData{header_, (LANG_NUM + 1) * sizeof(uint16_t)};
}

void DescriptorStrings::ToUTF16LE(const char* str, uint8_t* buffer)
{
  size_t len = 0;
  const unsigned char* s = reinterpret_cast<const unsigned char*>(str);

  while (*s)
  {
    uint32_t codepoint = 0;

    if (*s < 0x80)
    {
      codepoint = *s++;
    }
    else if ((*s & 0xE0) == 0xC0)
    {
      codepoint = (*s & 0x1F) << 6;
      s++;
      codepoint |= (*s & 0x3F);
      s++;
    }
    else if ((*s & 0xF0) == 0xE0)
    {
      codepoint = (*s & 0x0F) << 12;
      s++;
      codepoint |= (*s & 0x3F) << 6;
      s++;
      codepoint |= (*s & 0x3F);
      s++;
    }
    else if ((*s & 0xF8) == 0xF0)
    {
      // 忽略超出 BMP 的字符（如 emoji）
      s++;
      if (*s)
      {
        s++;
      }
      if (*s)
      {
        s++;
      }
      if (*s)
      {
        s++;
      }
      continue;
    }
    else
    {
      s++;
      continue;
    }

    buffer[len++] = codepoint & 0xFF;
    buffer[len++] = (codepoint >> 8) & 0xFF;
  }
}
