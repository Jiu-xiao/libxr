#pragma once

#include "libxr_cb.hpp"
#include "libxr_def.hpp"
#include "list.hpp"
#include "mutex.hpp"
#include "rbt.hpp"
#include <cstdint>
#include <utility>

namespace LibXR {
class Event {
public:
  Event()
      : rbt_([](const uint32_t &a, const uint32_t &b) { return int(a - b); }) {}

  void Register(uint32_t event, const Callback<void, uint32_t> &cb) {

    auto list = rbt_.Search<List>(event);

    if (!list) {
      list = new RBTree<uint32_t>::Node<List>;
      rbt_.Insert(*list, event);
    }

    List::Node<Block> *node = new List::Node<Block>;

    node->data_.event = event;
    node->data_.cb = cb;
    list->data_.Add(*node);
  }

  void Active(uint32_t event) {
    auto list = rbt_.Search<List>(event);
    if (!list) {
      return;
    }

    ErrorCode (*foreach_fun)(Block &, uint32_t &) = [](Block &block,
                                                       uint32_t &event) {
      block.cb.RunFromUser(event);
      return NO_ERR;
    };

    list->data_.Foreach(foreach_fun, event);
  }

  void ActiveFromCallback(uint32_t event, bool in_isr) {
    auto list = rbt_.Search<List>(event);
    if (!list) {
      return;
    }

    ErrorCode (*foreach_fun)(Block &, uint32_t &) = [](Block &block,
                                                       uint32_t &event) {
      block.cb.RunFromUser(event);
      return NO_ERR;
    };

    ErrorCode (*foreach_fun_isr)(Block &, uint32_t &) = [](Block &block,
                                                           uint32_t &event) {
      block.cb.RunFromISR(event);
      return NO_ERR;
    };

    if (in_isr) {
      list->data_.Foreach(foreach_fun, event);
    } else {
      list->data_.Foreach(foreach_fun_isr, event);
    }
  }

  void Bind(Event &sources, uint32_t source_event, uint32_t target_event) {
    typedef struct {
      Event *target;
      uint32_t event;
    } Block;

    auto block = new Block;
    block->event = target_event;
    block->target = this;

    void (*bind_fun)(bool in_isr, Block *target, uint32_t event) =
        [](bool in_isr, Block *block, uint32_t event) {
          UNUSED(event);
          block->target->Active(block->event);
        };

    auto cb = Callback<void, uint32_t>::Create(bind_fun, block);

    sources.Register(source_event, cb);
  }

private:
  typedef struct {
    uint32_t event;
    Callback<void, uint32_t> cb;
  } Block;

  RBTree<uint32_t> rbt_;
};
} // namespace LibXR
