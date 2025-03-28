#pragma once

#include <cstring>
#include <initializer_list>
#include <tuple>

#include "libxr_def.hpp"
#include "libxr_type.hpp"
#include "list.hpp"

namespace LibXR
{

/**
 * @brief 硬件条目模板结构体，存储对象引用及其别名列表
 * @brief Hardware entry template structure, stores object reference and its alias list
 * @tparam T 条目对象类型 / Type of entry object
 */
template <typename T>
struct Entry
{
  T& object;                                   ///< 对象引用 / Object reference
  std::initializer_list<const char*> aliases;  ///< 别名列表 / Alias list
};

/**
 * @brief 硬件容器类，管理多个硬件设备及其别名
 * @brief Hardware container class managing multiple devices and their aliases
 * @tparam Entries 可变模板参数，设备条目类型 / Variadic template parameter for device
 * entry types
 */
template <typename... Entries>
class HardwareContainer
{
 public:
  /**
   * @brief 构造函数，使用右值引用转发设备条目
   * @brief Constructor forwarding device entries using rvalue references
   * @param entries 设备条目可变参数 / Variadic device entries
   */
  constexpr HardwareContainer(Entries&&... entries)
      : devices_(std::forward<Entries>(entries)...)
  {
    // Register aliases for each Entry<T> during initialization
    (RegisterAliases(entries), ...);
  }

  /**
   * @brief 通过索引获取指定设备条目
   * @brief Get device entry by index
   * @tparam Index 设备条目在元组中的索引 / Index of device entry in tuple
   * @return auto& 返回设备条目的引用 / Returns reference to device entry
   */
  template <std::size_t Index>
  auto& Get()
  {
    return std::get<Index>(devices_);
  }

  /**
   * @brief 为设备注册别名
   * @brief Register alias for a device
   * @tparam T 设备类型与别名名称 / Type of device and alias name
   * @param entry 设备对象引用 / Device object reference
   */
  template <typename T>
  void RegisterAliases(const Entry<T>& entry)
  {
    for (const auto& alias : entry.aliases)  // Register each alias for the device
    {
      auto node = new LibXR::List::Node<AliasEntry>{
          alias, static_cast<void*>(&entry.object), TypeID::GetID<T>()};
      alias_list_.Add(*node);
    }
  }
  /**
   * @brief 通过别名查找设备
   * @brief Find device by alias name
   * @tparam T 设备类型 / Type of device
   * @param alias 要查找的别名名称 / Alias name to look up
   * @return T* 找到的设备指针，未找到返回nullptr / Device pointer if found, nullptr
   * otherwise
   */
  template <typename T>
  T* Find(const char* alias) const
  {
    T* result = nullptr;
    alias_list_.Foreach<AliasEntry>(
        [&](AliasEntry& entry)
        {
          if (std::strcmp(entry.name, alias) == 0)
          {
            result = static_cast<T*>(entry.object);
            return ErrorCode::FAILED;  // 停止搜索 / Stop searching
          }
          return ErrorCode::OK;
        });
    return result;
  }

 private:
  /// @brief 别名条目结构体，存储别名名称和对象指针
  /// @brief Alias entry structure storing alias name and object pointer
  struct AliasEntry
  {
    const char* name;  ///< 别名名称 / Alias name
    void* object;      ///< 对象指针 / Object pointer
    TypeID::ID id;     ///< 对象类型标识符 / Object type identifier
  };

  std::tuple<Entries...>
      devices_;  ///< 设备元组存储所有条目 / Tuple storing all device entries
  mutable LibXR::List
      alias_list_;  ///< 别名链表，支持常量操作 / Alias list supporting const operations
};

}  // namespace LibXR