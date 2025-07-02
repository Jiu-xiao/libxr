#pragma once

#include "libxr_cb.hpp"
#include "libxr_def.hpp"
#include "lockfree_list.hpp"
#include "rbt.hpp"

namespace LibXR
{

/**
 * @class Event
 * @brief 事件管理系统，允许基于事件 ID 注册和触发回调函数。
 *        Event management system that allows registration and triggering of callbacks
 * based on event IDs.
 */
class Event
{
 public:
  using Callback = LibXR::Callback<uint32_t>;

  /**
   * @brief 回调链表指针类型，用于事件触发时从 ISR 中安全调用。
   *        Pointer to the callback list, safe to use in ISR after acquired in
   * non-interrupt context.
   */
  using CallbackList = LockFreeList *;

  /**
   * @brief 构造函数，初始化用于存储事件的红黑树。
   *        Constructs an Event object with an empty red-black tree for event storage.
   */
  Event();

  /**
   * @brief 为特定事件注册回调函数。
   *        Registers a callback function for a specific event.
   * @param event 要注册回调的事件 ID。 The event ID to register the callback for.
   * @param cb    事件触发时执行的回调函数。 The callback function to be executed when the
   * event occurs.
   */
  void Register(uint32_t event, const Callback &cb);

  /**
   * @brief 触发与特定事件关联的所有回调函数（非中断上下文）。
   *        Triggers all callbacks associated with a specific event (non-interrupt
   * context).
   * @param event 要激活的事件 ID。 The event ID to activate.
   */
  void Active(uint32_t event);

  /**
   * @brief 从回调函数中触发与特定事件关联的所有回调函数。
   *        Triggers all callbacks associated with a specific event (interrupt
   * context).
   *
   * @param list 在非回调函数中获取的事件回调链表指针。 The event callback list pointer
   * obtained from the non-callback function.
   * @param event 要激活的事件 ID。 The event ID to activate.
   */
  void ActiveFromCallback(CallbackList list, uint32_t event);

  /**
   * @brief 获取指定事件的回调链表指针（必须在非中断上下文中调用）。
   *        Returns the callback list pointer for the given event (must be called outside
   * ISR).
   * @param event 要查询的事件 ID。 The event ID to search.
   * @return 回调链表指针，如果未注册则主动创建。 The callback list pointer, if not
   * registered, it is actively created.
   */
  CallbackList GetList(uint32_t event);

  /**
   * @brief 将源事件绑定到当前事件实例中的目标事件。
   *        Binds an event from a source Event instance to a target event in the current
   * instance.
   * @param sources       包含原始事件的源 Event 实例。 The source Event instance.
   * @param source_event  源事件实例中的事件 ID。 The source event ID.
   * @param target_event  当前实例中的目标事件 ID。 The target event ID in this instance.
   */
  void Bind(Event &sources, uint32_t source_event, uint32_t target_event);

 private:
  /**
   * @struct Block
   * @brief 用于存储事件回调的数据结构。
   *        Data structure for storing event callbacks.
   */
  struct Block
  {
    uint32_t event;  ///< 与该回调关联的事件 ID。 Event ID associated with this callback.
    Callback
        cb;  ///< 关联该事件的回调函数。 Callback function associated with this event.
  };

  RBTree<uint32_t> rbt_;  ///< 用于管理已注册事件的红黑树。 Red-black tree for managing
                          ///< registered events.
};

}  // namespace LibXR
