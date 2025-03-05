#pragma once

#include "libxr_cb.hpp"
#include "libxr_def.hpp"
#include "list.hpp"
#include "mutex.hpp"
#include "rbt.hpp"

namespace LibXR {
class Event {
 public:
  Event()
      : rbt_([](const uint32_t &a, const uint32_t &b) {
          return static_cast<int>(a) - static_cast<int>(b);
        }) {}

  void Register(uint32_t event, const Callback<uint32_t> &cb) {
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

    auto foreach_fun = [](Block &block, uint32_t &event) {
      block.cb.Run(false, event);
      return ErrorCode::OK;
    };

    list->data_.Foreach<LibXR::Event::Block, uint32_t &>(foreach_fun, event);
  }

  void ActiveFromCallback(uint32_t event, bool in_isr) {
    auto list = rbt_.Search<List>(event);
    if (!list) {
      return;
    }

    auto foreach_fun = [](Block &block, uint32_t &event) {
      block.cb.Run(false, event);
      return ErrorCode::OK;
    };

    auto foreach_fun_isr = [](Block &block, uint32_t &event) {
      block.cb.Run(false, event);
      return ErrorCode::OK;
    };

    if (in_isr) {
      list->data_.Foreach<Block, uint32_t &>(foreach_fun, event);
    } else {
      list->data_.Foreach<Block, uint32_t &>(foreach_fun_isr, event);
    }
  }

  void Bind(Event &sources, uint32_t source_event, uint32_t target_event) {
    struct BindBlock {
      Event *target;
      uint32_t event;
    };

    auto block = new BindBlock{this, target_event};

    auto bind_fun = [](bool in_isr, BindBlock *block, uint32_t event) {
      UNUSED(event);
      block->target->ActiveFromCallback(block->event, in_isr);
    };

    auto cb = Callback<uint32_t>::Create(bind_fun, block);

    sources.Register(source_event, cb);
  }

 private:
  struct Block {
    uint32_t event;
    Callback<uint32_t> cb;
  };

  RBTree<uint32_t> rbt_;
};
}  // namespace LibXR
