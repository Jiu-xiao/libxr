#pragma once

#include "libxr_cb.hpp"
#include "libxr_def.hpp"
#include "lockfree_list.hpp"
#include "rbt.hpp"

namespace LibXR
{

/**
 * @class Event
 * @brief 事件管理系统，允许基于事件 ID 注册和触发回调函数。 Event management system that
 * allows registration and triggering of callbacks based on event IDs.
 */
class Event
{
 public:
  using Callback = LibXR::Callback<uint32_t>;

  /**
   * @brief 构造函数，初始化用于存储事件的红黑树。 Constructs an Event object with an
   * empty red-black tree for event storage.
   */
  Event()
      : rbt_([](const uint32_t &a, const uint32_t &b)
             { return static_cast<int>(a) - static_cast<int>(b); })
  {
  }

  /**
   * @brief 为特定事件注册回调函数。 Registers a callback function for a specific event.
   * @param event 要注册回调的事件 ID。 The event ID to register the callback for.
   * @param cb 事件触发时执行的回调函数。 The callback function to be executed when the
   * event occurs.
   */
  void Register(uint32_t event, const Callback &cb)
  {
    auto list = rbt_.Search<LockFreeList>(event);

    if (!list)
    {
      list = new RBTree<uint32_t>::Node<LockFreeList>;
      rbt_.Insert(*list, event);
    }

    LockFreeList::Node<Block> *node = new LockFreeList::Node<Block>;

    node->data_.event = event;
    node->data_.cb = cb;
    list->data_.Add(*node);
  }

  /**
   * @brief 触发与特定事件关联的所有回调函数。 Triggers all callbacks associated with a
   * specific event.
   * @param event 要激活的事件 ID。 The event ID to activate.
   */
  void Active(uint32_t event)
  {
    auto list = rbt_.Search<LockFreeList>(event);
    if (!list)
    {
      return;
    }

    auto foreach_fun = [=](Block &block)
    {
      block.cb.Run(false, event);
      return ErrorCode::OK;
    };

    list->data_.Foreach<LibXR::Event::Block>(foreach_fun);
  }

  /**
   * @brief 在中断服务程序（ISR）上下文中触发事件回调。 Triggers event callbacks from an
   * interrupt service routine (ISR) context.
   * @param event 要激活的事件 ID。 The event ID to activate.
   * @param in_isr 是否从 ISR 调用该函数。 Whether the function is being called from an
   * ISR.
   */
  void ActiveFromCallback(uint32_t event, bool in_isr)
  {
    auto list = rbt_.Search<LockFreeList>(event);
    if (!list)
    {
      return;
    }

    auto foreach_fun = [=](Block &block)
    {
      block.cb.Run(in_isr, event);
      return ErrorCode::OK;
    };

    list->data_.Foreach<Block>(foreach_fun);
  }

  /**
   * @brief 将源事件绑定到当前事件实例中的目标事件。 Binds an event from a source Event
   * instance to a target event in the current Event instance.
   * @param sources 包含原始事件的源 Event 实例。 The source Event instance containing the
   * original event.
   * @param source_event 源事件实例中的事件 ID。 The event ID in the source Event
   * instance.
   * @param target_event 当前事件实例中的目标事件 ID。 The corresponding event ID in the
   * current Event instance.
   */
  void Bind(Event &sources, uint32_t source_event, uint32_t target_event)
  {
    struct BindBlock
    {
      Event *target;
      uint32_t event;
    };

    auto block = new BindBlock{this, target_event};

    auto bind_fun = [](bool in_isr, BindBlock *block, uint32_t event)
    {
      UNUSED(event);
      block->target->ActiveFromCallback(block->event, in_isr);
    };

    auto cb = Callback::Create(bind_fun, block);

    sources.Register(source_event, cb);
  }

 private:
  /**
   * @struct Block
   * @brief 用于存储事件回调的数据结构。 Data structure for storing event callbacks.
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
