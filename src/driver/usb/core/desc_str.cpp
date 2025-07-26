#include "desc_str.hpp"

using namespace LibXR::USB;

DescriptorStrings::DescriptorStrings(
    const std::initializer_list<const LanguagePack*>& lang_list)
    : LANG_NUM(lang_list.size()),
      header_(new uint16_t[LANG_NUM + 1]),
      land_id_(header_ + 1),
      string_list_(new const LanguagePack*[LANG_NUM])
{
  ASSERT(LANG_NUM > 0);
  auto lang = lang_list.begin();
  size_t max_str_len = 0;
  for (size_t i = 0; i < LANG_NUM; i++)
  {
    land_id_[i] = static_cast<uint16_t>((*lang)->lang_id);
    string_list_[i] = *lang;
    if (max_str_len < (*lang)->max_string_length)
    {
      max_str_len = (*lang)->max_string_length;
    }
    ++lang;
  }
  *header_ = (static_cast<uint16_t>(LANG_NUM * 2 + 2)) | (0x03 << 8);
  buffer_.addr_ = new uint8_t[max_str_len + 2];
  buffer_.size_ = max_str_len + 2;
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
  auto data_len = string_list_[ans]->string_lens[static_cast<size_t>(index) - 1] + 2;

  buffer[1] = 0x03;
  buffer[0] = data_len;
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
