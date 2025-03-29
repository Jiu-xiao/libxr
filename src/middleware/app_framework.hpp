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
 * 硬件条目模板 / Hardware entry template
 * @tparam T 设备类型 / Device type
 */
template <typename T>
struct Entry
{
  T& object;
  std::initializer_list<const char*> aliases;
};

/**
 * 硬件容器类 / Hardware container
 * @tparam Entries 所有设备条目类型 / All device entry types
 */
template <typename... Entries>
class HardwareContainer
{
 public:
  /**
   * 构造并注册所有别名 / Construct and register aliases
   */
  constexpr HardwareContainer(Entries&&... entries)
      : devices_(std::forward<Entries>(entries)...)
  {
    (RegisterAliases(entries), ...);
  }

  /**
   * 按索引获取条目 / Get entry by index
   */
  template <std::size_t Index>
  auto& Get()
  {
    return std::get<Index>(devices_);
  }

  /**
   * 查找设备（单个别名） / Find device by alias
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
            return ErrorCode::FAILED;
          }
          return ErrorCode::OK;
        });
    return result;
  }

  /**
   * 查找设备（多个别名） / Find device with fallback aliases
   */
  template <typename T>
  T* Find(std::initializer_list<const char*> aliases) const
  {
    for (const auto& alias : aliases)
    {
      if (T* obj = Find<T>(alias))
      {
        return obj;
      }
    }
    return nullptr;
  }

  /**
   * 查找或报错 / Find or ASSERT if not found
   */
  template <typename T>
  T* FindOrExit(std::initializer_list<const char*> aliases) const
  {
    T* result = Find<T>(aliases);
    ASSERT(result != nullptr);
    return result;
  }

  /**
   * 遍历所有别名条目 / Visit all alias entries
   */
  template <typename Callback>
  void RegisterAll(Callback cb) const
  {
    alias_list_.Foreach<AliasEntry>(
        [&](const AliasEntry& entry)
        {
          cb(entry);
          return ErrorCode::OK;
        });
  }

 private:
  struct AliasEntry
  {
    const char* name;
    void* object;
    TypeID::ID id;
  };

  std::tuple<Entries...> devices_;
  mutable LibXR::List alias_list_;

  template <typename T>
  void RegisterAliases(const Entry<T>& entry)
  {
    for (const auto& alias : entry.aliases)
    {
      auto node = new LibXR::List::Node<AliasEntry>{
          alias, static_cast<void*>(&entry.object), TypeID::GetID<T>()};
      alias_list_.Add(*node);
    }
  }
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
  LibXR::List app_list_;  ///< 模块链表 / Module list

  /**
   * @brief 注册一个应用模块
   * @brief Register an application module
   *
   * @param app 模块实例引用 / Reference to an Application instance
   */
  void Register(Application& app)
  {
    auto node = new LibXR::List::Node<Application*>(&app);
    app_list_.Add(*node);
  }

  /**
   * @brief 调用所有模块的 OnMonitor
   * @brief Call OnMonitor for all registered modules
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
