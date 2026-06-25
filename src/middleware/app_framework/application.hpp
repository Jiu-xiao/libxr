#pragma once

#include "libxr_def.hpp"
#include "lockfree_list.hpp"

namespace LibXR
{
/**
 * @brief `app_framework` 的应用模块管理片段
 *        Application-module management fragment of `app_framework`
 *
 * @note 这一组类型只负责“模块对象注册并统一调用 `OnMonitor()`”，不处理硬件别名查找。
 *       This group is responsible only for registering module objects and
 *       dispatching `OnMonitor()` across them; it does not handle hardware
 *       alias lookup.
 */

/**
 * @brief 应用模块抽象类，需实现 OnMonitor 方法
 * @brief Application module interface, must implement OnMonitor
 */
class Application
{
 public:
  /**
   * @brief 周期性模块入口
   *        Periodic module entry point
   */
  virtual void OnMonitor() = 0;  ///< 周期性任务 / Periodic update

  /**
   * @brief 虚析构函数
   *        Virtual destructor
   */
  virtual ~Application() = default;
};

/**
 * @brief 应用模块管理器
 * @brief Manager for registering and updating application modules
 */
class ApplicationManager
{
 public:
  LibXR::LockFreeList app_list_;  ///< 当前已注册模块链表 / List of currently registered modules.

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
    auto node = new (std::align_val_t(LibXR::CONCURRENCY_ALIGNMENT))
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
