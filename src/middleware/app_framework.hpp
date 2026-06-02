#pragma once

#include <cstring>
#include <initializer_list>

#include "libxr_def.hpp"
#include "libxr_type.hpp"
#include "lockfree_list.hpp"

namespace LibXR
{
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
  T& object;                                   ///< 设备对象 / Device object
  std::initializer_list<const char*> aliases;  ///< 别名列表 / Alias list
};

/**
 * 硬件容器类 / Hardware container
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
      auto node = new (std::align_val_t(LibXR::CACHE_LINE_SIZE))
          LibXR::LockFreeList::Node<AliasEntry>{alias, static_cast<void*>(&entry.object),
                                                TypeID::GetID<T>()};
      alias_list_.Add(*node);
    }
  }

 protected:
  struct AliasEntry
  {
    const char* name;
    void* object;
    TypeID::ID id;
  };

  mutable LibXR::LockFreeList alias_list_;
};

/**
 * @brief 应用模块抽象类，需实现 OnMonitor 方法
 * @brief Application module interface, must implement OnMonitor
 */
class Application
{
 public:
  virtual void OnMonitor() = 0;  ///< 周期性任务 / Periodic update
  virtual ~Application() = default;
};

/**
 * @brief 应用模块管理器
 * @brief Manager for registering and updating application modules
 */
class ApplicationManager
{
 public:
  LibXR::LockFreeList app_list_;  ///< 模块链表 / Module list

  /**
   * @brief 注册一个应用模块
   * @brief Register an application module
   *
   * @param app 模块实例引用 / Reference to an Application instance
   *
   * @note 包含动态内存分配。
   *       Contains dynamic memory allocation.
   */
  void Register(Application& app)
  {
    auto node = new (std::align_val_t(LibXR::CACHE_LINE_SIZE))
        LibXR::LockFreeList::Node<Application*>(&app);
    app_list_.Add(*node);
  }

  /**
   * @brief 调用所有模块的 OnMonitor
   * @brief Call OnMonitor for all registered modules
   *
   * @note 不保证模块执行顺序；当前实现按 `app_list_` 当下的链表遍历顺序调用。
   *       No module execution order is guaranteed; the current implementation
   *       follows the list traversal order of `app_list_` at the time of the call.
   */
  void MonitorAll()
  {
    app_list_.Foreach<Application*>(
        [](Application* app)
        {
          app->OnMonitor();
          return ErrorCode::OK;
        });
  }

  /**
   * @brief 获取模块数量
   * @brief Get number of registered modules
   *
   * @return 模块总数 / Total module count
   */
  size_t Size() { return app_list_.Size(); }
};

}  // namespace LibXR
