#pragma once

#include <cstring>
#include <initializer_list>

#include "libxr_def.hpp"
#include "libxr_type.hpp"
#include "lockfree_list.hpp"

namespace LibXR
{
/**
 * @brief `app_framework` 的硬件注册片段
 *        Hardware-registration fragment of `app_framework`
 *
 * @note 这一组类型只负责“设备对象通过别名注册并按类型查找”，不处理模块执行调度。
 *       This group is responsible only for registering device objects under
 *       aliases and looking them up by type; it does not handle application
 *       execution scheduling.
 */
/**
 * 硬件条目模板 / Hardware entry template
 * @tparam T 设备类型 / Device type
 *
 * @note `aliases` 中的每个别名都必须是非空字符串指针。
 *       Every alias in `aliases` must be a non-null string pointer.
 */
template <typename T>
struct Entry
{
  T& object;  ///< 被注册的设备对象引用 / Reference to the device object being registered.
  std::initializer_list<const char*>
      aliases;  ///< 指向同一设备的别名集合 / Alias set pointing to the same device.
};

/**
 * 硬件容器类 / Hardware container
 *
 * Each registered alias is stored together with the object's erased pointer and
 * its `TypeID`, so name lookup and type filtering happen in one pass.
 * 每个注册别名都会连同对象的擦除指针和对应 `TypeID` 一起存储，因此名称匹配和类型过滤
 * 会在一次遍历里同时完成。
 */
class HardwareContainer
{
 public:
  /**
   * 构造并注册所有别名 / Construct and register aliases
   * @param entries 硬件条目列表 / Hardware entry list
   */
  template <typename... Entries>
  constexpr HardwareContainer(Entries&&... entries)
  {
    (Register(std::forward<Entries>(entries)), ...);
  }

  /**
   * 查找设备（单个别名） / Find device by alias
   * @note `alias` 必须非空。
   *       `alias` must be non-null.
   */
  template <typename T>
  T* Find(const char* alias) const
  {
    ASSERT(alias != nullptr);
    T* result = nullptr;
    const auto wanted_id = TypeID::GetID<T>();
    alias_list_.Foreach<AliasEntry>(
        [&](AliasEntry& entry)
        {
          if (std::strcmp(entry.name, alias) == 0 && entry.id == wanted_id)
          {
            result = static_cast<T*>(entry.object);
            return ErrorCode::FAILED;
          }
          return ErrorCode::OK;
        });
    return result;
  }

  /**
   * 查找设备（多个别名） / Find device with fallback aliases
   * @note `ALIASES` 中的每个别名都必须非空。
   *       Every alias in `ALIASES` must be non-null.
   */
  template <typename T>
  T* Find(const std::initializer_list<const char*> ALIASES) const
  {
    for (const auto& alias : ALIASES)
    {
      ASSERT(alias != nullptr);
      if (T* obj = Find<T>(alias))
      {
        return obj;
      }
    }
    return nullptr;
  }

  /**
   * 查找或强约束失败 / Find or REQUIRE if not found
   * @note `aliases` 中的每个别名都必须非空。
   *       Every alias in `aliases` must be non-null.
   */
  template <typename T>
  T* FindOrExit(std::initializer_list<const char*> aliases) const
  {
    T* result = Find<T>(aliases);
    REQUIRE(result != nullptr);
    return result;
  }

  /**
   * @brief 注册一个硬件条目
   * @brief Register a hardware entry
   *
   * @note 包含动态内存分配。
   *       Contains dynamic memory allocation.
   * @note `entry.aliases` 中的每个别名都必须非空。
   *       Every alias in `entry.aliases` must be non-null.
   */
  template <typename T>
  void Register(const Entry<T>& entry)
  {
    for (const auto& alias : entry.aliases)
    {
      ASSERT(alias != nullptr);
      auto node = new (std::align_val_t(LibXR::CONCURRENCY_ALIGNMENT))
          LibXR::LockFreeList::Node<AliasEntry>{alias, static_cast<void*>(&entry.object),
                                                TypeID::GetID<T>()};
      alias_list_.Add(*node);
    }
  }

 protected:
  /**
   * @brief 一条硬件别名记录
   *        One registered hardware-alias record
   */
  struct AliasEntry
  {
    const char* name;  ///< 硬件别名 / Hardware alias string.
    void* object;      ///< 擦除类型后的设备对象指针 / Type-erased device object pointer.
    TypeID::ID id;     ///< 设备对象类型 ID / Device object type ID.
  };

  mutable LibXR::LockFreeList alias_list_;  ///< 当前已注册的全部别名链表 / List of all currently registered aliases.
};

}  // namespace LibXR
