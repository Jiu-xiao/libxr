#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "core.hpp"
#include "ep_pool.hpp"
#include "lockfree_list.hpp"
#include "lockfree_pool.hpp"

namespace LibXR::USB
{
/**
 * @brief 字符串描述符管理器 / USB string descriptor manager
 *
 */
class DescriptorStrings
{
 public:
  /**
   * @brief 描述符字符串索引 / USB descriptor string index
   */
  enum class Index : uint8_t
  {
    LANGUAGE_ID = 0x00,          ///< 语言ID描述符 / LangID descriptor
    MANUFACTURER_STRING = 0x01,  ///< 厂商字符串索引 / Manufacturer string
    PRODUCT_STRING = 0x02,       ///< 产品字符串索引 / Product string
    SERIAL_NUMBER_STRING = 0x03  ///< 序列号字符串索引 / Serial number string
  };

  /**
   * @brief 语言 / Supported language
   */
  enum class Language : uint16_t
  {
    EN_US = 0x0409,  ///< 英语 / English (US)
    ZH_CN = 0x0804   ///< 简体中文 / Simplified Chinese
  };

  typedef const char* StringData;
  static constexpr size_t STRING_LIST_SIZE = 3;

  /**
   * @brief 单语言描述符包（用于注册静态多语言字符串）
   *        Single language USB string descriptor pack for registration
   */
  struct LanguagePack
  {
    Language lang_id;                      ///< 语言 / Language
    StringData strings[STRING_LIST_SIZE];  ///< 指向UTF-16LE静态数组 / Pointers to
                                           ///< UTF-16LE static arrays
    size_t string_lens[STRING_LIST_SIZE];  ///< 每个字符串的字节数 / Size (bytes) of each
                                           ///< string
    size_t max_string_length;              ///< 最大字符串长度 / Maximum string length
  };

  /**
   * @brief 编译期构造LanguagePack
   *        Compile-time LanguagePack constructor
   *
   * @tparam N1/N2/N3 3个字符串字面量长度
   * @param lang 语言ID / Language ID
   * @param manu 制造商字符串 / Manufacturer
   * @param prod 产品字符串 / Product
   * @param serial 序列号字符串 / Serial number
   * @return 静态LanguagePack（供注册时传地址用）
   */
  template <size_t N1, size_t N2, size_t N3>
  static const constexpr LanguagePack MakeLanguagePack(Language lang,
                                                       const char (&manu)[N1],
                                                       const char (&prod)[N2],
                                                       const char (&serial)[N3])
  {
    static_assert(N1 < 128 && N2 < 128 && N3 < 128,
                  "String length must be less than 128.");

    auto len_manu = CalcUTF16LELen(manu);
    auto len_prod = CalcUTF16LELen(prod);
    auto len_serial = CalcUTF16LELen(serial);

    size_t maxlen = len_manu;
    if (len_prod > maxlen)
    {
      maxlen = len_prod;
    }
    if (len_serial > maxlen)
    {
      maxlen = len_serial;
    }

    return LanguagePack{
        lang, {manu, prod, serial}, {len_manu, len_prod, len_serial}, maxlen};
  }

  /**
   * @brief USB 描述符字符串管理器构造函数
   *        USB descriptor string manager constructor
   *
   * @param lang_list 全局/静态LanguagePack对象指针表 / Pointers to static LanguagePack
   */
  DescriptorStrings(const std::initializer_list<const LanguagePack*>& lang_list);

  /**
   * @brief 生成指定语言和索引的字符串描述符
   *        Generate USB string descriptor for given language and string index
   *
   * @param index 字符串类型索引 / String index
   * @param lang  语言ID / Language ID
   * @return 错误码 / Error code
   */
  ErrorCode GenerateString(Index index, uint16_t lang);
  /**
   * @brief 获取当前构建好的字符串描述符数据
   *        Get the descriptor buffer
   * @return RawData 数据结构
   */
  RawData GetData();

  /**
   * @brief 获取语言ID描述符内容
   *        Get LangID descriptor data
   * @return RawData 数据结构 / RawData
   */
  RawData GetLangIDData();

 private:
  template <size_t N>
  static constexpr size_t CalcUTF16LELen(const char (&input)[N])
  {
    size_t len = 0;
    for (size_t i = 0; i < N && input[i];)
    {
      unsigned char c = static_cast<unsigned char>(input[i]);
      if (c < 0x80)
      {
        len += 2;
        i += 1;
      }
      else if ((c & 0xE0) == 0xC0)
      {
        len += 2;
        i += 2;
      }
      else if ((c & 0xF0) == 0xE0)
      {
        len += 2;
        i += 3;
      }
      else
      {
        i += 4;
      }
    }
    return len;
  }

  static void ToUTF16LE(const char* str, uint8_t* buffer);

  const size_t LANG_NUM;              ///< 已注册语言数量 / Registered language count
  uint16_t* header_;                  ///< 语言ID描述符头部 / LangID descriptor header
  uint16_t* land_id_;                 ///< 语言ID数组 / LangID array
  const LanguagePack** string_list_;  ///< 多语言包指针表 / LanguagePack pointer table
  RawData buffer_;                    ///< 临时描述符缓冲区 / Temp descriptor buffer
};
}  // namespace LibXR::USB
